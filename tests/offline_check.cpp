#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
int wmain(int argc,wchar_t** argv) {
    if(argc<4) return 1;
    HMODULE mod=LoadLibraryW(argv[1]);if(!mod)return 2;
    auto identity=reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(mod,"_AA_Identity@0"));if(!identity)return 3;
    for(unsigned i=0;i<100 && !identity();++i)Sleep(10);
    if(identity()!=2)return 4;
    HMODULE proxy=LoadLibraryW(argv[2]),input=LoadLibraryW(argv[3]);if(!proxy || !input)return 5;
    for(auto name:{"Direct3DCreate9","Direct3DCreate9Ex","D3DPERF_BeginEvent","D3DPERF_EndEvent","D3DPERF_GetStatus"}) if(!GetProcAddress(proxy,name))return 6;
    if(!GetProcAddress(input,MAKEINTRESOURCEA(2)) || !GetProcAddress(input,MAKEINTRESOURCEA(3)) || !GetProcAddress(input,"BVR_SetPostGetStateHook"))return 7;
    auto factory=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(proxy,"Direct3DCreate9"));
    if(argc==4) {auto* d=factory(D3D_SDK_VERSION);if(!d)return 8;d->Release();puts("PASS: foreign host rejected, ordinary factory forwards, imported D3D9/XInput exports available.");return 0;}
    auto gpu=reinterpret_cast<DWORD(WINAPI*)(HWND)>(GetProcAddress(mod,"_AA_BridgeSelftest@4"));if(!gpu)return 9;
    HWND window=CreateWindowExW(0,L"STATIC",L"Asylum GPU fixture",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const DWORD result=gpu(window);DestroyWindow(window);
    if(result) {printf("GPU transport failed stage %lu\n",result);return 10;}
    puts("PASS: two wrapped 9On12 factories/devices and COM identity; D3DPOOL_MANAGED texture; eight exact color transfers through actual bridge; ordered fenced resource returns and resize to 80x72.");
    return 0;
}
