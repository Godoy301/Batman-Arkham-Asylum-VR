#include <windows.h>
#include <d3d9.h>
#include <cwchar>
namespace {
HMODULE real=nullptr,mod=nullptr;
HMODULE system_d3d9() {
    if(!real) {wchar_t path[MAX_PATH]{};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\d3d9.dll");real=LoadLibraryW(path);}
    return real;
}
bool vr_launch() {
    const wchar_t* cmd=GetCommandLineW();const auto* token=wcsstr(cmd,L"-bavr");
    return token && (token==cmd || token[-1]==L' ') && (!token[5] || token[5]==L' ' || token[5]==L'"');
}
template<class T> T original(const char* name) {return reinterpret_cast<T>(GetProcAddress(system_d3d9(),name));}
}
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    if(vr_launch()) {
        if(!mod) {wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);auto* end=wcsrchr(path,L'\\');
            if(end) {end[1]=0;wcscat_s(path,L"batman_asylum.dll");mod=LoadLibraryW(path);}}
        if(mod) if(auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(mod,"_AA_CreateD3D9@4"))) return create(sdk);
    }
    auto fn=original<IDirect3D9*(WINAPI*)(UINT)>("Direct3DCreate9");return fn ? fn(sdk):nullptr;
}
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdk,IDirect3D9Ex** out) {
    auto fn=original<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>("Direct3DCreate9Ex");return fn ? fn(sdk,out):E_NOTIMPL;
}
extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR c,LPCWSTR n) {auto f=original<int(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_BeginEvent");return f?f(c,n):-1;}
extern "C" int WINAPI D3DPERF_EndEvent() {auto f=original<int(WINAPI*)()>("D3DPERF_EndEvent");return f?f():-1;}
extern "C" void WINAPI D3DPERF_SetMarker(D3DCOLOR c,LPCWSTR n) {auto f=original<void(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_SetMarker");if(f)f(c,n);}
extern "C" void WINAPI D3DPERF_SetRegion(D3DCOLOR c,LPCWSTR n) {auto f=original<void(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_SetRegion");if(f)f(c,n);}
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame() {auto f=original<BOOL(WINAPI*)()>("D3DPERF_QueryRepeatFrame");return f?f():FALSE;}
extern "C" void WINAPI D3DPERF_SetOptions(DWORD o) {auto f=original<void(WINAPI*)(DWORD)>("D3DPERF_SetOptions");if(f)f(o);}
extern "C" DWORD WINAPI D3DPERF_GetStatus() {auto f=original<DWORD(WINAPI*)()>("D3DPERF_GetStatus");return f?f():0;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,LPVOID) {if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);return TRUE;}
