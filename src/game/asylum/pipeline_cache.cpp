#include "pipeline_cache.h"
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <algorithm>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::pipeline_cache {
namespace {
constexpr size_t maxEntries=512,maxBytes=16*1024*1024,maxKey=512*1024;
struct Key {
 std::vector<unsigned char> bytes;
 ComPtr<IUnknown> root;
 uint64_t groups[8]{};size_t start{};
 D3D12_RASTERIZER_DESC raster{};
 void group(unsigned i){uint64_t h=14695981039346656037ull;for(size_t n=start;n<bytes.size();++n){h^=bytes[n];h*=1099511628211ull;}groups[i]=h;start=bytes.size();}
 template<class T> bool add(const T& v){return data(&v,sizeof(v));}
 bool data(const void* p,size_t n){if(n>maxKey-bytes.size()||(!p&&n))return false;if(n){auto* b=static_cast<const unsigned char*>(p);bytes.insert(bytes.end(),b,b+n);}return true;}
 bool text(const char* s){if(!s)return add(unsigned(0));size_t n{};while(n<256&&s[n])++n;if(n==256)return false;return add(unsigned(n+1))&&data(s,n+1);}
 bool shader(const D3D12_SHADER_BYTECODE& s){return add(s.BytecodeLength)&&data(s.pShaderBytecode,s.BytecodeLength);}
};
struct Entry {Key key;ComPtr<ID3D12PipelineState> pso;uint64_t used{};};
std::unordered_multimap<uint64_t,Entry> entries;
std::mutex mutex;Stats totals;uint64_t tick{};
thread_local bool inside{};
bool key_for(ID3D12Device* dev,const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d,Key& k){
 if(d.CachedPSO.CachedBlobSizeInBytes||d.CachedPSO.pCachedBlob||d.InputLayout.NumElements>32||d.StreamOutput.NumEntries>64||d.StreamOutput.NumStrides>4)return false;
 if((d.InputLayout.NumElements&&!d.InputLayout.pInputElementDescs)||(d.StreamOutput.NumEntries&&!d.StreamOutput.pSODeclaration)||(d.StreamOutput.NumStrides&&!d.StreamOutput.pBufferStrides))return false;
 if(d.pRootSignature&&FAILED(d.pRootSignature->QueryInterface(IID_PPV_ARGS(&k.root))))return false;
 ComPtr<IUnknown> identity;if(FAILED(dev->QueryInterface(IID_PPV_ARGS(&identity))))return false;
 if(!k.add(identity.Get())||!k.add(k.root.Get()))return false;k.group(0);
 if(!k.shader(d.VS)||!k.shader(d.PS)||!k.shader(d.DS)||!k.shader(d.HS)||!k.shader(d.GS))return false;k.group(1);
#define FIELD(x) if(!k.add(x))return false
 FIELD(d.StreamOutput.NumEntries);FIELD(d.StreamOutput.NumStrides);FIELD(d.StreamOutput.RasterizedStream);
 for(unsigned i=0;i<d.StreamOutput.NumEntries;++i){const auto& e=d.StreamOutput.pSODeclaration[i];FIELD(e.Stream);if(!k.text(e.SemanticName))return false;FIELD(e.SemanticIndex);FIELD(e.StartComponent);FIELD(e.ComponentCount);FIELD(e.OutputSlot);}
 for(unsigned i=0;i<d.StreamOutput.NumStrides;++i){FIELD(d.StreamOutput.pBufferStrides[i]);}
 k.group(2);
 FIELD(d.BlendState.AlphaToCoverageEnable);FIELD(d.BlendState.IndependentBlendEnable);
 // D3D12_BLEND_DESC explicitly ignores slots 1..7 when independent
 // blending is disabled. Keep every field of each participating slot.
 // Malformed or future enum values retain the original full key so a hit
 // cannot mask a driver validation failure in ignored fields.
 auto validBlend=[](D3D12_BLEND b,bool alpha){const unsigned n=unsigned(b);return ((n>=1&&n<=11)||(n>=14&&n<=21))&&(!alpha||(n!=3&&n!=4&&n!=9&&n!=10&&n!=16&&n!=17));};
 bool inactiveValid=true;
 for(unsigned i=1;i<8;++i){const auto& b=d.BlendState.RenderTarget[i];inactiveValid=inactiveValid&&(b.BlendEnable==FALSE||b.BlendEnable==TRUE)&&(b.LogicOpEnable==FALSE||b.LogicOpEnable==TRUE)&&!(b.BlendEnable&&b.LogicOpEnable)&&validBlend(b.SrcBlend,false)&&validBlend(b.DestBlend,false)&&validBlend(b.SrcBlendAlpha,true)&&validBlend(b.DestBlendAlpha,true)&&unsigned(b.BlendOp)>=1&&unsigned(b.BlendOp)<=5&&unsigned(b.BlendOpAlpha)>=1&&unsigned(b.BlendOpAlpha)<=5&&unsigned(b.LogicOp)<=15&&b.RenderTargetWriteMask<=15;}
 const unsigned blendSlots=(d.BlendState.IndependentBlendEnable||!inactiveValid)?8u:1u;FIELD(blendSlots);
 for(unsigned i=0;i<blendSlots;++i){const auto& b=d.BlendState.RenderTarget[i];FIELD(b.BlendEnable);FIELD(b.LogicOpEnable);FIELD(b.SrcBlend);FIELD(b.DestBlend);FIELD(b.BlendOp);FIELD(b.SrcBlendAlpha);FIELD(b.DestBlendAlpha);FIELD(b.BlendOpAlpha);FIELD(b.LogicOp);FIELD(b.RenderTargetWriteMask);}
 FIELD(d.SampleMask);k.group(3);const auto& r=d.RasterizerState;k.raster=r;
 FIELD(r.FillMode);FIELD(r.CullMode);FIELD(r.FrontCounterClockwise);FIELD(r.DepthBias);FIELD(r.DepthBiasClamp);FIELD(r.SlopeScaledDepthBias);FIELD(r.DepthClipEnable);FIELD(r.MultisampleEnable);FIELD(r.AntialiasedLineEnable);FIELD(r.ForcedSampleCount);FIELD(r.ConservativeRaster);
 k.group(4);
 const auto& z=d.DepthStencilState;FIELD(z.DepthEnable);FIELD(z.DepthWriteMask);FIELD(z.DepthFunc);FIELD(z.StencilEnable);FIELD(z.StencilReadMask);FIELD(z.StencilWriteMask);
 for(const auto* f:{&z.FrontFace,&z.BackFace}){FIELD(f->StencilFailOp);FIELD(f->StencilDepthFailOp);FIELD(f->StencilPassOp);FIELD(f->StencilFunc);}
 k.group(5);
 FIELD(d.InputLayout.NumElements);
 for(unsigned i=0;i<d.InputLayout.NumElements;++i){const auto& e=d.InputLayout.pInputElementDescs[i];if(!k.text(e.SemanticName))return false;FIELD(e.SemanticIndex);FIELD(e.Format);FIELD(e.InputSlot);FIELD(e.AlignedByteOffset);FIELD(e.InputSlotClass);FIELD(e.InstanceDataStepRate);}
 k.group(6);
 if(d.NumRenderTargets>8)return false;
 FIELD(d.IBStripCutValue);FIELD(d.PrimitiveTopologyType);FIELD(d.NumRenderTargets);for(auto format:d.RTVFormats){FIELD(format);}FIELD(d.DSVFormat);FIELD(d.SampleDesc.Count);FIELD(d.SampleDesc.Quality);FIELD(d.NodeMask);FIELD(d.Flags);k.group(7);
#undef FIELD
 return true;
}
uint64_t hash(const std::vector<unsigned char>& b){uint64_t h=14695981039346656037ull;for(auto c:b){h^=c;h*=1099511628211ull;}return h;}
void trim(size_t incoming){while(!entries.empty()&&(entries.size()>=maxEntries||totals.keyBytes+incoming>maxBytes)){auto old=std::min_element(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.second.used<b.second.used;});totals.keyBytes-=old->second.key.bytes.size();entries.erase(old);++totals.evicted;}}
}
HRESULT create(ID3D12Device* dev,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,REFIID iid,void** out,Factory factory){
 if(inside||!dev||!desc||!out||iid!=__uuidof(ID3D12PipelineState))return factory(dev,desc,iid,out);
 struct Guard{Guard(){inside=true;}~Guard(){inside=false;}} guard;
 Key key;bool eligible=false;try{eligible=key_for(dev,*desc,key);}catch(...){eligible=false;}
 std::lock_guard lock(mutex);++totals.requests;
 if(!eligible){++totals.bypassed;return factory(dev,desc,iid,out);}
 const auto h=hash(key.bytes);const auto range=entries.equal_range(h);
 for(auto i=range.first;i!=range.second;++i)if(i->second.key.bytes==key.bytes){i->second.used=++tick;++totals.hits;*out=i->second.pso.Get();i->second.pso->AddRef();return S_OK;}
 // Observe one out of 32 misses. Fingerprints only select a candidate for
 // diagnosis; they never establish cache equivalence or change rendering.
 if(totals.created%32==0&&!entries.empty()){
  const Key* nearest{};unsigned score{};
  for(const auto& [hash,entry]:entries){unsigned same{};for(unsigned g=0;g<8;++g)same+=entry.key.groups[g]==key.groups[g];if(!nearest||same>score){nearest=&entry.key;score=same;}}
  ++totals.nearSamples;for(unsigned g=0;g<8;++g)totals.changed[g]+=nearest->groups[g]!=key.groups[g];
  totals.depthBiasChanges+=nearest->raster.DepthBias!=key.raster.DepthBias;
  totals.slopeBiasChanges+=memcmp(&nearest->raster.SlopeScaledDepthBias,&key.raster.SlopeScaledDepthBias,sizeof(float))!=0;
  totals.clampChanges+=memcmp(&nearest->raster.DepthBiasClamp,&key.raster.DepthBiasClamp,sizeof(float))!=0;
 }
 const auto hr=factory(dev,desc,iid,out);if(FAILED(hr)||!*out)return hr;
 ++totals.created;
 try{trim(key.bytes.size());const auto size=key.bytes.size();Entry e;e.key=std::move(key);e.pso=static_cast<ID3D12PipelineState*>(*out);e.used=++tick;entries.emplace(h,std::move(e));totals.keyBytes+=size;}catch(...){++totals.bypassed;}
 return hr;
}
Stats stats(){std::lock_guard lock(mutex);auto s=totals;s.live=unsigned(entries.size());return s;}
void clear(){std::lock_guard lock(mutex);entries.clear();totals.keyBytes=0;}
bool fixture(ID3D12Device* dev,ID3D12CommandQueue* queue,Factory factory){
 clear();
 const char* code="float4 vs(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}float4 red():SV_Target{return float4(1,0,0,1);}float4 green():SV_Target{return float4(0,1,0,1);}";
 ComPtr<ID3DBlob> vs,red,green,serialized,error;
 if(FAILED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error))||FAILED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"red","ps_5_0",0,0,&red,&error))||FAILED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"green","ps_5_0",0,0,&green,&error)))return false;
 D3D12_ROOT_SIGNATURE_DESC signature{};ComPtr<ID3D12RootSignature> root;
 if(FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error))||FAILED(dev->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
 D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.VS={vs->GetBufferPointer(),vs->GetBufferSize()};d.PS={red->GetBufferPointer(),red->GetBufferSize()};d.SampleMask=~0u;
 d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;d.RasterizerState.DepthClipEnable=TRUE;
 for(auto& b:d.BlendState.RenderTarget){b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=15;}
 d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;d.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
 d.DepthStencilState.FrontFace=d.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
 d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;d.NumRenderTargets=1;d.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
 ComPtr<ID3D12PipelineState> first,other;if(FAILED(dev->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&first))))return false;
 auto afterFirst=stats();
 // The bytecode moves in memory on each request. Only its actual content,
 // not pointer identity, determines whether the pipeline can be shared.
 for(unsigned i=0;i<1024;++i){std::vector<unsigned char> copy(static_cast<unsigned char*>(red->GetBufferPointer()),static_cast<unsigned char*>(red->GetBufferPointer())+red->GetBufferSize());auto same=d;same.PS={copy.data(),copy.size()};ComPtr<ID3D12PipelineState> p;
  if(FAILED(dev->CreateGraphicsPipelineState(&same,IID_PPV_ARGS(&p)))||p.Get()!=first.Get())return false;
 }
 auto reused=stats();if(reused.hits-afterFirst.hits!=1024||reused.created!=afterFirst.created)return false;
 auto ignored=d;ignored.BlendState.RenderTarget[7].SrcBlend=D3D12_BLEND_SRC_ALPHA;ignored.BlendState.RenderTarget[7].BlendEnable=TRUE;
 ComPtr<ID3D12PipelineState> ignoredDirect,ignoredCached;
 if(FAILED(factory(dev,&ignored,__uuidof(ID3D12PipelineState),reinterpret_cast<void**>(ignoredDirect.GetAddressOf())))||FAILED(dev->CreateGraphicsPipelineState(&ignored,IID_PPV_ARGS(&ignoredCached)))||ignoredCached.Get()!=first.Get())return false;
 auto independent=ignored;independent.BlendState.IndependentBlendEnable=TRUE;ComPtr<ID3D12PipelineState> independentResult;
 if(FAILED(dev->CreateGraphicsPipelineState(&independent,IID_PPV_ARGS(&independentResult)))||independentResult.Get()==first.Get())return false;
 auto different=d;different.PS={green->GetBufferPointer(),green->GetBufferSize()};if(FAILED(dev->CreateGraphicsPipelineState(&different,IID_PPV_ARGS(&other)))||other.Get()==first.Get())return false;
 auto invalid=d;invalid.SampleDesc.Count=0;ComPtr<ID3D12PipelineState> rejected;
 const auto direct=factory(dev,&invalid,__uuidof(ID3D12PipelineState),reinterpret_cast<void**>(rejected.GetAddressOf()));rejected.Reset();
 const auto cached=dev->CreateGraphicsPipelineState(&invalid,IID_PPV_ARGS(&rejected));if(SUCCEEDED(direct)||cached!=direct||rejected)return false;
 auto unusedFormat=d;unusedFormat.RTVFormats[7]=DXGI_FORMAT_R16G16B16A16_FLOAT;
 const auto formatDirect=factory(dev,&unusedFormat,__uuidof(ID3D12PipelineState),reinterpret_cast<void**>(rejected.GetAddressOf()));rejected.Reset();
 const auto formatCached=dev->CreateGraphicsPipelineState(&unusedFormat,IID_PPV_ARGS(&rejected));if(formatCached!=formatDirect)return false;rejected.Reset();
 auto malformed=d;malformed.BlendState.RenderTarget[7].BlendEnable=TRUE;malformed.BlendState.RenderTarget[7].LogicOpEnable=TRUE;
 const auto malformedDirect=factory(dev,&malformed,__uuidof(ID3D12PipelineState),reinterpret_cast<void**>(rejected.GetAddressOf()));rejected.Reset();
 const auto malformedCached=dev->CreateGraphicsPipelineState(&malformed,IID_PPV_ARGS(&rejected));if(malformedCached!=malformedDirect)return false;rejected.Reset();
 for(unsigned i=0;i<520;++i){auto variant=d;variant.RasterizerState.DepthBias=int(i)+1;ComPtr<ID3D12PipelineState> p;if(FAILED(dev->CreateGraphicsPipelineState(&variant,IID_PPV_ARGS(&p))))return false;}
 const auto bounded=stats();if(bounded.live>maxEntries||bounded.keyBytes>maxBytes||bounded.evicted<=afterFirst.evicted||!bounded.depthBiasChanges||!bounded.nearSamples)return false;
 clear();if(stats().live||stats().keyBytes)return false;
 // Caller references must survive both eviction and clearing. Render from
 // those same red/green PSOs after the cache has dropped its references.
 auto render=[&](ID3D12PipelineState* pso,unsigned channel){
  ComPtr<ID3D12Resource> image,readback;ComPtr<ID3D12DescriptorHeap> heap;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=r.Height=16;r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;r.Format=DXGI_FORMAT_R8G8B8A8_UNORM;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if(FAILED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&image))))return false;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT64 size{};dev->GetCopyableFootprints(&r,0,1,0,&layout,nullptr,nullptr,&size);
  hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=size;b.Height=b.DepthOrArraySize=b.MipLevels=b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
  if(FAILED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)))||FAILED(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)))||FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||FAILED(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso,IID_PPV_ARGS(&list)))||FAILED(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return false;
  auto handle=heap->GetCPUDescriptorHandleForHeapStart();dev->CreateRenderTargetView(image.Get(),nullptr,handle);list->SetGraphicsRootSignature(root.Get());D3D12_VIEWPORT vp{0,0,16,16,0,1};D3D12_RECT rect{0,0,16,16};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&rect);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={image.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
  D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=image.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION dest{};dest.pResource=readback.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dest.PlacedFootprint=layout;list->CopyTextureRegion(&dest,0,0,0,&source,nullptr);if(FAILED(list->Close()))return false;
  ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;
  const bool complete=SUCCEEDED(queue->Signal(fence.Get(),1))&&SUCCEEDED(fence->SetEventOnCompletion(1,event))&&WaitForSingleObject(event,5000)==WAIT_OBJECT_0;CloseHandle(event);if(!complete)return false;
  void* pixels{};D3D12_RANGE range{0,size_t(size)};if(FAILED(readback->Map(0,&range,&pixels)))return false;bool exact=true;
  for(unsigned y:{1u,8u,14u})for(unsigned x:{1u,8u,14u}){auto* c=static_cast<unsigned char*>(pixels)+layout.Offset+y*layout.Footprint.RowPitch+x*4;for(unsigned j=0;j<4;++j)exact=exact&&c[j]==((j==channel||j==3)?255:0);}
  D3D12_RANGE empty{};readback->Unmap(0,&empty);return exact;
 };
 const bool pixels=render(first.Get(),0)&&render(other.Get(),1)&&render(ignoredDirect.Get(),0);
 printf("Pipeline reuse GPU: 1024 moved-bytecode requests reused exact PSO; documented inactive blend slots reused with direct/cached red pixels; independent blend separate; different shader separate; invalid request preserves HRESULT; 512-entry/16MiB key bounds, eviction+clear; caller-held red/green 9-point pixels %s\n",pixels?"PASS":"FAIL");
 clear();return pixels;
}
}
