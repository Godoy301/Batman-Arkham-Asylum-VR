#include "dynamic_pipeline.h"
#include "core/util/log.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <vector>
#include <deque>
#include <string>
#include <memory>
#include <unordered_map>
#include <map>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <climits>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::dynamic_pipeline {
namespace {
std::atomic<bool> enabled{};ID3D12Device* owner{};
std::atomic<uint64_t> variants{},binds{},staticFallbacks{},blobFallbacks{};
std::atomic<unsigned> liveVariants{},liveTemplates{};
std::atomic<void*> proxyVtable{};
pipeline_cache::Factory nativeFactory{};
struct Snapshot {
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> real;
 std::vector<unsigned char> shaders[5];std::vector<D3D12_INPUT_ELEMENT_DESC> input;
 std::vector<D3D12_SO_DECLARATION_ENTRY> declarations;std::vector<UINT> strides;std::deque<std::string> names;
 Snapshot(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d,ID3D12PipelineState* p):desc(d),root(d.pRootSignature),real(p){
  D3D12_SHADER_BYTECODE* parts[]={&desc.VS,&desc.PS,&desc.DS,&desc.HS,&desc.GS};
  for(unsigned i=0;i<5;++i)if(parts[i]->BytecodeLength){auto* b=static_cast<const unsigned char*>(parts[i]->pShaderBytecode);shaders[i].assign(b,b+parts[i]->BytecodeLength);parts[i]->pShaderBytecode=shaders[i].data();}
  if(d.InputLayout.NumElements){input.assign(d.InputLayout.pInputElementDescs,d.InputLayout.pInputElementDescs+d.InputLayout.NumElements);for(auto& e:input)if(e.SemanticName){names.emplace_back(e.SemanticName);e.SemanticName=names.back().c_str();}desc.InputLayout.pInputElementDescs=input.data();}
  if(d.StreamOutput.NumEntries){declarations.assign(d.StreamOutput.pSODeclaration,d.StreamOutput.pSODeclaration+d.StreamOutput.NumEntries);for(auto& e:declarations)if(e.SemanticName){names.emplace_back(e.SemanticName);e.SemanticName=names.back().c_str();}desc.StreamOutput.pSODeclaration=declarations.data();}
  if(d.StreamOutput.NumStrides){strides.assign(d.StreamOutput.pBufferStrides,d.StreamOutput.pBufferStrides+d.StreamOutput.NumStrides);desc.StreamOutput.pBufferStrides=strides.data();}
  ++liveTemplates;
 }
 ~Snapshot(){--liveTemplates;}
};
std::mutex snapshotsMutex;
std::unordered_map<ID3D12PipelineState*,std::weak_ptr<Snapshot>> snapshots;
std::shared_ptr<Snapshot> snapshot(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d,ID3D12PipelineState* p){
 std::lock_guard lock(snapshotsMutex);auto it=snapshots.find(p);if(it!=snapshots.end())if(auto s=it->second.lock())return s;
 auto s=std::make_shared<Snapshot>(d,p);if(snapshots.size()>=512)snapshots.clear();snapshots[p]=s;return s;
}
struct GuidLess {bool operator()(const GUID&a,const GUID&b)const{return memcmp(&a,&b,sizeof(a))<0;}};
struct Private {std::vector<unsigned char> bytes;ComPtr<IUnknown> object;};
class Proxy final:public ID3D12PipelineState {
 std::atomic<ULONG> refs{1};std::mutex mutex;
 std::map<GUID,Private,GuidLess> data;ComPtr<ID3D12PipelineState> staticState;
public:
 std::shared_ptr<Snapshot> source;INT bias{};float clamp{},slope{};
 Proxy(std::shared_ptr<Snapshot> s,const D3D12_RASTERIZER_DESC& r):source(std::move(s)),bias(r.DepthBias),clamp(r.DepthBiasClamp),slope(r.SlopeScaledDepthBias){proxyVtable=*reinterpret_cast<void**>(this);++liveVariants;}
 ~Proxy(){--liveVariants;}
 HRESULT WINAPI QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==__uuidof(IUnknown)||iid==__uuidof(ID3D12Object)||iid==__uuidof(ID3D12DeviceChild)||iid==__uuidof(ID3D12Pageable)||iid==__uuidof(ID3D12PipelineState)){*out=static_cast<ID3D12PipelineState*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG WINAPI AddRef()override{return ++refs;}
 ULONG WINAPI Release()override{auto n=--refs;if(!n)delete this;return n;}
 HRESULT WINAPI GetPrivateData(REFGUID guid,UINT* size,void* out)override{
  if(!size)return E_POINTER;std::lock_guard lock(mutex);auto it=data.find(guid);if(it==data.end())return DXGI_ERROR_NOT_FOUND;
  const auto& p=it->second;const UINT required=UINT(p.object?sizeof(IUnknown*):p.bytes.size());const auto capacity=*size;*size=required;if(!out)return S_OK;if(capacity<required)return DXGI_ERROR_MORE_DATA;
  if(p.object){auto* object=p.object.Get();object->AddRef();memcpy(out,&object,sizeof(object));}else if(required)memcpy(out,p.bytes.data(),required);return S_OK;
 }
 HRESULT WINAPI SetPrivateData(REFGUID guid,UINT size,const void* value)override{
  if(size&&!value)return E_INVALIDARG;try{Private p;if(size){auto* b=static_cast<const unsigned char*>(value);p.bytes.assign(b,b+size);}std::lock_guard lock(mutex);if(size)data[guid]=std::move(p);else data.erase(guid);return S_OK;}catch(...){return E_OUTOFMEMORY;}
 }
 HRESULT WINAPI SetPrivateDataInterface(REFGUID guid,const IUnknown* object)override{try{Private p;p.object=const_cast<IUnknown*>(object);std::lock_guard lock(mutex);if(object)data[guid]=std::move(p);else data.erase(guid);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
 HRESULT WINAPI SetName(LPCWSTR name)override{const auto hr=source->real->SetName(name);if(FAILED(hr))return hr;return SetPrivateData(WKPDID_D3DDebugObjectNameW,name?UINT(wcslen(name)*sizeof(wchar_t)):0,name);}
 HRESULT WINAPI GetDevice(REFIID iid,void** out)override{return source->real->GetDevice(iid,out);}
 D3D12_GRAPHICS_PIPELINE_STATE_DESC original()const{auto d=source->desc;d.Flags=D3D12_PIPELINE_STATE_FLAGS(d.Flags&~D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS);d.RasterizerState.DepthBias=bias;d.RasterizerState.DepthBiasClamp=clamp;d.RasterizerState.SlopeScaledDepthBias=slope;return d;}
 HRESULT static_pipeline(ID3D12PipelineState** out){std::lock_guard lock(mutex);if(!staticState){ComPtr<ID3D12Device> dev;auto hr=source->real->GetDevice(IID_PPV_ARGS(&dev));if(FAILED(hr))return hr;auto d=original();hr=nativeFactory(dev.Get(),&d,IID_PPV_ARGS(&staticState));if(FAILED(hr))return hr;++staticFallbacks;}*out=staticState.Get();staticState->AddRef();return S_OK;}
 HRESULT WINAPI GetCachedBlob(ID3DBlob** out)override{if(!out)return E_POINTER;*out=nullptr;ComPtr<ID3D12PipelineState> p;auto hr=static_pipeline(&p);if(FAILED(hr))return hr;++blobFallbacks;return p->GetCachedBlob(out);}
};
Proxy* proxy(ID3D12PipelineState* p){return p&&*reinterpret_cast<void**>(p)==proxyVtable.load()?static_cast<Proxy*>(p):nullptr;}
struct Binding {Proxy* variant{};ID3D12PipelineState* real{};ComPtr<ID3D12GraphicsCommandList9> list;ComPtr<ID3D12PipelineState> fallback;};
Binding binding(ID3D12GraphicsCommandList* list,ID3D12PipelineState* p){Binding b;b.real=p;b.variant=proxy(p);if(!b.variant)return b;
 if(SUCCEEDED(list->QueryInterface(IID_PPV_ARGS(&b.list))))b.real=b.variant->source->real.Get();
 else if(SUCCEEDED(b.variant->static_pipeline(&b.fallback)))b.real=b.fallback.Get();
 else b.real=b.variant->source->real.Get();return b;
}
void apply(Binding& b){if(b.variant&&b.list){b.list->RSSetDepthBias(float(b.variant->bias),b.variant->clamp,b.variant->slope);++binds;}}
using SetPSO=void(WINAPI*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
using ResetList=HRESULT(WINAPI*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using CreateList=HRESULT(WINAPI*)(ID3D12Device*,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void**);
SetPSO setOriginal{},clearOriginal{};ResetList resetOriginal{};CreateList createOriginal{};
void WINAPI set_pso(ID3D12GraphicsCommandList* list,ID3D12PipelineState* p){auto b=binding(list,p);setOriginal(list,b.real);apply(b);}
void WINAPI clear_state(ID3D12GraphicsCommandList* list,ID3D12PipelineState* p){auto b=binding(list,p);clearOriginal(list,b.real);apply(b);}
HRESULT WINAPI reset_list(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* p){auto b=binding(list,p);auto hr=resetOriginal(list,allocator,b.real);if(SUCCEEDED(hr))apply(b);return hr;}
HRESULT WINAPI create_list(ID3D12Device* dev,UINT node,D3D12_COMMAND_LIST_TYPE type,ID3D12CommandAllocator* allocator,ID3D12PipelineState* p,REFIID iid,void** out){
 auto* v=proxy(p);const auto hr=createOriginal(dev,node,type,allocator,v?v->source->real.Get():p,iid,out);
 if(SUCCEEDED(hr)&&v&&out&&*out){ComPtr<ID3D12GraphicsCommandList9> list;if(SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&list)))){list->RSSetDepthBias(float(v->bias),v->clamp,v->slope);++binds;}}
 return hr;
}
}
bool install(ID3D12Device* dev,pipeline_cache::Factory factory){
 if(!dev||!factory)return false;
 if(enabled)return dev==owner;
 D3D12_FEATURE_DATA_D3D12_OPTIONS16 options{};
 if(FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16,&options,sizeof(options)))||!options.DynamicDepthBiasSupported)return false;
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12GraphicsCommandList9> v9;
 if(FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||FAILED(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||FAILED(list.As(&v9)))return false;list->Close();
 auto** v=*reinterpret_cast<void***>(list.Get());auto** d=*reinterpret_cast<void***>(dev);
 const void* targets[]={v[25],v[10],v[11],d[12]};void* replacements[]={reinterpret_cast<void*>(set_pso),reinterpret_cast<void*>(reset_list),reinterpret_cast<void*>(clear_state),reinterpret_cast<void*>(create_list)};
 void** originals[]={reinterpret_cast<void**>(&setOriginal),reinterpret_cast<void**>(&resetOriginal),reinterpret_cast<void**>(&clearOriginal),reinterpret_cast<void**>(&createOriginal)};
 unsigned installed{};for(;installed<4;++installed)if(MH_CreateHook(const_cast<void*>(targets[installed]),replacements[installed],originals[installed])!=MH_OK)break;
 if(installed!=4){for(unsigned i=0;i<installed;++i){MH_RemoveHook(const_cast<void*>(targets[i]));*originals[i]=nullptr;}return false;}
 for(unsigned i=0;i<4;++i)MH_QueueEnableHook(const_cast<void*>(targets[i]));if(MH_ApplyQueued()!=MH_OK){for(unsigned i=0;i<4;++i){MH_DisableHook(const_cast<void*>(targets[i]));MH_RemoveHook(const_cast<void*>(targets[i]));*originals[i]=nullptr;}return false;}
 nativeFactory=factory;owner=dev;enabled=true;return true;
}
HRESULT create(ID3D12Device* dev,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d,REFIID iid,void** out,pipeline_cache::Factory factory){
 if(!enabled||dev!=owner||!d||!out||iid!=__uuidof(ID3D12PipelineState)||d->CachedPSO.pCachedBlob||d->CachedPSO.CachedBlobSizeInBytes||(d->Flags&D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS)||!std::isfinite(d->RasterizerState.DepthBiasClamp)||!std::isfinite(d->RasterizerState.SlopeScaledDepthBias))return pipeline_cache::create(dev,d,iid,out,factory);
 auto dynamic=*d;dynamic.Flags=D3D12_PIPELINE_STATE_FLAGS(d->Flags|D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS);dynamic.RasterizerState.DepthBias=0;dynamic.RasterizerState.DepthBiasClamp=dynamic.RasterizerState.SlopeScaledDepthBias=0;
 ComPtr<ID3D12PipelineState> real;const auto hr=pipeline_cache::create(dev,&dynamic,iid,reinterpret_cast<void**>(real.GetAddressOf()),factory);if(FAILED(hr))return factory(dev,d,iid,out);
 try{*out=new Proxy(snapshot(dynamic,real.Get()),d->RasterizerState);++variants;return hr;}catch(...){return factory(dev,d,iid,out);}
}
Stats stats(){return {enabled.load(),variants.load(),binds.load(),staticFallbacks.load(),blobFallbacks.load(),liveVariants.load(),liveTemplates.load()};}
void clear_templates(){std::lock_guard lock(snapshotsMutex);snapshots.clear();}
bool fixture(ID3D12Device* dev,ID3D12CommandQueue* queue,pipeline_cache::Factory factory){
 if(!enabled){printf("Dynamic bias GPU: hardware/API unsupported, original path retained\n");return true;}
 const char* code="float4 vs(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),.45+.1*p.x,1);}float4 ps():SV_Target{return float4(0,1,0,1);}";
 ComPtr<ID3DBlob> vs,ps,serialized,error;ComPtr<ID3D12RootSignature> root;
 D3D12_ROOT_SIGNATURE_DESC signature{};
 if(FAILED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error))||FAILED(D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error))||FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error))||FAILED(dev->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
 D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=root.Get();d.VS={vs->GetBufferPointer(),vs->GetBufferSize()};d.PS={ps->GetBufferPointer(),ps->GetBufferSize()};d.SampleMask=~0u;
 d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;d.RasterizerState.DepthClipEnable=TRUE;
 for(auto& b:d.BlendState.RenderTarget){b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=15;}
 d.DepthStencilState.DepthEnable=TRUE;d.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
 d.DepthStencilState.FrontFace=d.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
 d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;d.NumRenderTargets=1;d.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;d.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;d.SampleDesc.Count=1;
 pipeline_cache::clear();clear_templates();auto before=pipeline_cache::stats();std::vector<ComPtr<ID3D12PipelineState>> held;
 for(int i=0;i<4096;++i){auto v=d;v.RasterizerState.DepthBias=i-2048;ComPtr<ID3D12PipelineState> p;if(FAILED(dev->CreateGraphicsPipelineState(&v,IID_PPV_ARGS(&p)))||!proxy(p.Get()))return false;held.push_back(p);}
 const auto after=pipeline_cache::stats();if(after.created-before.created!=1||after.hits-before.hits!=4095)return false;
 const GUID tag={0xe336e8e1,0x6e48,0x4d67,{0x8a,0x1f,0x12,0xe4,0x99,0x6a,0x72,0x1b}};
 UINT a=42,b=84,size=sizeof(UINT),got{};
 if(FAILED(held[0]->SetPrivateData(tag,sizeof(a),&a))||FAILED(held[1]->SetPrivateData(tag,sizeof(b),&b))||FAILED(held[0]->GetPrivateData(tag,&size,&got))||got!=a)return false;
 ComPtr<IUnknown> identity;if(FAILED(held[0].As(&identity))||identity.Get()!=static_cast<IUnknown*>(held[0].Get()))return false;
 ComPtr<ID3DBlob> cached;auto blobDesc=d;blobDesc.RasterizerState.DepthBias=-2048;
 if(FAILED(held[0]->GetCachedBlob(&cached)))return false;blobDesc.CachedPSO={cached->GetBufferPointer(),cached->GetBufferSize()};ComPtr<ID3D12PipelineState> fromBlob;
 if(FAILED(dev->CreateGraphicsPipelineState(&blobDesc,IID_PPV_ARGS(&fromBlob)))||proxy(fromBlob.Get()))return false;
 // Cache clearing must not invalidate caller-held variant/template ownership.
 pipeline_cache::clear();clear_templates();
 auto render=[&](ID3D12PipelineState* pso,DXGI_FORMAT format,bool reverse,unsigned path,std::vector<unsigned char>& result){
  ComPtr<ID3D12Resource> image,depth,readback;ComPtr<ID3D12DescriptorHeap> rtHeap,dsHeap;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=r.Height=16;r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;r.Format=DXGI_FORMAT_R8G8B8A8_UNORM;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if(FAILED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&image))))return false;
  auto z=r;z.Format=format;z.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;D3D12_CLEAR_VALUE dc{};dc.Format=format;dc.DepthStencil.Depth=.5f;
  if(FAILED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&z,D3D12_RESOURCE_STATE_DEPTH_WRITE,&dc,IID_PPV_ARGS(&depth))))return false;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};dev->GetCopyableFootprints(&r,0,1,0,&footprint,nullptr,nullptr,&bytes);
  hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
  if(FAILED(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)))||FAILED(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtHeap))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;if(FAILED(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsHeap)))||FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||FAILED(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),path==1?pso:nullptr,IID_PPV_ARGS(&list)))||FAILED(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return false;
  if(path==2){if(FAILED(list->Close())||FAILED(list->Reset(allocator.Get(),pso)))return false;}else if(path==3)list->ClearState(pso);else if(path==0)list->SetPipelineState(pso);else if(path==4){list->SetPipelineState(pso);if(proxy(pso)){ComPtr<ID3D12GraphicsCommandList9> v9;if(FAILED(list.As(&v9)))return false;v9->RSSetDepthBias(999999,0,3);}list->SetPipelineState(pso);}
  auto rt=rtHeap->GetCPUDescriptorHandleForHeapStart(),ds=dsHeap->GetCPUDescriptorHandleForHeapStart();dev->CreateRenderTargetView(image.Get(),nullptr,rt);dev->CreateDepthStencilView(depth.Get(),nullptr,ds);
  const float red[]={1,0,0,1};list->ClearRenderTargetView(rt,red,0,nullptr);list->ClearDepthStencilView(ds,D3D12_CLEAR_FLAG_DEPTH,.5f,0,0,nullptr);list->SetGraphicsRootSignature(root.Get());D3D12_VIEWPORT vp{0,0,16,16,0,1};D3D12_RECT rect{0,0,16,16};list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&rect);list->OMSetRenderTargets(1,&rt,FALSE,&ds);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
  D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={image.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
  D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=image.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION dest{};dest.pResource=readback.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dest.PlacedFootprint=footprint;list->CopyTextureRegion(&dest,0,0,0,&source,nullptr);if(FAILED(list->Close()))return false;
  ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;const bool complete=SUCCEEDED(queue->Signal(fence.Get(),1))&&SUCCEEDED(fence->SetEventOnCompletion(1,event))&&WaitForSingleObject(event,5000)==WAIT_OBJECT_0;CloseHandle(event);if(!complete)return false;
  void* pixels{};D3D12_RANGE range{0,size_t(bytes)};if(FAILED(readback->Map(0,&range,&pixels)))return false;result.resize(16*16*4);for(unsigned y=0;y<16;++y)memcpy(result.data()+y*16*4,static_cast<unsigned char*>(pixels)+footprint.Offset+y*footprint.Footprint.RowPitch,16*4);D3D12_RANGE empty{};readback->Unmap(0,&empty);return true;
 };
 struct Bias{INT offset;float clamp,slope;};const Bias cases[]={{-1048576,0,0},{0,0,0},{1048576,0,0},{-1048576,.005f,0},{1048576,.005f,0},{0,0,-2},{0,0,2},{12000,.02f,1.25f},{-12345,-.02f,-1.25f},{INT_MIN,0,0},{INT_MAX,0,0},{16777217,0,0},{-16777217,0,0}};
 unsigned compared{};std::vector<unsigned char> firstReference;bool distinct{};
 for(auto format:{DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_D32_FLOAT})for(bool reverse:{false,true})for(const auto& bias:cases){auto v=d;v.DSVFormat=format;v.DepthStencilState.DepthFunc=reverse?D3D12_COMPARISON_FUNC_GREATER:D3D12_COMPARISON_FUNC_LESS;v.RasterizerState.DepthBias=bias.offset;v.RasterizerState.DepthBiasClamp=bias.clamp;v.RasterizerState.SlopeScaledDepthBias=bias.slope;
  ComPtr<ID3D12PipelineState> staticPSO,dynamicPSO;if(FAILED(factory(dev,&v,IID_PPV_ARGS(&staticPSO)))||FAILED(dev->CreateGraphicsPipelineState(&v,IID_PPV_ARGS(&dynamicPSO)))||!proxy(dynamicPSO.Get()))return false;
  for(unsigned path=0;path<5;++path){std::vector<unsigned char> reference,actual;if(!render(staticPSO.Get(),format,reverse,path,reference)||!render(dynamicPSO.Get(),format,reverse,path,actual)||reference!=actual){printf("Dynamic bias mismatch format=%u reverse=%u bias=%d clamp=%g slope=%g path=%u\n",format,reverse,bias.offset,bias.clamp,bias.slope,path);return false;}if(firstReference.empty())firstReference=reference;else distinct=distinct||reference!=firstReference;++compared;}
 }
 if(!distinct)return false;
 std::vector<unsigned char> retained;if(!render(held[0].Get(),d.DSVFormat,false,0,retained))return false;
 printf("Dynamic bias GPU: 4096 distinct biases share ONE native PSO; exact original static pixels for %u D24/D32 forward/reverse bias/clamp/slope including INT extremes x Set/Create/Reset/Clear/rebind; per-variant COM/private data, original cached blob and caller lifetime after clear PASS\n",compared);
 pipeline_cache::clear();clear_templates();return true;
}
}
