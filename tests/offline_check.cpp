#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <dbghelp.h>
LONG WINAPI native_fault(EXCEPTION_POINTERS* p){
    printf("Native fixture exception %08lx at %p\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress);
    HANDLE process=GetCurrentProcess();SymInitialize(process,nullptr,TRUE);
    STACKFRAME64 f{};f.AddrPC={p->ContextRecord->Eip,0,AddrModeFlat};f.AddrStack={p->ContextRecord->Esp,0,AddrModeFlat};f.AddrFrame={p->ContextRecord->Ebp,0,AddrModeFlat};
    CONTEXT c=*p->ContextRecord;for(unsigned i=0;i<20&&f.AddrPC.Offset;++i){char mem[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto*s=reinterpret_cast<SYMBOL_INFO*>(mem);s->SizeOfStruct=sizeof(*s);s->MaxNameLen=MAX_SYM_NAME;DWORD64 delta{};if(SymFromAddr(process,f.AddrPC.Offset,&delta,s))printf("  %llx %s + %llu\n",f.AddrPC.Offset,s->Name,delta);else printf("  %llx\n",f.AddrPC.Offset);if(!StackWalk64(IMAGE_FILE_MACHINE_I386,process,GetCurrentThread(),&f,&c,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;}
    fflush(stdout);return EXCEPTION_EXECUTE_HANDLER;
}
int wmain(int argc,wchar_t** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc>4&&!wcscmp(argv[4],L"native"))SetUnhandledExceptionFilter(native_fault);
    if(argc<4) return 1;
    if(argc>4&&!wcscmp(argv[4],L"native"))printf("Native harness: loading runtime\n");
    HMODULE mod=LoadLibraryW(argv[1]);if(!mod)return 2;
    auto identity=reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(mod,"_AA_Identity@0"));if(!identity)return 3;
    for(unsigned i=0;i<100 && !identity();++i)Sleep(10);
    if(identity()!=2)return 4;
    if(argc>4&&!wcscmp(argv[4],L"native"))printf("Native harness: identity accepted, loading proxies\n");
    HMODULE proxy=LoadLibraryW(argv[2]),input=LoadLibraryW(argv[3]);if(!proxy || !input)return 5;
    for(auto name:{"Direct3DCreate9","Direct3DCreate9Ex","D3DPERF_BeginEvent","D3DPERF_EndEvent","D3DPERF_GetStatus"}) if(!GetProcAddress(proxy,name))return 6;
    if(!GetProcAddress(input,MAKEINTRESOURCEA(2)) || !GetProcAddress(input,MAKEINTRESOURCEA(3)) || !GetProcAddress(input,"BVR_SetPostGetStateHook"))return 7;
    auto factory=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(proxy,"Direct3DCreate9"));
    if(argc==4) {auto* d=factory(D3D_SDK_VERSION);if(!d)return 8;d->Release();puts("PASS: foreign host rejected, ordinary factory forwards, imported D3D9/XInput exports available.");return 0;}
    if(!wcscmp(argv[4],L"panel")){auto panel=reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(mod,"_AA_PanelSelftest@0"));return panel?int(panel()):11;}
    const bool native=!wcscmp(argv[4],L"native");
    auto gpu=reinterpret_cast<DWORD(WINAPI*)(HWND)>(GetProcAddress(mod,native?"_AA_NativeBridgeSelftest@4":"_AA_BridgeSelftest@4"));if(!gpu)return 9;
    HWND window=CreateWindowExW(0,L"STATIC",L"Asylum GPU fixture",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(native)printf("Native harness: call assembled mod fixture\n");
    const DWORD result=gpu(window);DestroyWindow(window);
    if(result) {printf("GPU transport failed stage %lu\n",result);return 10;}
    if(native)puts("PASS: assembled native9Ex -> DX11 mod, actual hooked HUD/depth, eye lenses and x64 DLSS; no game launch.");
    else puts("PASS: two 9On12 devices, patterned color transfers, production centred-to-XR lens remap; upright anchored preview and private-stencil HUD extraction/composition; real per-eye D24S8 .25/.75 depth; x86-to-x64 NGX DLAA/Quality both eyes, camera motion/jitter cancellation and missing-right pair rollback; depth-based AFW both directions/forward+inverted-Z, AFW NGX single-eye histories, full2284x2448 game source with fitted mirror/logical WM_SIZE; fenced returns and reset.");
    return 0;
}
