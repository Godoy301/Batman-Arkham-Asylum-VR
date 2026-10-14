#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdio>
#include <initializer_list>
#include <vector>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include "game/asylum/managed9.h"
#include "game/asylum/native9_bridge.h"
using Microsoft::WRL::ComPtr;
bool finish9(IDirect3DDevice9* d){ComPtr<IDirect3DQuery9> q;if(FAILED(d->CreateQuery(D3DQUERYTYPE_EVENT,&q))||FAILED(q->Issue(D3DISSUE_END)))return false;const auto until=GetTickCount64()+3000;HRESULT h;while((h=q->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE&&GetTickCount64()<until)SwitchToThread();return h==S_OK;}
bool pixels(ID3D11Texture2D* image,unsigned color){using namespace bvr::aa;auto* dev=native9::device();auto* ctx=native9::context();D3D11_TEXTURE2D_DESC d{};image->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=d.MiscFlags=0;ComPtr<ID3D11Texture2D> read;if(FAILED(dev->CreateTexture2D(&d,nullptr,&read)))return false;ctx->CopyResource(read.Get(),image);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return false;bool ok=true;for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x){unsigned v{};memcpy(&v,static_cast<const BYTE*>(m.pData)+y*m.RowPitch+x*4,4);ok=ok&&v==color;}ctx->Unmap(read.Get(),0);return native9::finish_reads()&&ok;}
bool fill(IDirect3DTexture9* t,unsigned color){for(unsigned l=0;l<t->GetLevelCount();++l){D3DSURFACE_DESC d{};D3DLOCKED_RECT m{};if(FAILED(t->GetLevelDesc(l,&d))||FAILED(t->LockRect(l,&m,nullptr,0)))return false;for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x)memcpy(static_cast<BYTE*>(m.pBits)+y*m.Pitch+x*4,&color,4);if(FAILED(t->UnlockRect(l)))return false;}return true;}
int managed_check(IDirect3DDevice9Ex* ex,HWND window){
 using namespace bvr::aa;ComPtr<IDirect3DDevice9>d;d.Attach(managed9::wrap(ex));if(!d||!native9::adopt(d.Get()))return 30;
 ComPtr<IDirect3DTexture9> tex;ComPtr<IDirect3DVertexBuffer9> vb;ComPtr<IDirect3DIndexBuffer9> ib;
 if(FAILED(d->CreateTexture(16,16,3,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex,nullptr))||FAILED(d->CreateVertexBuffer(4*24,0,D3DFVF_XYZRHW|D3DFVF_TEX1,D3DPOOL_MANAGED,&vb,nullptr))||FAILED(d->CreateIndexBuffer(6*2,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr)))return 31;
 D3DSURFACE_DESC desc{};D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};tex->GetLevelDesc(0,&desc);vb->GetDesc(&vd);ib->GetDesc(&id);if(desc.Pool!=D3DPOOL_MANAGED||vd.Pool!=D3DPOOL_MANAGED||id.Pool!=D3DPOOL_MANAGED)return 32;
 ComPtr<IDirect3DDevice9> logical;tex->GetDevice(&logical);if(logical.Get()!=d.Get())return 33;logical.Reset();
 struct V{float x,y,z,w,u,v;};V v[]={{-.5f,-.5f,.5f,1,0,0},{63.5f,-.5f,.5f,1,1,0},{-.5f,63.5f,.5f,1,0,1},{63.5f,63.5f,.5f,1,1,1}};const WORD indexes[]={0,1,2,2,1,3};void* p{};
 if(FAILED(vb->Lock(0,0,&p,0)))return 34;memcpy(p,v,sizeof(v));vb->Unlock();if(FAILED(ib->Lock(0,0,&p,0)))return 35;memcpy(p,indexes,sizeof(indexes));ib->Unlock();
 d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetStreamSource(0,vb.Get(),0,sizeof(V));d->SetIndices(ib.Get());
 auto draw=[&](unsigned color){if(FAILED(d->BeginScene())||FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2))||FAILED(d->EndScene()))return false;ComPtr<IDirect3DSurface9>b;if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&b)))return false;auto* shared=native9::colour(b.Get(),0);return shared&&pixels(shared,color);};
 if(!fill(tex.Get(),0xff2989c1)||FAILED(d->SetTexture(0,tex.Get()))||!draw(0xff2989c1))return 36;
 ComPtr<IDirect3DBaseTexture9> get;d->GetTexture(0,&get);if(get.Get()!=tex.Get())return 37;get.Reset();
 if(!fill(tex.Get(),0xffdb3953)||!draw(0xffdb3953))return 38; // Update while already bound.
 ComPtr<IDirect3DSurface9> child,childAgain;tex->GetSurfaceLevel(0,&child);tex->GetSurfaceLevel(0,&childAgain);if(childAgain.Get()!=child.Get())return 39;childAgain.Reset();ComPtr<IDirect3DTexture9> container;child->GetContainer(IID_PPV_ARGS(&container));if(container.Get()!=tex.Get())return 39;container.Reset();
 D3DLOCKED_RECT lock{};child->LockRect(&lock,nullptr,0);const unsigned blue=0xff3146d2;for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)memcpy(static_cast<BYTE*>(lock.pBits)+y*lock.Pitch+x*4,&blue,4);child->UnlockRect();child.Reset();if(!draw(blue))return 40;
 ComPtr<IDirect3DStateBlock9> saved;if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&saved))||FAILED(d->SetTexture(0,nullptr))||FAILED(saved->Apply())||!draw(blue))return 41;
 ComPtr<IDirect3DStateBlock9> recorded;if(FAILED(d->BeginStateBlock())||FAILED(d->SetTexture(0,tex.Get()))||FAILED(d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,1))||FAILED(d->EndStateBlock(&recorded)))return 55;DWORD level{};d->GetSamplerState(0,D3DSAMP_MAXMIPLEVEL,&level);if(level)return 56;if(FAILED(recorded->Apply()))return 57;d->GetSamplerState(0,D3DSAMP_MAXMIPLEVEL,&level);if(level!=1)return 58;d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0);recorded.Reset();
 // Partial copy must respect row width, preserving pixels outside the rect.
 ComPtr<IDirect3DSurface9> patch,managedSurface;if(FAILED(d->CreateOffscreenPlainSurface(4,4,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&patch,nullptr))||FAILED(tex->GetSurfaceLevel(0,&managedSurface)))return 59;
 D3DLOCKED_RECT patchLock{};patch->LockRect(&patchLock,nullptr,0);const unsigned patchColor=0xff775544;for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)memcpy(static_cast<BYTE*>(patchLock.pBits)+y*patchLock.Pitch+x*4,&patchColor,4);patch->UnlockRect();const POINT patchAt{3,5};if(FAILED(d->UpdateSurface(patch.Get(),nullptr,managedSurface.Get(),&patchAt)))return 60;
 managedSurface->LockRect(&patchLock,nullptr,D3DLOCK_READONLY);for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){unsigned pixel{};memcpy(&pixel,static_cast<BYTE*>(patchLock.pBits)+y*patchLock.Pitch+x*4,4);if(pixel!=(x>=3&&x<7&&y>=5&&y<9?patchColor:blue))return 61;}managedSurface->UnlockRect();managedSurface.Reset();patch.Reset();if(!fill(tex.Get(),blue)||!draw(blue))return 62;
 D3DLOCKED_RECT lodLock{};if(FAILED(tex->LockRect(1,&lodLock,nullptr,0)))return 42;const unsigned mip=0xff934815;for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)memcpy(static_cast<BYTE*>(lodLock.pBits)+y*lodLock.Pitch+x*4,&mip,4);tex->UnlockRect(1);tex->SetLOD(1);if(!draw(mip))return 43;tex->SetLOD(0);
 // Cube/volume uploads are sampled by actual native9 shaders.
 ComPtr<IDirect3DCubeTexture9> cube;if(FAILED(d->CreateCubeTexture(8,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube,nullptr)))return 44;
 for(unsigned face=0;face<6;++face)for(unsigned l=0;l<cube->GetLevelCount();++l){D3DSURFACE_DESC c{};cube->GetLevelDesc(l,&c);cube->LockRect(D3DCUBEMAP_FACES(face),l,&lock,nullptr,0);for(unsigned y=0;y<c.Height;++y)for(unsigned x=0;x<c.Width;++x)memcpy(static_cast<BYTE*>(lock.pBits)+y*lock.Pitch+x*4,&blue,4);cube->UnlockRect(D3DCUBEMAP_FACES(face),l);}
 auto sample=[&](IDirect3DBaseTexture9* t,const char* source,unsigned color){ComPtr<ID3DBlob> b,e;ComPtr<IDirect3DPixelShader9> ps;if(FAILED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","ps_3_0",0,0,&b,&e))||FAILED(d->CreatePixelShader(static_cast<const DWORD*>(b->GetBufferPointer()),&ps))||FAILED(d->SetTexture(0,t))||FAILED(d->SetPixelShader(ps.Get())))return false;const bool result=draw(color);d->SetPixelShader(nullptr);return result;};
 if(!sample(cube.Get(),"samplerCUBE s:register(s0);float4 main():COLOR0{return texCUBE(s,float3(1,0,0));}",blue))return 45;
 ComPtr<IDirect3DVolumeTexture9> volume;if(FAILED(d->CreateVolumeTexture(4,4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&volume,nullptr)))return 46;D3DLOCKED_BOX box{};volume->LockBox(0,&box,nullptr,0);for(unsigned z=0;z<4;++z)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)memcpy(static_cast<BYTE*>(box.pBits)+z*box.SlicePitch+y*box.RowPitch+x*4,&mip,4);volume->UnlockBox(0);if(!sample(volume.Get(),"sampler3D s:register(s0);float4 main():COLOR0{return tex3D(s,float3(.5,.5,.5));}",mip))return 47;
 // INTZ depth includes actual two-depth geometry, a later clear, and far-Z
 // samples. Transport must retain 24-bit precision, not half-float rounding.
 ComPtr<IDirect3DSurface9> depth;if(FAILED(d->CreateDepthStencilSurface(64,64,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr))||!managed9::depth_texture(depth.Get()))return 48;d->SetDepthStencilSurface(depth.Get());
 auto zread=[&](float expected){ComPtr<ID3D11Texture2D> image,read;ComPtr<ID3D11ShaderResourceView> view;if(!native9::depth(depth.Get(),&image,&view))return false;D3D11_TEXTURE2D_DESC td{};image->GetDesc(&td);td.BindFlags=td.MiscFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(native9::device()->CreateTexture2D(&td,nullptr,&read)))return false;native9::context()->CopyResource(read.Get(),image.Get());D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(native9::context()->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return false;bool ok=true;float lo=1,hi=0;for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){float z;memcpy(&z,static_cast<const BYTE*>(m.pData)+y*m.RowPitch+x*4,4);lo=std::min(lo,z);hi=std::max(hi,z);ok=ok&&std::fabs(z-expected)<=2.0f/16777215;}native9::context()->Unmap(read.Get(),0);if(!ok)printf("depth mismatch expected %.9f got %.9f..%.9f\n",expected,lo,hi);return ok;};
 if(FAILED(d->BeginScene()))return 49;for(float z:{0.f,.25f,.75f,.9999982f,.99999988f,1.f}){d->Clear(0,nullptr,D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,z,7);if(!zread(z)){d->EndScene();return 50;}}d->EndScene();
 saved.Reset();d->SetTexture(0,nullptr);d->SetDepthStencilSurface(nullptr);depth.Reset();native9::reset();
 D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 if(FAILED(d->Reset(&pp)))return 51;
 if(FAILED(vb->Lock(0,0,&p,D3DLOCK_READONLY)))return 52;const bool preserved=memcmp(p,v,sizeof(v))==0;vb->Unlock();if(!preserved||!fill(tex.Get(),blue))return 53;
 d->SetTexture(0,tex.Get());d->SetStreamSource(0,vb.Get(),0,sizeof(V));d->SetIndices(ib.Get());d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);if(!draw(blue))return 54;
 native9::reset();pp.BackBufferWidth=2836;pp.BackBufferHeight=3040;if(FAILED(d->Reset(&pp)))return 63;
 for(unsigned eye=0;eye<2;++eye){ComPtr<IDirect3DSurface9> large;if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&large)))return 64;const DWORD color=eye?0xffd13b57:0xff2989c1;if(FAILED(d->ColorFill(large.Get(),nullptr,color)))return 65;auto* shared=native9::colour(large.Get(),eye);if(!shared||!pixels(shared,color))return 66;}
 native9::shutdown();
 puts("PASS managed native9Ex: real textured/indexed draws, bound dirty updates, child/container identity, recorded state blocks, partial-copy boundaries, LOD, cube/volume shader pixels, reset+CPU contents, full-precision INTZ depth including far-Z; BOTH actual2836x3040 eye transfers checked at every pixel.");return 0;
}
int main(){
 wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);wcscat_s(system,L"\\d3d9.dll");HMODULE dll=LoadLibraryW(system);if(!dll)return 1;
 auto plain=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(dll,"Direct3DCreate9"));auto extended=reinterpret_cast<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>(GetProcAddress(dll,"Direct3DCreate9Ex"));if(!plain||!extended)return 2;
 HWND window=CreateWindowExW(0,L"STATIC",L"Native9 bridge harness",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)return 3;
 ComPtr<IDirect3D9> d9;d9.Attach(plain(D3D_SDK_VERSION));ComPtr<IDirect3D9Ex> ex;if(FAILED(extended(D3D_SDK_VERSION,&ex)))return 4;
 D3DPRESENT_PARAMETERS p{};p.BackBufferWidth=p.BackBufferHeight=64;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.BackBufferCount=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;p.Windowed=TRUE;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 ComPtr<IDirect3DDevice9> classic,exBase;ComPtr<IDirect3DDevice9Ex> fullEx;
 const DWORD flags=D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE;
 if(FAILED(d9->CreateDevice(0,D3DDEVTYPE_HAL,window,flags,&p,&classic))||FAILED(ex->CreateDevice(0,D3DDEVTYPE_HAL,window,flags,&p,&exBase))||FAILED(ex->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,flags,&p,nullptr,&fullEx)))return 5;
 LUID luid{};if(FAILED(ex->GetAdapterLUID(0,&luid)))return 6;
 ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> chosen;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 7;
 for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.AdapterLuid.LowPart==luid.LowPart&&desc.AdapterLuid.HighPart==luid.HighPart){chosen=a;printf("Matched native9/DX11 adapter: %ls\n",desc.Description);break;}}
 ComPtr<ID3D11Device> eleven;ComPtr<ID3D11DeviceContext> context;if(!chosen||FAILED(D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&eleven,nullptr,&context)))return 8;
 HANDLE shared{};ComPtr<IDirect3DTexture9> source;if(FAILED(fullEx->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&source,&shared))||!shared)return 9;
 IDirect3DDevice9* devices[]={classic.Get(),exBase.Get(),fullEx.Get()};const char* names[]={"classic9","Exfactory-baseCreateDevice","full9Ex"};
 for(unsigned i=0;i<3;++i){ComPtr<IDirect3DTexture9> managed,created,opened;HANDLE handle{};const auto m=devices[i]->CreateTexture(16,16,2,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&managed,nullptr);const auto c=devices[i]->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&created,&handle);handle=shared;const auto o=devices[i]->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&opened,&handle);ComPtr<IDirect3DDevice9Ex> q;const auto qi=devices[i]->QueryInterface(IID_PPV_ARGS(&q));printf("Native device %s managed=%08lx sharedCreate=%08lx sharedOpenExisting=%08lx QIEx=%08lx\n",names[i],m,c,o,qi);}
 ComPtr<ID3D11Texture2D> imported;if(FAILED(eleven->OpenSharedResource(shared,IID_PPV_ARGS(&imported))))return 10;
 D3D11_TEXTURE2D_DESC desc{};imported->GetDesc(&desc);desc.BindFlags=0;desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;if(FAILED(eleven->CreateTexture2D(&desc,nullptr,&read)))return 11;
 ComPtr<IDirect3DSurface9> surface;if(FAILED(source->GetSurfaceLevel(0,&surface)))return 12;
 for(unsigned frame=0;frame<4;++frame){const unsigned r=frame&1?219:41,g=frame&1?57:137,b=frame&1?83:193;if(FAILED(fullEx->ColorFill(surface.Get(),nullptr,D3DCOLOR_ARGB(255,r,g,b)))||!finish9(fullEx.Get()))return 13;
  context->CopyResource(read.Get(),imported.Get());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context->Map(read.Get(),0,D3D11_MAP_READ,0,&map)))return 14;bool exact=true;
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){const auto* bytes=static_cast<const unsigned char*>(map.pData)+y*map.RowPitch+x*4;exact=exact&&bytes[0]==b&&bytes[1]==g&&bytes[2]==r&&bytes[3]==255;}context->Unmap(read.Get(),0);if(!exact)return 15;
 }
 for(const auto f:{MAKEFOURCC('I','N','T','Z'),MAKEFOURCC('R','E','S','Z')}){auto h=ex->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,f==MAKEFOURCC('I','N','T','Z')?D3DUSAGE_DEPTHSTENCIL:D3DUSAGE_RENDERTARGET,f==MAKEFOURCC('I','N','T','Z')?D3DRTYPE_TEXTURE:D3DRTYPE_SURFACE,D3DFORMAT(f));printf("Native depth capability %c%c%c%c = %08lx\n",char(f),char(f>>8),char(f>>16),char(f>>24),h);}
 const auto compatibility=managed_check(fullEx.Get(),window);if(compatibility){printf("Managed/native bridge check failed: %d\n",compatibility);return compatibility;}
 if(GetModuleHandleW(L"d3d9on12.dll")){puts("FAIL: native harness loaded9On12");return 16;}
 puts("PASS: actual native9Ex -> DX11 shared GPU images, four alternating color frames/all pixels, producer completion/consumer readback; no9On12 loaded. Device compatibility results above are observations.");
 return 0;
}
