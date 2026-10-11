#include "d3d9_bridge.h"
#include "core/hooks/d3d11_hook.h"
#include "core/gfx/hud_capture.h"
#include "core/gfx/blit.h"
#include "core/ui/overlay.h"
#include "core/util/log.h"
#include "core/vr/openxr_runtime.h"
#include <d3d9on12.h>
#include <d3d11on12.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <imgui.h>
#include <atomic>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;
extern "C" DWORD WINAPI AA_Identity();
namespace bvr::aa::bridge {
namespace {
std::atomic<bool> ready{false};
std::atomic<unsigned> frames{0}, failures{0};
HMODULE system9=nullptr;
IDirect3DDevice9* ownDevice=nullptr;
using Present=HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using Reset=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
Present originalPresent=nullptr;Reset originalReset=nullptr;
ComPtr<IDirect3DDevice9On12> nine;
ComPtr<ID3D12Device> twelve;
ComPtr<ID3D12CommandQueue> queue;
ComPtr<ID3D12Fence> fence;
ComPtr<ID3D11Device> eleven;
ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11On12Device> elevenOn12;
UINT64 serial=0;
HWND gameWindow=nullptr;
bool dead=false;

// The frozen core needs a device, owned backbuffer and window descriptor.
// This facade has no presentation or resizing side effects; D3D9 owns those.
class Frame final : public IDXGISwapChain {
    std::atomic<ULONG> refs{1};
public:
    ComPtr<ID3D11Texture2D> image;
    DXGI_SWAP_CHAIN_DESC desc{};
    Frame(ID3D11Texture2D* tex,HWND window):image(tex) {
        D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);
        desc.BufferDesc.Width=d.Width;desc.BufferDesc.Height=d.Height;desc.BufferDesc.Format=d.Format;
        desc.SampleDesc={1,0};desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=1;desc.OutputWindow=window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    }
    HRESULT WINAPI QueryInterface(REFIID iid,void** p) override {
        if(!p) return E_POINTER;*p=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IDXGIObject)||iid==__uuidof(IDXGIDeviceSubObject)||iid==__uuidof(IDXGISwapChain)) {
            *p=static_cast<IDXGISwapChain*>(this);AddRef();return S_OK;
        }return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef() override {return ++refs;}
    ULONG WINAPI Release() override {const ULONG n=--refs;if(!n) delete this;return n;}
    HRESULT WINAPI SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT WINAPI SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT WINAPI GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT WINAPI GetParent(REFIID,void**) override {return E_NOINTERFACE;}
    HRESULT WINAPI GetDevice(REFIID iid,void** p) override {ComPtr<ID3D11Device> d;image->GetDevice(&d);return d->QueryInterface(iid,p);}
    HRESULT WINAPI Present(UINT,UINT) override {return E_NOTIMPL;}
    HRESULT WINAPI GetBuffer(UINT index,REFIID iid,void** p) override {return index ? DXGI_ERROR_INVALID_CALL:image->QueryInterface(iid,p);}
    HRESULT WINAPI SetFullscreenState(BOOL,IDXGIOutput*) override {return E_NOTIMPL;}
    HRESULT WINAPI GetFullscreenState(BOOL* b,IDXGIOutput** out) override {if(b)*b=FALSE;if(out)*out=nullptr;return S_OK;}
    HRESULT WINAPI GetDesc(DXGI_SWAP_CHAIN_DESC* d) override {if(!d)return E_POINTER;*d=desc;return S_OK;}
    HRESULT WINAPI ResizeBuffers(UINT,UINT,UINT,DXGI_FORMAT,UINT) override {return E_NOTIMPL;}
    HRESULT WINAPI ResizeTarget(const DXGI_MODE_DESC*) override {return E_NOTIMPL;}
    HRESULT WINAPI GetContainingOutput(IDXGIOutput**) override {return E_NOTIMPL;}
    HRESULT WINAPI GetFrameStatistics(DXGI_FRAME_STATISTICS*) override {return E_NOTIMPL;}
    HRESULT WINAPI GetLastPresentCount(UINT* p) override {if(p)*p=frames.load();return p ? S_OK:E_POINTER;}
};
Frame* frame=nullptr;
DXGI_FORMAT sourceFormat=DXGI_FORMAT_UNKNOWN;
ComPtr<ID3D11Texture2D> colourSource;
ComPtr<ID3D11ShaderResourceView> colourSrv;
ComPtr<ID3D11RenderTargetView> frameRtv;

