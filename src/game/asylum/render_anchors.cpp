// Asylum's own command executors: observer only, original runs exactly once.
// STATIC: tiny thiscall bodies, no stack arguments, return command size 8.
// These do not identify a gameplay caller or authorize stereo doubling yet.
#include "scene.h"
#include "core/util/log.h"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <cstdio>
namespace bvr::aa::scene {
namespace {
using Execute=DWORD(__fastcall*)(void*,void*);
Execute originals[2]{};
std::atomic<unsigned> counts[2]{};
uintptr_t base=0;
bool read(const void* p,void* out,size_t size) {
    __try {memcpy(out,p,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void observed(unsigned which,void* self) {
    const auto n=++counts[which];if(n>3)return;
    uintptr_t viewport=0;read(static_cast<unsigned char*>(self)+4,&viewport,4);
    uintptr_t vt=0;read(reinterpret_cast<void*>(viewport),&vt,4);
    void* stack[10]{};const USHORT depth=RtlCaptureStackBackTrace(0,10,stack,nullptr);
    char line[256]{};int at=0;
    for(USHORT i=0;i<depth && at<230;++i) {
        const uintptr_t p=reinterpret_cast<uintptr_t>(stack[i]);
        if(p>=base && p<base+0x26c0000)at+=sprintf_s(line+at,sizeof(line)-at," %08x",unsigned(p-base));
    }
    BVR_LOG("[aa render] command %u hit %u thread %lu, viewport %p vtable RVA %08x, exe stack:%s",
        which,n,GetCurrentThreadId(),reinterpret_cast<void*>(viewport),unsigned(vt-base),line);
}
DWORD __fastcall begin_a(void* self,void* edx) {observed(0,self);return originals[0](self,edx);}
DWORD __fastcall begin_b(void* self,void* edx) {observed(1,self);return originals[1](self,edx);}
}
void install_render_anchors() {
    base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const unsigned char signature[]={0x8b,0x49,0x04,0x8b,0x01,0x8b,0x50,0x54,0xff,0xd2,0xb8,0x08,0,0,0,0xc3};
    const uintptr_t rvas[]={0x896bb0,0x896cd0};
    void* targets[]={reinterpret_cast<void*>(base+rvas[0]),reinterpret_cast<void*>(base+rvas[1])};
    void* hooks[]={reinterpret_cast<void*>(begin_a),reinterpret_cast<void*>(begin_b)};
    for(unsigned i=0;i<2;++i) {
        unsigned char actual[sizeof(signature)]{};
        if(!read(targets[i],actual,sizeof(actual)) || memcmp(actual,signature,sizeof(actual))) {
            BVR_LOG("[aa render] Asylum instruction guard mismatch; no command observers installed");return;
        }
    }
    bool ok=true;
    for(unsigned i=0;i<2;++i) {
        if(MH_CreateHook(targets[i],hooks[i],reinterpret_cast<void**>(&originals[i]))!=MH_OK) {ok=false;break;}
        MH_QueueEnableHook(targets[i]);
    }
    if(ok)ok=MH_ApplyQueued()==MH_OK;
    if(!ok) {for(auto p:targets)MH_QueueDisableHook(p);MH_ApplyQueued();}
    BVR_LOG("[aa render] Asylum guarded command observers %s; stereo replay not enabled",ok?"installed":"unavailable");
}
void render_anchor_status() {BVR_LOG("[aa render] begin commands A/B %u/%u; camera seams still pending",counts[0].load(),counts[1].load());}
}
