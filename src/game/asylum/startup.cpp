// Asylum Bink lifecycle, not the City/Origins GFx player or an EOF trick.
#include "startup.h"
#include "opening_movies.h"
#include "core/input/xinput_bridge.h"
#include "core/util/log.h"
#include <windows.h>
#include <Xinput.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cstdio>
namespace bvr::aa::startup {
namespace {
using Open=void*(WINAPI*)(const char*,unsigned);
using Close=void(WINAPI*)(void*);
using Tick=void(__fastcall*)(void*,void*,float);
using Stop=void(__thiscall*)(void*,float,int,int);
using RenderStop=int(__thiscall*)(void*,int);
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
Open openOriginal{};Close closeOriginal{};Tick tickOriginal{};GetState getState{};
uintptr_t base{};
std::atomic<void*> logo{};
std::atomic<bool> active{false},requested{false},stopped{false};
std::atomic<unsigned short> consumed[4]{};
constexpr unsigned short buttons=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK;
bool read(const void*p,void*out,size_t n){__try{memcpy(out,p,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool startup_name(const char*path){
 char name[MAX_PATH]{};unsigned n=0;for(;n<MAX_PATH-1;n++){if(!read(path+n,name+n,1))return false;if(!name[n])break;}if(n==MAX_PATH-1)return false;
 wchar_t wide[MAX_PATH]{};for(unsigned i=0;i<n;++i)wide[i]=static_cast<unsigned char>(name[i]);
 return opening_movie_name(wide);
}
bool startup_entry(void*self,int index,int count){
 // STATIC 6d0c90: startup list at +70, 1Ch entries; first 0Ch is
 // a Unicode FString. This works even when Bink opens MEMORY, not a path.
 if(index<0||index>=count||count>16)return false;
 uint32_t entries=0,entry[3]{};
 if(!read(static_cast<BYTE*>(self)+0x70,&entries,4)||!entries||
    !read(reinterpret_cast<void*>(entries+index*0x1c),entry,sizeof(entry))||!entry[0]||entry[1]<1||entry[1]>MAX_PATH||entry[2]<entry[1])return false;
 wchar_t name[MAX_PATH]{};
 if(!read(reinterpret_cast<void*>(entry[0]),name,entry[1]*sizeof(wchar_t))||name[entry[1]-1]!=0)return false;
 return opening_movie_name(std::wstring_view(name,entry[1]-1));
}
void* WINAPI open(const char*path,unsigned flags){void*h=openOriginal(path,flags);if(h&&startup_name(path)){logo=h;BVR_LOG("[aa startup] known opening movie %s -> %p",path,h);}return h;}
void WINAPI close(void*h){void*expected=h;if(logo.compare_exchange_strong(expected,nullptr))active=false;closeOriginal(h);}
void filter(unsigned index,uint16_t*b){
 if(index>=4)return;auto held=consumed[index].load();held&=*b;
 if(active&&!stopped){auto pressed=uint16_t(*b&buttons);if(pressed){requested=true;held|=pressed;}}
 consumed[index]=held;*b&=~held;
}
DWORD WINAPI poll(void*){
 for(;;){if(active&&!stopped&&getState)for(unsigned i=0;i<4;i++){XINPUT_STATE s{};
  input::set_menu_poll(true);const auto result=getState(i,&s);input::set_menu_poll(false);
  if(result==ERROR_SUCCESS){auto p=uint16_t(s.Gamepad.wButtons&buttons);if(p){consumed[i].fetch_or(p);if(!requested.exchange(true))BVR_LOG("[aa startup] pad %u requested skip, buttons %04X",i,p);}}}Sleep(16);}
}
void __fastcall tick(void*self,void*edx,float dt){
 uintptr_t vt=0;void*h=nullptr;int index=-1,count=0;
 const bool ours=read(self,&vt,4)&&vt==base+0x1acff08;
 if(ours){read(static_cast<BYTE*>(self)+0x4c,&h,4);read(static_cast<BYTE*>(self)+0x7c,&index,4);read(static_cast<BYTE*>(self)+0x74,&count,4);}
 const bool opening=ours&&h&&(startup_entry(self,index,count)||h==logo.load());
 static bool logged=false;
 if(opening&&!logged){logged=true;BVR_LOG("[aa startup] engine startup entry recognised: index %d of %d, handle %p, thread %lu, gameThread %d renderThread %d",index,count,h,GetCurrentThreadId(),reinterpret_cast<int(__cdecl*)()>(base+0x315880)(),reinterpret_cast<int(__cdecl*)()>(base+0x315850)());}
 active=opening&&!stopped;
 tickOriginal(self,edx,dt);
 // Use the engine's owning-thread predicates before its native stop methods.
 void*current=nullptr;int currentIndex=-1;
 if(ours){read(static_cast<BYTE*>(self)+0x4c,&current,4);read(static_cast<BYTE*>(self)+0x7c,&currentIndex,4);}
 const bool gameThread=reinterpret_cast<int(__cdecl*)()>(base+0x315880)()!=0;
 const bool renderThread=reinterpret_cast<int(__cdecl*)()>(base+0x315850)()!=0;
 if(opening&&current&&current==h&&currentIndex==index&&requested&&(gameThread||renderThread)&&!stopped.exchange(true)){
  // Ret 0Ch, three stack arguments. False wait, true force: the engine's
  // force-stop bypasses the remaining startup list on its own render queue.
  if(gameThread)reinterpret_cast<Stop>(base+0x2d1fa0)(self,0.f,0,1);
  else reinterpret_cast<RenderStop>(base+0x2cca00)(self,1);
  active=false;BVR_LOG("[aa startup] gamepad cancelled opening sequence through Asylum %s stop (thread %lu)",gameThread?"game-thread":"render-thread",GetCurrentThreadId());
 }
}
bool signature(uintptr_t target,const char*sig,unsigned reloc){BYTE e[16]{},a[16]{};for(int n=0;n<16;n++){unsigned v;sscanf_s(sig+2*n,"%2x",&v);e[n]=BYTE(v);}if(reloc){uint32_t v;memcpy(&v,e+reloc,4);v+=uint32_t(base-0x400000);memcpy(e+reloc,&v,4);}return read((void*)target,a,16)&&!memcmp(e,a,16);}
}
bool install(){
 base=uintptr_t(GetModuleHandleW(nullptr));
 if(!signature(base+0x2d1cf0,"0f57c9f30f1054240483ec405355568b",0)||!signature(base+0x2d1fa0,"6aff68907cc70164a1000000005083ec",3)||!signature(base+0x2cca00,"5633c0578b7c240c3bf8be0100000089",0)){BVR_LOG("[aa startup] movie guard failed");return false;}
 auto bink=GetModuleHandleW(L"binkw32.dll");if(!bink)return false;
 void*targets[]={(void*)GetProcAddress(bink,"_BinkOpen@8"),(void*)GetProcAddress(bink,"_BinkClose@4"),(void*)(base+0x2d1cf0)};
 void*hooks[]={(void*)open,(void*)close,(void*)tick};void**originals[]={(void**)&openOriginal,(void**)&closeOriginal,(void**)&tickOriginal};
 bool ok=true;for(int n=0;n<3;n++){if(!targets[n]||MH_CreateHook(targets[n],hooks[n],originals[n])!=MH_OK){ok=false;break;}MH_QueueEnableHook(targets[n]);}
 if(ok)ok=MH_ApplyQueued()==MH_OK;
 if(!ok){for(auto p:targets)if(p)MH_QueueDisableHook(p);MH_ApplyQueued();BVR_LOG("[aa startup] movie hooks unavailable");return false;}
 wchar_t path[MAX_PATH]{};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\xinput1_4.dll");auto real=LoadLibraryW(path);if(real)getState=reinterpret_cast<GetState>(GetProcAddress(real,"XInputGetState"));
 input::set_button_filter(filter);if(getState){auto thread=CreateThread(nullptr,0,poll,nullptr,0,nullptr);if(thread)CloseHandle(thread);}
 BVR_LOG("[aa startup] A/B/X/Y, Start or Back cancels opening logos; guarded native game/render-thread force-stop, story/loading movies excluded");return true;
}
bool logo_active(){return active;}
}