bool checked(HRESULT hr,const char* stage) {
    if(FAILED(hr)) {
        if(++failures<=8) BVR_LOG("[aa bridge] %s failed: 0x%08lx",stage,hr);
        return false;
    }return true;
}
HMODULE load_system9() {
    if(!system9) {wchar_t path[MAX_PATH]{};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\d3d9.dll");system9=LoadLibraryW(path);}
    return system9;
}
bool drain() {
    if(!queue || !fence) return true;
    context->Flush();
    const UINT64 value=++serial;
    if(!checked(queue->Signal(fence.Get(),value),"drain signal")) return false;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event) return false;
    const bool done=checked(fence->SetEventOnCompletion(value,event),"drain fence") && WaitForSingleObject(event,5000)==WAIT_OBJECT_0;
    CloseHandle(event);return done;
}
void release_frame() {
    frameRtv.Reset();colourSrv.Reset();colourSource.Reset();sourceFormat=DXGI_FORMAT_UNKNOWN;
    if(frame) {frame->Release();frame=nullptr;}
}
bool adopt(IDirect3DDevice9* dev) {
    if(nine) return elevenOn12!=nullptr;
    if(!checked(dev->QueryInterface(IID_PPV_ARGS(&nine)),"9On12 interface") ||
       !checked(nine->GetD3D12Device(IID_PPV_ARGS(&twelve)),"adopt game D3D12 device")) return false;
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(!checked(twelve->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"ordered bridge queue") ||
       !checked(twelve->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"bridge fence")) return false;
    IUnknown* queues[]={queue.Get()};D3D_FEATURE_LEVEL level{};
    if(!checked(D3D11On12CreateDevice(twelve.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,
        queues,1,0,&eleven,&context,&level),"11On12 device") ||
       !checked(eleven.As(&elevenOn12),"11On12 interface")) return false;
    D3DDEVICE_CREATION_PARAMETERS cp{};dev->GetCreationParameters(&cp);gameWindow=cp.hFocusWindow;
    const LUID luid=twelve->GetAdapterLuid();
    BVR_LOG("[aa bridge] adopted game GPU LUID %08lx:%08lx, 11On12 feature level 0x%x, window %p",luid.HighPart,luid.LowPart,level,gameWindow);
    return true;
}

