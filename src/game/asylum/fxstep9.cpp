// Debug toolkit (VR_DEBUG_TOOLKIT.md "collect the effects on screen" port):
// hide each kind of 3D scene draw (by vertex shader) in turn for a short step,
// both eyes, logging the frame generation each step starts at, then save the
// 10-second stereo replay. The replay frame whose generation falls in a step
// shows the scene without that kind: whatever vanished is drawn by it.
#include "fxstep9.h"
#include "core/gfx/stereo_history.h"
#include "core/util/log.h"
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::fxstep9 {
namespace {
constexpr ULONGLONG kStepMs=250;constexpr size_t kMaxKinds=36;
struct Kind{void*vs{};unsigned draws{},prims{};UINT texW{},texH{};uint64_t hash{};};
enum State{Idle,Collect,Step};std::atomic<int> state{Idle};std::vector<Kind> kinds;size_t step{};ULONGLONG stepAt{};void*hidden{};uint32_t generation{};int eye{};UINT width{},height{};
std::atomic<unsigned> skipped{};
uint64_t vs_hash(void*vs){UINT n{};if(!vs||FAILED(static_cast<IDirect3DVertexShader9*>(vs)->GetFunction(nullptr,&n))||n<8||n>262144||n%4)return 0;std::vector<unsigned char> b(n);if(FAILED(static_cast<IDirect3DVertexShader9*>(vs)->GetFunction(b.data(),&n)))return 0;uint64_t h=1469598103934665603ull;for(auto c:b){h^=c;h*=1099511628211ull;}return h;}
void begin_step(){
 if(step>=kinds.size()){hidden=nullptr;state=Idle;BVR_LOG("[aa fxstep] done at generation %u: %zu kinds stepped; saving the replay",generation,kinds.size());stereo_history::request_save();return;}
 auto&k=kinds[step];hidden=k.vs;stepAt=GetTickCount64();BVR_LOG("[aa fxstep] step %zu/%zu from generation %u: hiding VS %016llx (%u draws, %u prims, tex %ux%u)",step+1,kinds.size(),generation,(unsigned long long)k.hash,k.draws,k.prims,k.texW,k.texH);
}
}
void request(){int idle=Idle;if(state.compare_exchange_strong(idle,Collect)){kinds.clear();BVR_LOG("[aa fxstep] armed: next LEFT scene pass lists the draw kinds, then each is hidden for %llu ms",kStepMs);}}
bool running(){return state.load()!=Idle;}
void begin_eye(int e,uint32_t g,UINT w,UINT h){
 eye=e;width=w;height=h;if(e>=0)return;
 const int s=state.load();
 if(s==Collect&&!kinds.empty()){for(auto&k:kinds)k.hash=vs_hash(k.vs);generation=g;step=0;state=Step;begin_step();return;}
 if(s==Step&&GetTickCount64()-stepAt>=kStepMs){generation=g;++step;begin_step();}
 else if(s==Step)generation=g;
}
bool hide(IDirect3DDevice9*d,bool sceneDraw,UINT primitives){
 const int s=state.load();if(s==Idle||!sceneDraw||!eye)return false;
 ComPtr<IDirect3DVertexShader9>vs;d->GetVertexShader(&vs);if(!vs)return false;
 if(s==Collect){if(eye>0)return false;
  // Full-screen passes (2 triangles over a screen-sized texture) are skipped:
  // hiding them blanks the whole view instead of one object.
  ComPtr<IDirect3DBaseTexture9>tex;ComPtr<IDirect3DTexture9>t;D3DSURFACE_DESC td{};d->GetTexture(0,&tex);if(tex&&SUCCEEDED(tex.As(&t)))t->GetLevelDesc(0,&td);
  if(primitives<=2&&td.Width>=width/2&&td.Height>=height/2)return false;
  for(auto&k:kinds)if(k.vs==vs.Get()){++k.draws;k.prims+=primitives;return false;}
  if(kinds.size()<kMaxKinds)kinds.push_back({vs.Get(),1,primitives,td.Width,td.Height});return false;}
 if(vs.Get()!=hidden)return false;++skipped;return true;
}
}
