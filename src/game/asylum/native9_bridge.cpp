#include "native9_bridge.h"
#include "managed9.h"
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::native9 {
namespace {
ComPtr<IDirect3DDevice9Ex> nine;
ComPtr<ID3D11Device> eleven;
ComPtr<ID3D11DeviceContext> ctx;
ComPtr<IDirect3DPixelShader9> pack;
ComPtr<ID3D11VertexShader> decodeVs;
ComPtr<ID3D11PixelShader> decodePs;
ComPtr<ID3D11Texture2D> decoded;
ComPtr<ID3D11RenderTargetView> decodedRt;
ComPtr<ID3D11ShaderResourceView> decodedSrv;
struct Slot {
 ComPtr<IDirect3DTexture9> texture;
 ComPtr<IDirect3DSurface9> surface;
 ComPtr<ID3D11Texture2D> imported;
 ComPtr<ID3D11ShaderResourceView> view;
 ComPtr<IDirect3DQuery9> producer;
 ComPtr<ID3D11Query> consumer;
 UINT w{},h{};bool reading{},pending{};
};
std::array<Slot,7> slots; // Slot4: transition coverage; slot3 remains physical depth.
bool wait11(Slot& s){
 if(!s.pending)return true;ctx->Flush();const auto limit=GetTickCount64()+3000;HRESULT h;
 while((h=ctx->GetData(s.consumer.Get(),nullptr,0,0))==S_FALSE&&GetTickCount64()<limit)SwitchToThread();
 if(h!=S_OK)return false;s.pending=false;return true;
}
bool prepare(Slot& s,UINT w,UINT h){
 if(!wait11(s))return false;if(s.texture&&s.w==w&&s.h==h)return true;
 s={};HANDLE handle{};
 if(FAILED(nine->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&s.texture,&handle))||!handle||
    FAILED(s.texture->GetSurfaceLevel(0,&s.surface))||FAILED(eleven->OpenSharedResource(handle,IID_PPV_ARGS(&s.imported)))||
    FAILED(eleven->CreateShaderResourceView(s.imported.Get(),nullptr,&s.view))||FAILED(nine->CreateQuery(D3DQUERYTYPE_EVENT,&s.producer)))return false;
 D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};if(FAILED(eleven->CreateQuery(&q,&s.consumer)))return false;s.w=w;s.h=h;return true;
}
bool produced(Slot& s){
 if(FAILED(s.producer->Issue(D3DISSUE_END)))return false;
 const auto limit=GetTickCount64()+3000;HRESULT h;
 while((h=s.producer->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE&&GetTickCount64()<limit)SwitchToThread();
 if(h!=S_OK)return false;s.reading=true;return true;
}
bool shaders(){
 if(pack&&decodeVs&&decodePs)return true;
 const char* p9="sampler2D raw:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{float q=min(16777215.0,floor(saturate(tex2D(raw,uv).r)*16777215.0+0.5));float r=floor(q/65536.0);q-=r*65536.0;float g=floor(q/256.0);float b=q-g*256.0;return float4(r,g,b,255)/255.0;}";
 const char* vs="struct O{float4 p:SV_Position;};O main(uint i:SV_VertexID){O o;o.p=float4(i==2?3:-1,i==1?3:-1,0,1);return o;}";
 const char* ps="Texture2D<float4> t:register(t0);float main(float4 p:SV_Position):SV_Target{float3 c=round(t.Load(int3(p.xy,0)).rgb*255);return dot(c,float3(65536,256,1))/16777215.0;}";
 ComPtr<ID3DBlob>b,error;
 if(FAILED(D3DCompile(p9,strlen(p9),nullptr,nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&error))||FAILED(nine->CreatePixelShader(static_cast<const DWORD*>(b->GetBufferPointer()),&pack)))return false;
 b.Reset();error.Reset();if(FAILED(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&error))||FAILED(eleven->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&decodeVs)))return false;
 b.Reset();error.Reset();return SUCCEEDED(D3DCompile(ps,strlen(ps),nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&error))&&SUCCEEDED(eleven->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&decodePs));
}
}
bool adopt(IDirect3DDevice9* d){
 if(nine)return true;auto* real=managed9::native_device(d);if(!real)return false;
 D3DDEVICE_CREATION_PARAMETERS cp{};ComPtr<IDirect3D9> base;ComPtr<IDirect3D9Ex> factory;
 LUID luid{};if(FAILED(real->GetCreationParameters(&cp))||FAILED(real->GetDirect3D(&base))||FAILED(base.As(&factory))||FAILED(factory->GetAdapterLUID(cp.AdapterOrdinal,&luid)))return false;
 ComPtr<IDXGIFactory1> f;ComPtr<IDXGIAdapter1>a;
 if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))return false;
 for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> candidate;const auto h=f->EnumAdapters1(i,&candidate);if(h==DXGI_ERROR_NOT_FOUND)break;if(FAILED(h))return false;DXGI_ADAPTER_DESC1 desc{};candidate->GetDesc1(&desc);if(desc.AdapterLuid.LowPart==luid.LowPart&&desc.AdapterLuid.HighPart==luid.HighPart){a=candidate;break;}}
 if(!a||FAILED(D3D11CreateDevice(a.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&eleven,nullptr,&ctx)))return false;
 nine=real;return true;
}
ID3D11Device* device(){return eleven.Get();}ID3D11DeviceContext* context(){return ctx.Get();}
ID3D11Texture2D* colour(IDirect3DSurface9* source,unsigned index){
 if(!nine||!source||index>=slots.size()||index==3)return nullptr;D3DSURFACE_DESC d{};source->GetDesc(&d);auto& s=slots[index];
 if(!prepare(s,d.Width,d.Height)||FAILED(nine->StretchRect(managed9::native_surface(source),nullptr,s.surface.Get(),nullptr,D3DTEXF_NONE))||!produced(s))return nullptr;
 return s.imported.Get();
}
bool depth(IDirect3DSurface9* source,ID3D11Texture2D** image,ID3D11ShaderResourceView** srv){
 if(!image||!srv||!nine||!source)return false;*image=nullptr;*srv=nullptr;auto* input=managed9::depth_texture(source);if(!input||!shaders())return false;
 D3DSURFACE_DESC d{};source->GetDesc(&d);auto& s=slots[3];if(!prepare(s,d.Width,d.Height))return false;
 ComPtr<IDirect3DStateBlock9> state;ComPtr<IDirect3DSurface9> rt,ds;std::array<ComPtr<IDirect3DSurface9>,3> mrt;D3DVIEWPORT9 viewport{};
 if(FAILED(nine->CreateStateBlock(D3DSBT_ALL,&state))||FAILED(nine->GetRenderTarget(0,&rt))||FAILED(nine->GetViewport(&viewport)))return false;nine->GetDepthStencilSurface(&ds);
 for(unsigned i=1;i<4;++i){nine->GetRenderTarget(i,&mrt[i-1]);nine->SetRenderTarget(i,nullptr);}
 bool ok=SUCCEEDED(nine->SetDepthStencilSurface(nullptr))&&SUCCEEDED(nine->SetRenderTarget(0,s.surface.Get()));
 D3DVIEWPORT9 vp{0,0,d.Width,d.Height,0,1};nine->SetViewport(&vp);
 nine->SetVertexShader(nullptr);nine->SetPixelShader(pack.Get());nine->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);nine->SetTexture(0,input);
 nine->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);nine->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);nine->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);nine->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);nine->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);nine->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,0);
 nine->SetRenderState(D3DRS_ZENABLE,FALSE);nine->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);nine->SetRenderState(D3DRS_STENCILENABLE,FALSE);nine->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);nine->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);nine->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);nine->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);nine->SetRenderState(D3DRS_COLORWRITEENABLE,15);nine->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);nine->SetRenderState(D3DRS_FOGENABLE,FALSE);nine->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
 struct V{float x,y,z,w,u,v;};const V v[]={{-.5f,-.5f,0,1,0,0},{float(d.Width)-.5f,-.5f,0,1,1,0},{-.5f,float(d.Height)-.5f,0,1,0,1},{float(d.Width)-.5f,float(d.Height)-.5f,0,1,1,1}};
 ok=ok&&SUCCEEDED(nine->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)));
 nine->SetTexture(0,nullptr);state->Apply();nine->SetRenderTarget(0,rt.Get());for(unsigned i=1;i<4;++i)nine->SetRenderTarget(i,mrt[i-1].Get());nine->SetDepthStencilSurface(ds.Get());nine->SetViewport(&viewport);
 if(!ok||!produced(s))return false;
 D3D11_TEXTURE2D_DESC old{};if(decoded)decoded->GetDesc(&old);
 if(!decoded||old.Width!=d.Width||old.Height!=d.Height){decodedSrv.Reset();decodedRt.Reset();decoded.Reset();D3D11_TEXTURE2D_DESC td{};td.Width=d.Width;td.Height=d.Height;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;if(FAILED(eleven->CreateTexture2D(&td,nullptr,&decoded))||FAILED(eleven->CreateRenderTargetView(decoded.Get(),nullptr,&decodedRt))||FAILED(eleven->CreateShaderResourceView(decoded.Get(),nullptr,&decodedSrv)))return false;}
 // This context belongs to the bridge. Establish every stage/state explicitly.
 ID3D11RenderTargetView* target=decodedRt.Get();ctx->OMSetRenderTargets(1,&target,nullptr);ctx->OMSetBlendState(nullptr,nullptr,~0u);ctx->OMSetDepthStencilState(nullptr,0);ctx->RSSetState(nullptr);D3D11_VIEWPORT screen{0,0,float(d.Width),float(d.Height),0,1};ctx->RSSetViewports(1,&screen);
 ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(decodeVs.Get(),nullptr,0);ctx->PSSetShader(decodePs.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);
 ID3D11ShaderResourceView* view=s.view.Get();ctx->PSSetShaderResources(0,1,&view);ctx->Draw(3,0);view=nullptr;ctx->PSSetShaderResources(0,1,&view);ctx->OMSetRenderTargets(0,nullptr,nullptr);
 ctx->End(s.consumer.Get());s.pending=true;s.reading=false;*image=decoded.Get();(*image)->AddRef();*srv=decodedSrv.Get();(*srv)->AddRef();return true;
}
bool finish_reads(){if(!ctx)return false;for(auto& s:slots)if(s.reading){ctx->End(s.consumer.Get());s.pending=true;s.reading=false;}ctx->Flush();return true;}
bool drain(){if(!ctx)return true;finish_reads();for(auto&s:slots)if(!wait11(s))return false;return true;}
void reset(){drain();for(auto&s:slots)s={};decodedSrv.Reset();decodedRt.Reset();decoded.Reset();pack.Reset();decodeVs.Reset();decodePs.Reset();}
void shutdown(){reset();if(ctx){ctx->ClearState();ctx->Flush();}ctx.Reset();eleven.Reset();nine.Reset();}
}