bool capture(IDirect3DDevice9* dev,bool submit) {
    if(dead || !adopt(dev)) return false;
    ComPtr<IDirect3DSurface9> surface;
    if(!checked(dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&surface),"game backbuffer")) return false;
    ComPtr<ID3D12Resource> resource;
    if(!checked(nine->UnwrapUnderlyingResource(surface.Get(),queue.Get(),IID_PPV_ARGS(&resource)),"unwrap completed game image")) return false;
    // The resource is checked out in COMMON. The same queue performs every
    // 11On12 copy, releases to COMMON and signals the fence handed back to 9On12.
    const auto rd=resource->GetDesc();
    bool ok=true;
    if(!frame || frame->desc.BufferDesc.Width!=rd.Width || frame->desc.BufferDesc.Height!=rd.Height || sourceFormat!=rd.Format) {
        if(submit) {vr::on_resize(UINT(rd.Width),rd.Height,rd.Format);overlay::on_resize();hud::release_resources();}
        release_frame();
        D3D11_TEXTURE2D_DESC d{};d.Width=UINT(rd.Width);d.Height=rd.Height;d.Format=rd.Format;
        d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        // Origins' XR images are RGBA; D3D9 backbuffers are commonly BGRA/XRGB.
        // Never CopyResource between those unrelated families in the core.
        if(rd.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) {
            ok=checked(eleven->CreateTexture2D(&d,nullptr,&colourSource),"source-format intermediate") &&
               checked(eleven->CreateShaderResourceView(colourSource.Get(),nullptr,&colourSrv),"source-format view");
        }
        d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        ComPtr<ID3D11Texture2D> image;
        ok=ok && checked(eleven->CreateTexture2D(&d,nullptr,&image),"owned RGBA game-frame texture") &&
            checked(eleven->CreateRenderTargetView(image.Get(),nullptr,&frameRtv),"RGBA frame view");
        if(ok) {sourceFormat=rd.Format;frame=new Frame(image.Get(),gameWindow);BVR_LOG("[aa bridge] actual game image %ux%u source format %u samples %u -> RGBA core format %u",d.Width,d.Height,rd.Format,rd.SampleDesc.Count,d.Format);}
    }
    ComPtr<ID3D11Texture2D> wrapped;
    D3D11_RESOURCE_FLAGS flags{};
    if(ok) ok=checked(elevenOn12->CreateWrappedResource(resource.Get(),&flags,D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON,IID_PPV_ARGS(&wrapped)),"wrap game resource for frozen core");
    if(ok) {
        ID3D11Resource* resources[]={wrapped.Get()};elevenOn12->AcquireWrappedResources(resources,1);
        auto* destination=colourSource ? colourSource.Get():frame->image.Get();
        if(rd.SampleDesc.Count>1) context->ResolveSubresource(destination,0,wrapped.Get(),0,rd.Format);
        else context->CopyResource(destination,wrapped.Get());
        elevenOn12->ReleaseWrappedResources(resources,1);
        if(colourSource) ok=blit::shift_copy(context.Get(),frameRtv.Get(),UINT(rd.Width),rd.Height,colourSrv.Get(),0);
        context->Flush();
    }
    const UINT64 value=++serial;
    if(!checked(queue->Signal(fence.Get(),value),"resource return fence signal")) {
        // Device failure makes completion untrackable. Do not falsely return
        // or release a still checked-out resource; retain it until process exit.
        dead=true;surface.Detach();resource.Detach();wrapped.Detach();return false;
    }
    ID3D12Fence* fences[]={fence.Get()};UINT64 values[]={value};
    if(!checked(nine->ReturnUnderlyingResource(surface.Get(),1,values,fences),"return game image ownership")) {dead=true;return false;}
    if(ok && submit) {++frames;d3d11_hook::process_bridge_frame(frame);}
    return ok;
}
HRESULT WINAPI present(IDirect3DDevice9* dev,const RECT* src,const RECT* dst,HWND wnd,const RGNDATA* dirty) {
    if(dev==ownDevice) capture(dev,true);
    return originalPresent(dev,src,dst,wnd,dirty);
}
HRESULT WINAPI reset(IDirect3DDevice9* dev,D3DPRESENT_PARAMETERS* pp) {
    if(dev==ownDevice) {
        if(!drain()) {dead=true;return D3DERR_DEVICELOST;}
        vr::on_resize(pp->BackBufferWidth,pp->BackBufferHeight,0);overlay::on_resize();hud::release_resources();release_frame();
    }
    const HRESULT hr=originalReset(dev,pp);
    BVR_LOG("[aa bridge] game Reset %ux%u -> 0x%08lx",pp->BackBufferWidth,pp->BackBufferHeight,hr);
    return hr;
}
HRESULT create_device(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,HWND wnd,DWORD behavior,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out,bool hookDevice) {
    const HRESULT hr=d->CreateDevice(adapter,type,wnd,behavior,pp,out);
    if(FAILED(hr)) {
        BVR_LOG("[aa bridge] game CreateDevice adapter %u flags 0x%x %ux%u windowed %u format %u -> 0x%08lx",adapter,behavior,pp?pp->BackBufferWidth:0,pp?pp->BackBufferHeight:0,pp?pp->Windowed:0,pp?pp->BackBufferFormat:0,hr);
        return hr;
    }
    if(!hookDevice || !out || !*out) return hr;
    if(ownDevice && ownDevice!=*out) {BVR_LOG("[aa bridge] additional game device left untouched");return hr;}
    ownDevice=*out;
    BVR_LOG("[aa bridge] LIVE game device, adapter %u flags 0x%x FPU-preserve %u",adapter,behavior,!!(behavior&D3DCREATE_FPU_PRESERVE));
    auto** v=*reinterpret_cast<void***>(ownDevice);
    if(MH_CreateHook(v[17],reinterpret_cast<void*>(present),reinterpret_cast<void**>(&originalPresent))!=MH_OK ||
       MH_CreateHook(v[16],reinterpret_cast<void*>(reset),reinterpret_cast<void**>(&originalReset))!=MH_OK) {
        BVR_LOG("[aa bridge] device hooks unavailable; game remains flat");return hr;
    }
    MH_QueueEnableHook(v[17]);MH_QueueEnableHook(v[16]);MH_ApplyQueued();
    return hr;
}

