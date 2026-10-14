#include "menu_state.h"
#include "core/util/log.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <cstdio>
namespace bvr::aa::menu_state {
namespace {
uintptr_t base{};bool ready{};
bool read(uintptr_t address,void* out,size_t bytes){__try{memcpy(out,reinterpret_cast<void*>(address),bytes);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool guard(uintptr_t address,const BYTE* expected,size_t bytes){BYTE actual[32]{};return bytes<=sizeof(actual)&&read(address,actual,bytes)&&!memcmp(actual,expected,bytes);}
}
bool install(uintptr_t imageBase){
 // Hash-supported Asylum's IsMenuLevel native reads this chain, and its
 // map-change setter writes this exact flag. No engine calls or state writes.
 const BYTE getter[]={0x8b,0xb1,0x90,0x02,0,0,0xc1,0xee,0x0a,0x83,0xe6,0x01};
 const BYTE setter[]={0xc1,0xe0,0x0a,0x33,0x81,0x90,0x02,0,0,0x25,0,0x04,0,0};
 BYTE chain[]={0xa1,0xb8,0x62,0x6b,0x02,0x8b,0x70,0x44,0x8b,0x46,0x34,0x83,0xc6,0x30};
 const uint32_t global=uint32_t(imageBase+0x22b62b8);memcpy(chain+1,&global,4);
 ready=guard(imageBase+0x5b62d9,getter,sizeof(getter))&&guard(imageBase+0x5cb716,setter,sizeof(setter))&&guard(imageBase+0x5c6fb1,chain,sizeof(chain));base=imageBase;
 BVR_LOG("[aa menu state] engine IsMenuLevel reader/setter guards %s; particle correction gameplay only",ready?"matched":"unavailable");return ready;
}
Mode from_engine(uintptr_t engine){
 uint32_t client{},viewports[3]{},viewport{},flags{};
 if(!engine||!read(engine+0x44,&client,4)||!client||!read(client+0x30,viewports,sizeof(viewports))||!viewports[0]||viewports[1]<1||viewports[1]>16||viewports[2]<viewports[1]||viewports[2]>16||!read(viewports[0],&viewport,4)||!viewport||!read(uintptr_t(viewport)+0x290,&flags,4))return Mode::unknown;
 return flags&0x400?Mode::menu:Mode::gameplay;
}
Mode current(){uint32_t engine{};if(!ready||!read(base+0x22b62b8,&engine,4))return Mode::unknown;return from_engine(engine);}
bool fixture(){
 std::array<BYTE,0x50> engine{},client{};std::array<BYTE,0x300> viewport{};uint32_t list=uint32_t(uintptr_t(viewport.data()));
 auto pointer=[](auto& data,size_t offset,const void* p){const uint32_t value=uint32_t(uintptr_t(p));memcpy(data.data()+offset,&value,4);};
 pointer(engine,0x44,client.data());pointer(client,0x30,&list);const uint32_t counts[2]={1,1};memcpy(client.data()+0x34,counts,8);
 uint32_t flags=0xdeadbeef|0x400;memcpy(viewport.data()+0x290,&flags,4);
 const auto address=uintptr_t(engine.data());if(from_engine(address)!=Mode::menu)return false;
 flags&=~0x400;memcpy(viewport.data()+0x290,&flags,4);if(from_engine(address)!=Mode::gameplay)return false;
 flags|=0x400;memcpy(viewport.data()+0x290,&flags,4);if(from_engine(address)!=Mode::menu)return false;
 client[0x34]=0;if(from_engine(address)!=Mode::unknown)return false;
 if(from_engine(0)!=Mode::unknown||from_engine(1)!=Mode::unknown)return false;
 printf("Menu state: engine IsMenuLevel chain menu/gameplay/menu, unrelated flag bits preserved, invalid/teardown reads fail closed PASS\n");return true;
}
}