// Scope interception to each returned factory. System D3D9/9On12 factories
// can dispatch internally through different implementations of CreateDevice;
// replacing a process-global trampoline on a second factory recurses into our
// first hook. A COM wrapper forwards directly to its own unmodified factory.
class Factory final : public IDirect3D9 {
    std::atomic<ULONG> refs{1};
    ComPtr<IDirect3D9> real;
    bool hookDevice;
public:
    Factory(IDirect3D9* owned,bool hook):hookDevice(hook) {real.Attach(owned);}
    HRESULT WINAPI QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IDirect3D9)) {
            *out=static_cast<IDirect3D9*>(this);AddRef();return S_OK;
        }
        return real->QueryInterface(iid,out);
    }
    ULONG WINAPI AddRef() override {return ++refs;}
    ULONG WINAPI Release() override {const ULONG n=--refs;if(!n)delete this;return n;}
    HRESULT WINAPI RegisterSoftwareDevice(void* p) override {return real->RegisterSoftwareDevice(p);}
    UINT WINAPI GetAdapterCount() override {return real->GetAdapterCount();}
    HRESULT WINAPI GetAdapterIdentifier(UINT a,DWORD f,D3DADAPTER_IDENTIFIER9* p) override {return real->GetAdapterIdentifier(a,f,p);}
    UINT WINAPI GetAdapterModeCount(UINT a,D3DFORMAT f) override {return real->GetAdapterModeCount(a,f);}
    HRESULT WINAPI EnumAdapterModes(UINT a,D3DFORMAT f,UINT m,D3DDISPLAYMODE* p) override {return real->EnumAdapterModes(a,f,m,p);}
    HRESULT WINAPI GetAdapterDisplayMode(UINT a,D3DDISPLAYMODE* p) override {return real->GetAdapterDisplayMode(a,p);}
    HRESULT WINAPI CheckDeviceType(UINT a,D3DDEVTYPE t,D3DFORMAT f,D3DFORMAT b,BOOL w) override {return real->CheckDeviceType(a,t,f,b,w);}
    HRESULT WINAPI CheckDeviceFormat(UINT a,D3DDEVTYPE t,D3DFORMAT f,DWORD u,D3DRESOURCETYPE r,D3DFORMAT c) override {return real->CheckDeviceFormat(a,t,f,u,r,c);}
    HRESULT WINAPI CheckDeviceMultiSampleType(UINT a,D3DDEVTYPE t,D3DFORMAT f,BOOL w,D3DMULTISAMPLE_TYPE m,DWORD* q) override {return real->CheckDeviceMultiSampleType(a,t,f,w,m,q);}
    HRESULT WINAPI CheckDepthStencilMatch(UINT a,D3DDEVTYPE t,D3DFORMAT f,D3DFORMAT r,D3DFORMAT z) override {return real->CheckDepthStencilMatch(a,t,f,r,z);}
    HRESULT WINAPI CheckDeviceFormatConversion(UINT a,D3DDEVTYPE t,D3DFORMAT s,D3DFORMAT d) override {return real->CheckDeviceFormatConversion(a,t,s,d);}
    HRESULT WINAPI GetDeviceCaps(UINT a,D3DDEVTYPE t,D3DCAPS9* p) override {return real->GetDeviceCaps(a,t,p);}
    HMONITOR WINAPI GetAdapterMonitor(UINT a) override {return real->GetAdapterMonitor(a);}
    HRESULT WINAPI CreateDevice(UINT a,D3DDEVTYPE t,HWND w,DWORD b,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** d) override {
        return create_device(real.Get(),a,t,w,b,p,d,hookDevice);
    }
};
}
bool install() {ready.store(true);return load_system9()!=nullptr;}
void draw_ui() {ImGui::Text("Game frames %u; bridge failures %u",frames.load(),failures.load());}
}

extern "C" __declspec(dllexport) IDirect3D9* WINAPI AA_CreateD3D9(UINT sdk) {
    using namespace bvr::aa::bridge;
    const ULONGLONG deadline=GetTickCount64()+10000;
    while(!ready.load() && AA_Identity()!=2 && GetTickCount64()<deadline) Sleep(1);
    HMODULE sys=load_system9();if(!sys) return nullptr;
    const auto native=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(sys,"Direct3DCreate9"));
    if(!ready.load() || AA_Identity()!=1) return native ? native(sdk):nullptr;
    auto factory=reinterpret_cast<PFN_Direct3DCreate9On12>(GetProcAddress(sys,"Direct3DCreate9On12"));
    if(!factory) {BVR_LOG("[aa bridge] plain 9On12 factory unavailable; game remains flat");return native(sdk);}
    D3D9ON12_ARGS args{};args.Enable9On12=TRUE;
    IDirect3D9* d=factory(sdk,&args,1);
    if(!d) {BVR_LOG("[aa bridge] 9On12 creation failed; game remains flat");return native(sdk);}
    BVR_LOG("[aa bridge] plain Direct3DCreate9On12 active; factory-scoped forwarding, behavior flags preserved");
    return new Factory(d,true);
}

extern "C" __declspec(dllexport) DWORD WINAPI AA_BridgeSelftest(HWND window) {
    using namespace bvr::aa::bridge;
    auto factory=reinterpret_cast<PFN_Direct3DCreate9On12>(GetProcAddress(load_system9(),"Direct3DCreate9On12"));
    if(!factory) return 1;
    D3D9ON12_ARGS args{};args.Enable9On12=TRUE;
    IDirect3D9* raw=factory(D3D_SDK_VERSION,&args,1);if(!raw)return 2;
    ComPtr<IDirect3D9> d;d.Attach(new Factory(raw,false));
    // Asylum creates two factories during startup. Keep both alive and create
    // a real device through each wrapper, rather than testing a raw factory.
    raw=factory(D3D_SDK_VERSION,&args,1);if(!raw)return 12;
    ComPtr<IDirect3D9> second;second.Attach(new Factory(raw,false));
    ComPtr<IUnknown> identity;ComPtr<IDirect3D9> identity9;
    if(FAILED(d.As(&identity)) || FAILED(identity.As(&identity9)) || identity9.Get()!=d.Get())return 13;
    D3DCAPS9 caps{};if(FAILED(second->GetDeviceCaps(0,D3DDEVTYPE_HAL,&caps)))return 14;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> dev;
    if(FAILED(d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_FPU_PRESERVE|D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&dev)))return 3;
    ComPtr<IDirect3DDevice9> secondDevice;
    auto secondParameters=pp;
    if(FAILED(second->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_FPU_PRESERVE|D3DCREATE_HARDWARE_VERTEXPROCESSING,&secondParameters,&secondDevice)))return 15;
    secondDevice.Reset();second.Reset();identity9.Reset();identity.Reset();
    ComPtr<IDirect3DTexture9> managed;
    if(FAILED(dev->CreateTexture(16,16,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&managed,nullptr)))return 4;
    for(unsigned i=0;i<8;++i) {
        ComPtr<IDirect3DSurface9> bb;dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb);
        const unsigned char r=51+i,g=128-i,b=204-i;
        if(FAILED(dev->ColorFill(bb.Get(),nullptr,D3DCOLOR_ARGB(255,r,g,b))) || !capture(dev.Get(),false)) return 5;
        D3D11_TEXTURE2D_DESC desc{};frame->image->GetDesc(&desc);
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stage;
        if(FAILED(eleven->CreateTexture2D(&desc,nullptr,&stage)))return 6;
        context->CopyResource(stage.Get(),frame->image.Get());context->Flush();
        D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context->Map(stage.Get(),0,D3D11_MAP_READ,0,&map)))return 7;
        const auto* p=static_cast<const unsigned char*>(map.pData)+32*map.RowPitch+32*4;
        const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format==DXGI_FORMAT_B8G8R8X8_UNORM;
        const bool exact=p[0]==(bgra?b:r) && p[1]==g && p[2]==(bgra?r:b);
        context->Unmap(stage.Get(),0);if(!exact)return 8;
    }
    managed.Reset();
    if(!drain())return 9;
    release_frame();pp.BackBufferWidth=80;pp.BackBufferHeight=72;
    if(FAILED(dev->Reset(&pp)) || !capture(dev.Get(),false) || frame->desc.BufferDesc.Width!=80 || frame->desc.BufferDesc.Height!=72)return 10;
    if(!drain())return 11;
    release_frame();elevenOn12.Reset();context.Reset();eleven.Reset();fence.Reset();queue.Reset();twelve.Reset();nine.Reset();
    return 0;
}
