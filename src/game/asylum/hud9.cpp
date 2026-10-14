// D3D9 equivalent of Infinite/City's final scene-blit -> UI boundary.
// Only the owned main backbuffer is redirected. Private filter targets keep
// their original bindings and draws; right-eye UI raster reuses the LEFT sheet.
#include "hud9.h"
#include "d3d9_bridge.h"
#include "detail9.h"
#include "billboard9.h"
#include "transitions9.h"
#include "markers9.h"
#include "eyesame9.h"
#include "xform9.h"
#include "fxstep9.h"
#include "core/util/log.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <imgui.h>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <algorithm>
#include <array>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::hud9 {
namespace {
IDirect3DDevice9* device{};
ComPtr<IDirect3DSurface9> backbuffer,capture,stencil;
ComPtr<IDirect3DSurface9> sceneDepth;
uint64_t depthDrawWeight{},frozenDepthWeight{};DWORD depthCompare{};
bool depthFrozen{};
bool depthConsumer{};
ComPtr<IDirect3DTexture9> captureTexture;
int currentEye{};uint32_t currentGeneration{};
bool boundary{},cleared{};
bool leftReady{};uint32_t leftGeneration{};
unsigned frameDraws{};
std::atomic<unsigned> boundaries{},captured{},skipped{},unsupported{};
using DP=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using DIP=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using UP=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
using IUP=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
using Clear=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD);
using SetDepth=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*);
DP dp{};DIP dip{};UP up{};IUP iup{};Clear clear{};
SetDepth setDepth{};
using SetTarget=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
using SetViewport=HRESULT(WINAPI*)(IDirect3DDevice9*,const D3DVIEWPORT9*);
using SetState=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD);
using ApplyBlock=HRESULT(WINAPI*)(IDirect3DStateBlock9*);
SetTarget setTarget{};SetViewport setViewport{};SetState setState{};ApplyBlock applyBlock{};
void freeze_depth();
std::atomic<bool> fastTracking{true};wchar_t settings[MAX_PATH]{};
struct Bindings {ComPtr<IDirect3DSurface9> rt,ds;D3DSURFACE_DESC rtDesc{},dsDesc{},bbDesc{};D3DVIEWPORT9 vp{};bool dirty=true,valid{},depthValid{},main{};} bindings;
DWORD cachedZ{},cachedWrite{},cachedCompare{};bool stateDirty=true;
unsigned bindingReads{},stateReads{},classified{},blockRestores{};double classifierMs{};
double cpu_now(){static const double scale=[](){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return 1000./f.QuadPart;}();LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart*scale;}
void invalidate(){bindings=Bindings{};stateDirty=true;}
struct ClassifierTimer {double at=cpu_now();~ClassifierTimer(){classifierMs+=cpu_now()-at;++classified;}};
struct Span {unsigned count{};double total{},peak{};void add(double v){++count;total+=v;if(v>peak)peak=v;}double mean()const{return count?total/count:0;}};
Span renderCpu[2],renderGpu[2];double renderAt{};int renderEye{};
struct Chunk {ComPtr<IDirect3DQuery9> start,end;unsigned first{},draws{},primitives{},width{},height{},format{};DWORD z{},blend{};uint64_t shader{};double cpuAt{},cpuMs{};};
struct GpuSample {ComPtr<IDirect3DQuery9> start,end,freq,disjoint;bool pending{};int eye{};Chunk chunks[64];unsigned chunkCount{},drawCount{},overflow{};};
GpuSample gpuSamples[8];int activeSample=-1;bool gpuAttempted{},gpuSupported{},samplePair{};ULONGLONG nextGpuSample{};unsigned gpuSkipped{},gpuRejected{};
struct SlowChunk {double gpuMs{},cpuMs{};unsigned first{},draws{},primitives{},width{},height{},format{},z{},blend{};uint64_t shader{};};
std::vector<SlowChunk> slowChunks[2];unsigned timelineOverflow{},timelineRejected{};
struct ShaderStamp {ComPtr<IDirect3DPixelShader9> object;uint64_t hash{};};ShaderStamp shaderStamps[64];unsigned shaderStampNext{};
uint64_t shader_stamp(){
 ComPtr<IDirect3DPixelShader9> shader;if(FAILED(device->GetPixelShader(&shader))||!shader)return 0;
 for(const auto& item:shaderStamps)if(item.object.Get()==shader.Get())return item.hash;
 UINT size{};std::array<unsigned char,8192> code{};uint64_t hash=14695981039346656037ull;
 if(FAILED(shader->GetFunction(nullptr,&size))||size>code.size()||FAILED(shader->GetFunction(code.data(),&size)))return 0;
 for(unsigned i=0;i<size;++i){hash^=code[i];hash*=1099511628211ull;}
 auto& item=shaderStamps[shaderStampNext++%64];item.object=shader;item.hash=hash;return hash;
}
void finish_chunk(){if(activeSample<0)return;auto& s=gpuSamples[activeSample];if(!s.chunkCount)return;auto& c=s.chunks[s.chunkCount-1];if(!c.draws||c.cpuMs>=0)return;c.end->Issue(D3DISSUE_END);c.cpuMs=cpu_now()-c.cpuAt;}
void begin_chunk(UINT primitives){
 if(activeSample<0)return;auto& s=gpuSamples[activeSample];
 if(s.drawCount%64==0){finish_chunk();if(s.chunkCount<64){auto& c=s.chunks[s.chunkCount++];c.first=s.drawCount;c.draws=c.primitives=0;c.cpuMs=-1;c.width=c.height=c.format=c.z=c.blend=0;c.shader=shader_stamp();
   ComPtr<IDirect3DSurface9> target;D3DSURFACE_DESC d{};if(SUCCEEDED(device->GetRenderTarget(0,&target))&&target&&SUCCEEDED(target->GetDesc(&d))){c.width=d.Width;c.height=d.Height;c.format=unsigned(d.Format);}
   device->GetRenderState(D3DRS_ZENABLE,&c.z);device->GetRenderState(D3DRS_ALPHABLENDENABLE,&c.blend);c.cpuAt=cpu_now();c.start->Issue(D3DISSUE_END);
  }else ++s.overflow;
 }
 if(s.drawCount<4096&&s.chunkCount){auto& c=s.chunks[s.chunkCount-1];++c.draws;c.primitives+=primitives;}
 ++s.drawCount;
}
void draw_chunk_done(){if(activeSample>=0&&gpuSamples[activeSample].drawCount%64==0)finish_chunk();}
void collect_gpu(){
 for(auto& s:gpuSamples)if(s.pending){
  UINT64 start{},end{},freq{};BOOL disjoint=TRUE;
  const auto hr=s.end->GetData(&end,sizeof(end),0);
  if(hr==S_FALSE)continue;
  if(FAILED(hr)){s.pending=false;++gpuRejected;continue;}
  const HRESULT a=s.start->GetData(&start,sizeof(start),0),b=s.freq->GetData(&freq,sizeof(freq),0),c=s.disjoint->GetData(&disjoint,sizeof(disjoint),0);
  if(a==S_FALSE||b==S_FALSE||c==S_FALSE)continue;
  if(SUCCEEDED(a)&&SUCCEEDED(b)&&SUCCEEDED(c)&&!disjoint&&freq&&end>=start){
   bool ready=true;UINT64 starts[64]{},ends[64]{};
   for(unsigned i=0;i<s.chunkCount;++i){const auto x=s.chunks[i].start->GetData(&starts[i],sizeof(UINT64),0),y=s.chunks[i].end->GetData(&ends[i],sizeof(UINT64),0);if(x==S_FALSE||y==S_FALSE){ready=false;break;}if(FAILED(x)||FAILED(y)||ends[i]<starts[i]){++timelineRejected;starts[i]=ends[i]=0;}}
   if(!ready)continue;
   auto& ranked=slowChunks[s.eye];for(unsigned i=0;i<s.chunkCount;++i){const auto& p=s.chunks[i];if(!p.draws||ends[i]<=starts[i])continue;
    ranked.push_back({1000.*double(ends[i]-starts[i])/double(freq),p.cpuMs,p.first,p.draws,p.primitives,p.width,p.height,p.format,p.z,p.blend,p.shader});
   }
   std::sort(ranked.begin(),ranked.end(),[](const auto& x,const auto& y){return x.gpuMs>y.gpuMs;});if(ranked.size()>8)ranked.resize(8);timelineOverflow+=s.overflow;
  }
  s.pending=false;
  if(FAILED(a)||FAILED(b)||FAILED(c)||disjoint||!freq||end<start){++gpuRejected;continue;}
  renderGpu[s.eye].add(1000.*double(end-start)/double(freq));
 }
}
void begin_budget(int eye){
 collect_gpu();if(!eye)return;renderAt=cpu_now();renderEye=eye<0?0:1;
 // Sample both eyes of at most two pairs/second. Keep timestamp command
 // overhead out of the normal per-frame path; CPU spans remain every eye.
 if(eye<0){const auto now=GetTickCount64();samplePair=now>=nextGpuSample;if(samplePair)nextGpuSample=now+500;}
 if(!samplePair)return;
 if(!gpuAttempted){gpuAttempted=true;gpuSupported=true;
  for(auto& s:gpuSamples)if(FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&s.start))||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&s.end))||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&s.freq))||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&s.disjoint))){gpuSupported=false;break;}
  if(gpuSupported)for(auto& s:gpuSamples)for(auto& c:s.chunks)if(FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&c.start))||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&c.end))){gpuSupported=false;break;}
  if(!gpuSupported)for(auto& s:gpuSamples)s=GpuSample{};
  BVR_LOG("[aa budget] scene GPU timestamp ring %s; max2 sampled pairs/s, results polled without flush/wait",gpuSupported?"supported":"unavailable (CPU spans only)");
 }
 if(!gpuSupported)return;
 for(int i=0;i<8;++i)if(!gpuSamples[i].pending){auto& s=gpuSamples[i];s.eye=renderEye;
  s.chunkCount=s.drawCount=s.overflow=0;
  if(SUCCEEDED(s.disjoint->Issue(D3DISSUE_BEGIN))&&SUCCEEDED(s.freq->Issue(D3DISSUE_END))&&SUCCEEDED(s.start->Issue(D3DISSUE_END))){activeSample=i;return;}
  s.disjoint->Issue(D3DISSUE_END);++gpuRejected;return;
 }
 ++gpuSkipped; // never wait for a busy query slot
}

bool same(IDirect3DSurface9*a,IDirect3DSurface9*b){
 if(!a||!b)return false;if(a==b)return true;ComPtr<IUnknown> x,y;
 return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x.Get()==y.Get();
}
bool live_bindings(){
 if(!fastTracking||bindings.dirty){bindings.dirty=false;bindings.rt.Reset();bindings.ds.Reset();bindings.valid=bindings.depthValid=bindings.main=false;++bindingReads;
  bindings.valid=SUCCEEDED(device->GetRenderTarget(0,&bindings.rt))&&bindings.rt&&SUCCEEDED(bindings.rt->GetDesc(&bindings.rtDesc))&&backbuffer&&SUCCEEDED(backbuffer->GetDesc(&bindings.bbDesc))&&SUCCEEDED(device->GetViewport(&bindings.vp));
  bindings.depthValid=depthConsumer&&SUCCEEDED(device->GetDepthStencilSurface(&bindings.ds))&&bindings.ds&&SUCCEEDED(bindings.ds->GetDesc(&bindings.dsDesc));
  bindings.main=bindings.valid&&same(bindings.rt.Get(),backbuffer.Get());
 }
 return bindings.valid;
}
void live_states(){if(!fastTracking||stateDirty){stateDirty=false;++stateReads;cachedZ=cachedWrite=cachedCompare=0;device->GetRenderState(D3DRS_ZENABLE,&cachedZ);device->GetRenderState(D3DRS_ZWRITEENABLE,&cachedWrite);device->GetRenderState(D3DRS_ZFUNC,&cachedCompare);}}
HRESULT WINAPI target_hook(IDirect3DDevice9*d,DWORD slot,IDirect3DSurface9* next){const auto hr=setTarget(d,slot,next);if(d==device&&slot==0&&SUCCEEDED(hr))bindings.dirty=true;return hr;}
HRESULT WINAPI viewport_hook(IDirect3DDevice9*d,const D3DVIEWPORT9* next){const auto hr=setViewport(d,next);if(d==device&&SUCCEEDED(hr))bindings.dirty=true;return hr;}
HRESULT WINAPI state_hook(IDirect3DDevice9*d,D3DRENDERSTATETYPE type,DWORD value){const auto hr=setState(d,type,value);if(d==device&&SUCCEEDED(hr)&&(type==D3DRS_ZENABLE||type==D3DRS_ZWRITEENABLE||type==D3DRS_ZFUNC))stateDirty=true;return hr;}
HRESULT WINAPI apply_hook(IDirect3DStateBlock9* block){
 ComPtr<IDirect3DDevice9> owner;const bool ours=SUCCEEDED(block->GetDevice(&owner))&&owner.Get()==device;
 // D3D9 stateblocks restore viewport/render states, not RT/DS surfaces.
 // Apply can bypass our setters, so refresh live state after any owned block.
 const auto hr=applyBlock(block);if(ours){invalidate();billboard9::invalidate_shader();++blockRestores;}return hr;
}
void freeze_depth(){
 if(!depthConsumer||!sceneDepth||!depthDrawWeight||(depthFrozen&&depthDrawWeight<=frozenDepthWeight))return;
 if(bridge::capture_scene_depth(sceneDepth.Get(),currentEye,currentGeneration)){
  depthFrozen=true;frozenDepthWeight=depthDrawWeight;static unsigned n{};
  if(n++<12)BVR_LOG("[aa depth] frozen before DS clear/rebind/postprocess: eye %d generation %u geometry weight %llu hardware ZFUNC %u",currentEye,currentGeneration,depthDrawWeight,depthCompare);
 }
}
HRESULT WINAPI depth_hook(IDirect3DDevice9*d,IDirect3DSurface9* next){
 if(d==device&&currentEye&&!boundary&&sceneDepth&&!same(sceneDepth.Get(),next))freeze_depth();
 const auto hr=setDepth(d,next);if(d==device&&SUCCEEDED(hr))bindings.dirty=true;return hr;
}
bool on_backbuffer(IDirect3DDevice9*d){return live_bindings()&&bindings.main;}
bool ensure(IDirect3DDevice9*d){
 D3DSURFACE_DESC bd{},cd{};if(!backbuffer||FAILED(backbuffer->GetDesc(&bd)))return false;
 if(capture&&SUCCEEDED(capture->GetDesc(&cd))&&cd.Width==bd.Width&&cd.Height==bd.Height)return true;
 capture.Reset();stencil.Reset();captureTexture.Reset();
 if(bd.MultiSampleType!=D3DMULTISAMPLE_NONE || FAILED(d->CreateTexture(bd.Width,bd.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&captureTexture,nullptr))||FAILED(captureTexture->GetSurfaceLevel(0,&capture))||
    FAILED(d->CreateDepthStencilSurface(bd.Width,bd.Height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&stencil,nullptr))){capture.Reset();stencil.Reset();captureTexture.Reset();++unsupported;return false;}
 return true;
}
bool scene_blit(IDirect3DDevice9*d,D3DPRIMITIVETYPE type,UINT primitives){
 if(primitives!=2||(type!=D3DPT_TRIANGLELIST&&type!=D3DPT_TRIANGLESTRIP))return false;
 DWORD z=1,blend=1;d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
 if(z||blend)return false;
 ComPtr<IDirect3DBaseTexture9> source;ComPtr<IDirect3DTexture9> tex;
 if(FAILED(d->GetTexture(0,&source))||!source||FAILED(source.As(&tex)))return false;
 D3DSURFACE_DESC sd{};const auto& bd=bindings.bbDesc;const auto& vp=bindings.vp;
 if(FAILED(tex->GetLevelDesc(0,&sd)))return false;
 return (sd.Usage&D3DUSAGE_RENDERTARGET)&&sd.Width>=bd.Width&&sd.Width<=bd.Width+16&&sd.Height>=bd.Height&&sd.Height<=bd.Height+16&&
   vp.X==0&&vp.Y==0&&vp.Width==bd.Width&&vp.Height==bd.Height;
}
constexpr D3DRENDERSTATETYPE blendStates[]={D3DRS_COLORWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,
 D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA};
struct Scope {
 IDirect3DDevice9*d;ComPtr<IDirect3DSurface9> rt,ds;D3DVIEWPORT9 viewport{};RECT scissor{};
 DWORD saved[9]{};bool changed=false,skip=false;
 Scope(IDirect3DDevice9*dev,D3DPRIMITIVETYPE type,UINT count):d(dev){
  if(dev!=device||!currentEye)return;
  begin_chunk(count);
  if(!boundary)eyesame9::scene_draw(dev,count);
  // Main-scene depth is selected before the post/UI lane. Never use our
  // private UI stencil, a shadow atlas, or a differently sized filter target.
  ClassifierTimer timer;
  if(depthConsumer)live_states();
  if(depthConsumer&&!boundary&&cachedZ&&cachedWrite&&live_bindings()&&bindings.depthValid){
   const auto& rd=bindings.rtDesc;const auto& dd=bindings.dsDesc;const auto& bd=bindings.bbDesc;const auto& vp=bindings.vp;
   if(rd.Width>=bd.Width&&rd.Width<=bd.Width+64&&rd.Height>=bd.Height&&rd.Height<=bd.Height+64&&
      dd.Width==rd.Width&&dd.Height==rd.Height&&dd.MultiSampleType==D3DMULTISAMPLE_NONE&&vp.X==0&&vp.Y==0&&vp.Width==bd.Width&&vp.Height==bd.Height){
    if(!same(sceneDepth.Get(),bindings.ds.Get())){freeze_depth();sceneDepth=bindings.ds;depthDrawWeight=0;}
    depthDrawWeight+=count;depthCompare=cachedCompare;
   }
  }
  if(!on_backbuffer(dev))return;
  if(scene_blit(dev,type,count)){
   detail9::end_scene();
   freeze_depth();
   boundary=true;cleared=false;frameDraws=0;
   if(++boundaries<=4)BVR_LOG("[aa HUD] main scene blit boundary, eye %d generation %u",currentEye,currentGeneration);
   return;
  }
  if(!boundary||!ensure(dev))return;
  if(currentEye>0){if(leftReady&&leftGeneration==currentGeneration){skip=true;++skipped;}return;}
  dev->GetRenderTarget(0,&rt);dev->GetDepthStencilSurface(&ds);dev->GetViewport(&viewport);dev->GetScissorRect(&scissor);
  for(int i=0;i<9;++i)dev->GetRenderState(blendStates[i],&saved[i]);
  // Unbind the game's depth before changing target; every captured mask gets
  // our own same-sized cleared stencil, never the scene's stencil contents.
  dev->SetDepthStencilSurface(nullptr);
  if(FAILED(dev->SetRenderTarget(0,capture.Get()))||FAILED(dev->SetDepthStencilSurface(stencil.Get()))){restore();++unsupported;return;}
  changed=true;dev->SetViewport(&viewport);dev->SetScissorRect(&scissor);
  if(!cleared){const auto hr=clear(dev,0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1.f,0);if(FAILED(hr)){restore();++unsupported;return;}cleared=true;}
  // Preserve colour blends, including additive glows; accumulate coverage
  // separately. Mask-only draws must keep alpha writes off.
  if(saved[0]&7){
   dev->SetRenderState(D3DRS_COLORWRITEENABLE,saved[0]|D3DCOLORWRITEENABLE_ALPHA);
   dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);
   // City fixed_blend: only real OVER draws add coverage. Multiplication and
   // additive effects cannot make a transparent sheet into an opaque box.
   const bool over=!saved[1]||(saved[4]==D3DBLENDOP_ADD&&saved[3]==D3DBLEND_INVSRCALPHA&&(saved[2]==D3DBLEND_SRCALPHA||saved[2]==D3DBLEND_ONE));
   DWORD source=over?D3DBLEND_ONE:D3DBLEND_ZERO,destination=over?D3DBLEND_INVSRCALPHA:D3DBLEND_ONE;
   // Native erase/alpha-mask modes attenuate existing premultiplied RGBA.
   if(saved[1]&&saved[4]==D3DBLENDOP_ADD&&saved[2]==D3DBLEND_ZERO&&(saved[3]==D3DBLEND_SRCALPHA||saved[3]==D3DBLEND_INVSRCALPHA))destination=saved[3];
   dev->SetRenderState(D3DRS_SRCBLENDALPHA,source);
   dev->SetRenderState(D3DRS_DESTBLENDALPHA,destination);
   dev->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
  }else dev->SetRenderState(D3DRS_COLORWRITEENABLE,0); // mask-only pass: stencil retained, no new colour/coverage.

  ++frameDraws;if(++captured<=4)BVR_LOG("[aa HUD] isolated UI draw %u, private depth/stencil, LEFT only",frameDraws);
 }
 void restore(){if(!rt)return;d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,rt.Get());d->SetDepthStencilSurface(ds.Get());d->SetViewport(&viewport);d->SetScissorRect(&scissor);for(int i=0;i<9;++i)d->SetRenderState(blendStates[i],saved[i]);changed=false;}
 ~Scope(){if(changed)restore();if(d==device&&currentEye)draw_chunk_done();}
};
HRESULT WINAPI draw(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT start,UINT count){if(d==device&&fxstep9::hide(d,currentEye!=0&&!boundary,count))return S_OK;transitions9::Draw data{};data.type=t;data.count=count;data.start=start;HRESULT markerResult{};if(d==device&&boundary&&currentEye&&markers9::draw(d,boundary,count,[&]{return dp(d,t,start,count);},markerResult))return markerResult;Scope s(d,t,count);transitions9::Scope f(d,data,boundary,s.skip);billboard9::Scope b(d,!s.skip&&!f.skip&&currentEye!=0);xform9::Scope x(d,!s.skip&&!f.skip&&currentEye!=0&&!boundary);return s.skip||f.skip?S_OK:dp(d,t,start,count);}
HRESULT WINAPI indexed(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,INT base,UINT min,UINT num,UINT start,UINT count){if(d==device&&fxstep9::hide(d,currentEye!=0&&!boundary,count))return S_OK;transitions9::Draw data{};data.type=t;data.count=count;data.base=base;data.min=min;data.num=num;data.indexStart=start;data.indexed=true;HRESULT markerResult{};if(d==device&&boundary&&currentEye&&markers9::draw(d,boundary,count,[&]{return dip(d,t,base,min,num,start,count);},markerResult))return markerResult;Scope s(d,t,count);transitions9::Scope f(d,data,boundary,s.skip);billboard9::Scope b(d,!s.skip&&!f.skip&&currentEye!=0);xform9::Scope x(d,!s.skip&&!f.skip&&currentEye!=0&&!boundary);return s.skip||f.skip?S_OK:dip(d,t,base,min,num,start,count);}
HRESULT WINAPI draw_up(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT count,const void*p,UINT stride){if(d==device&&fxstep9::hide(d,currentEye!=0&&!boundary,count))return S_OK;transitions9::Draw data{};data.type=t;data.count=count;data.vertices=p;data.stride=stride;HRESULT markerResult{};if(d==device&&boundary&&currentEye&&markers9::draw(d,boundary,count,[&]{return up(d,t,count,p,stride);},markerResult))return markerResult;Scope s(d,t,count);transitions9::Scope f(d,data,boundary,s.skip);billboard9::Scope b(d,!s.skip&&!f.skip&&currentEye!=0);xform9::Scope x(d,!s.skip&&!f.skip&&currentEye!=0&&!boundary);return s.skip||f.skip?S_OK:up(d,t,count,p,stride);}
HRESULT WINAPI indexed_up(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT min,UINT num,UINT count,const void*idx,D3DFORMAT fmt,const void*p,UINT stride){if(d==device&&fxstep9::hide(d,currentEye!=0&&!boundary,count))return S_OK;transitions9::Draw data{};data.type=t;data.count=count;data.min=min;data.num=num;data.vertices=p;data.stride=stride;data.indices=idx;data.format=fmt;data.indexed=true;HRESULT markerResult{};if(d==device&&boundary&&currentEye&&markers9::draw(d,boundary,count,[&]{return iup(d,t,min,num,count,idx,fmt,p,stride);},markerResult))return markerResult;Scope s(d,t,count);transitions9::Scope f(d,data,boundary,s.skip);billboard9::Scope b(d,!s.skip&&!f.skip&&currentEye!=0);xform9::Scope x(d,!s.skip&&!f.skip&&currentEye!=0&&!boundary);return s.skip||f.skip?S_OK:iup(d,t,min,num,count,idx,fmt,p,stride);}
HRESULT WINAPI clear_hook(IDirect3DDevice9*d,DWORD count,const D3DRECT*rects,DWORD flags,D3DCOLOR colour,float depth,DWORD value){
 if(d==device&&currentEye&&!boundary&&(flags&D3DCLEAR_ZBUFFER)&&sceneDepth){
  ComPtr<IDirect3DSurface9> bound;d->GetDepthStencilSurface(&bound);
  if(same(bound.Get(),sceneDepth.Get())){freeze_depth();depthDrawWeight=0;}
 }
 if(d==device&&currentEye&&boundary&&on_backbuffer(d)){
  if(flags&D3DCLEAR_TARGET){boundary=false;frameDraws=0;cleared=false;}
  else if(currentEye<0&&cleared&&stencil){ComPtr<IDirect3DSurface9> original;d->GetDepthStencilSurface(&original);d->SetDepthStencilSurface(stencil.Get());clear(d,count,rects,flags,colour,depth,value);d->SetDepthStencilSurface(original.Get());}
 }
 return clear(d,count,rects,flags,colour,depth,value);
}
}
bool install(IDirect3DDevice9*d){
 auto**v=*reinterpret_cast<void***>(d);ComPtr<IDirect3DStateBlock9> probe;
 if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&probe)))return false;
 void* blockApply=(*reinterpret_cast<void***>(probe.Get()))[5];
 const int slots[]={81,82,83,84,43,39,37,47,57};
 void*hooks[]={(void*)draw,(void*)indexed,(void*)draw_up,(void*)indexed_up,(void*)clear_hook,(void*)depth_hook,(void*)target_hook,(void*)viewport_hook,(void*)state_hook};
 void**originals[]={(void**)&dp,(void**)&dip,(void**)&up,(void**)&iup,(void**)&clear,(void**)&setDepth,(void**)&setTarget,(void**)&setViewport,(void**)&setState};
 bool ok=true;for(int i=0;i<9;++i){if(MH_CreateHook(v[slots[i]],hooks[i],originals[i])!=MH_OK){ok=false;break;}MH_QueueEnableHook(v[slots[i]]);}
 if(ok){ok=MH_CreateHook(blockApply,(void*)apply_hook,(void**)&applyBlock)==MH_OK;if(ok)MH_QueueEnableHook(blockApply);}
 if(ok)ok=MH_ApplyQueued()==MH_OK;
 if(!ok){for(int slot:slots)MH_QueueDisableHook(v[slot]);MH_QueueDisableHook(blockApply);MH_ApplyQueued();return false;}
 device=d;swprintf_s(settings,L"%s\\aa_rendering.ini",log::data_dir());fastTracking=GetPrivateProfileIntW(L"AsylumVR",L"FastSceneTracking",1,settings)!=0;
 invalidate();if(!transitions9::install(d))return false;BVR_LOG("[aa HUD] owned D3D9 final-blit extraction installed; live binding cache observes setters/stateblock Apply, private filters preserved");return true;
}
void begin_eye(int eye,uint32_t gen){end_render();eyesame9::begin_eye(eye,gen);transitions9::begin_eye(eye,gen);markers9::begin_eye(eye,gen);depthConsumer=bridge::depth_required();invalidate();bridge::invalidate_depth();sceneDepth.Reset();depthDrawWeight=frozenDepthWeight=0;depthFrozen=false;if(eye<0)leftReady=false;currentEye=eye;currentGeneration=gen;boundary=false;cleared=false;frameDraws=0;if(device)device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer);if(eye&&backbuffer){D3DSURFACE_DESC d{};backbuffer->GetDesc(&d);xform9::begin_eye(eye,gen,d.Width,d.Height);fxstep9::begin_eye(eye,gen,d.Width,d.Height);detail9::begin_scene(d.Width,0);begin_budget(eye);}}
void end_render(){
 finish_chunk();
 if(renderAt){renderCpu[renderEye].add(cpu_now()-renderAt);renderAt=0;}
 if(activeSample>=0){auto& s=gpuSamples[activeSample];const auto a=s.end->Issue(D3DISSUE_END),b=s.disjoint->Issue(D3DISSUE_END);s.pending=SUCCEEDED(a)&&SUCCEEDED(b);if(!s.pending)++gpuRejected;activeSample=-1;}
}
void end_present(){end_render();invalidate();detail9::end_scene();if(currentEye<0){leftReady=boundary&&cleared&&frameDraws>0;leftGeneration=currentGeneration;}currentEye=0;boundary=cleared=false;frameDraws=0;}
void reset(){end_present();transitions9::reset();markers9::reset();for(auto& s:gpuSamples)s=GpuSample{};for(auto& v:slowChunks)v.clear();for(auto& item:shaderStamps)item=ShaderStamp{};shaderStampNext=timelineOverflow=timelineRejected=0;gpuAttempted=gpuSupported=samplePair=false;nextGpuSample=0;renderCpu[0]=renderCpu[1]=renderGpu[0]=renderGpu[1]={};leftReady=false;sceneDepth.Reset();capture.Reset();captureTexture.Reset();stencil.Reset();backbuffer.Reset();}
void mirror(){
 if(!device||!captureTexture||!backbuffer)return;
 ComPtr<IDirect3DStateBlock9> state;ComPtr<IDirect3DSurface9> rt,ds;
 if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)))return;
 device->GetRenderTarget(0,&rt);device->GetDepthStencilSurface(&ds);
 D3DSURFACE_DESC bd{};backbuffer->GetDesc(&bd);
 if(FAILED(device->BeginScene()))return;
 device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,backbuffer.Get());
 D3DVIEWPORT9 vp{0,0,bd.Width,bd.Height,0,1};device->SetViewport(&vp);
 device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);
 device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);device->SetTexture(0,captureTexture.Get());
 device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
 device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
 device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
 device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
 device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
 device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
 device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
 device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
 device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
 device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
 struct V{float x,y,z,w,u,v;};const float w=float(bd.Width)-.5f,h=float(bd.Height)-.5f;
 const V vertices[]={{-.5f,-.5f,0,1,0,0},{w,-.5f,0,1,1,0},{-.5f,h,0,1,0,1},{w,h,0,1,1,1}};
 device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V));device->EndScene();
 device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,rt.Get());device->SetDepthStencilSurface(ds.Get());state->Apply();
}
IDirect3DSurface9* sheet(){return currentEye<0&&boundary&&cleared&&frameDraws?capture.Get():nullptr;}
uint32_t generation(){return currentGeneration;}
int eye(){return currentEye;}
bool fast_tracking(){return fastTracking;}
void set_fast_tracking(bool enabled,bool persist){fastTracking=enabled;invalidate();if(persist&&settings[0])WritePrivateProfileStringW(L"AsylumVR",L"FastSceneTracking",enabled?L"1":L"0",settings);BVR_LOG("[aa HUD] scene tracking cache %s",enabled?"ON":"OFF (live queries every draw)");}
void status(){collect_gpu();BVR_LOG("[aa HUD] boundaries %u captured %u rightSkipped %u unsupported %u; tracking cache=%u classified=%u bindings=%u states=%u stateblockRestores=%u classifier CPU %.3f ms (heartbeat totals, includes depth transfer)",boundaries.load(),captured.load(),skipped.load(),unsupported.load(),fastTracking.load(),classified,bindingReads,stateReads,blockRestores,classifierMs);classified=bindingReads=stateReads=blockRestores=0;classifierMs=0;
 BVR_LOG("[aa budget] scene RT begin -> D3D9 Present entry CPU avg/max ms LEFT %.3f/%.3f (%u) RIGHT %.3f/%.3f (%u); GPU timestamp elapsed LEFT %.3f/%.3f (%u) RIGHT %.3f/%.3f (%u), supported=%u skipped=%u rejected=%u; includes scene/HUD/depth interception, excludes final colour/XR/DLSS transport; GPU elapsed can include queue starvation/other queue interference, not pure busy time",renderCpu[0].mean(),renderCpu[0].peak,renderCpu[0].count,renderCpu[1].mean(),renderCpu[1].peak,renderCpu[1].count,renderGpu[0].mean(),renderGpu[0].peak,renderGpu[0].count,renderGpu[1].mean(),renderGpu[1].peak,renderGpu[1].count,gpuSupported,gpuSkipped,gpuRejected);
 for(int eye=0;eye<2;++eye){for(const auto& c:slowChunks[eye])BVR_LOG("[aa scene timeline] eye %c intercepted draws %u..%u primitives %u firstRT %ux%u format %u firstPS %016llX Z=%u blend=%u CPU %.3fms GPUelapsed %.3fms; up to64 calls/chunk, first-state metadata, mixed passes possible",eye?'R':'L',c.first,c.first+c.draws-1,c.primitives,c.width,c.height,c.format,c.shader,c.z,c.blend,c.cpuMs,c.gpuMs);slowChunks[eye].clear();}
 BVR_LOG("[aa scene timeline] 2 sampled pairs/s, top8 slow chunks/eye,4096 intercepted draws/eye cap; overflow groups %u rejected chunks %u; collector no flush/wait",timelineOverflow,timelineRejected);timelineOverflow=timelineRejected=0;
 renderCpu[0]=renderCpu[1]=renderGpu[0]=renderGpu[1]={};gpuSkipped=gpuRejected=0;
}
void draw_ui(){ImGui::Text("HUD captured draws %u / boundaries %u",captured.load(),boundaries.load());bool enabled=fast_tracking();if(ImGui::Checkbox("Reduce scene tracking overhead",&enabled))set_fast_tracking(enabled);}
bool fixture_alpha(IDirect3DDevice9*d,const wchar_t*directory){
 if(d!=device)return false;ComPtr<IDirect3DStateBlock9>state;ComPtr<IDirect3DSurface9>originalRt,originalDs,bb;D3DVIEWPORT9 vp{};if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&state))||FAILED(d->GetRenderTarget(0,&originalRt))||FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))||FAILED(d->GetViewport(&vp)))return false;d->GetDepthStencilSurface(&originalDs);
 struct Restore{IDirect3DDevice9*d;IDirect3DStateBlock9*s;IDirect3DSurface9*rt,*ds;D3DVIEWPORT9 vp;~Restore(){reset();d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,rt);d->SetDepthStencilSurface(ds);d->SetViewport(&vp);s->Apply();}}restore{d,state.Get(),originalRt.Get(),originalDs.Get(),vp};
 auto load=[&](const wchar_t*name,std::vector<DWORD>&code){wchar_t path[MAX_PATH]{};swprintf_s(path,L"%s\\%s.bin",directory,name);FILE*f=_wfopen(path,L"rb");if(!f)return false;fseek(f,0,SEEK_END);const long n=ftell(f);rewind(f);if(n<=0||n%4||n>32768){fclose(f);return false;}code.resize(n/4);const bool ok=fread(code.data(),1,n,f)==n;fclose(f);return ok;};
 std::vector<DWORD>v,p;if(!load(L"ui-clip-vs",v)||!load(L"ui-constant-ps",p))return false;ComPtr<IDirect3DVertexShader9>vs;ComPtr<IDirect3DPixelShader9>ps;ComPtr<IDirect3DVertexDeclaration9>decl;if(FAILED(d->CreateVertexShader(v.data(),&vs))||FAILED(d->CreatePixelShader(p.data(),&ps)))return false;
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};if(FAILED(d->CreateVertexDeclaration(elements,&decl)))return false;D3DSURFACE_DESC size{};bb->GetDesc(&size);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,bb.Get());D3DVIEWPORT9 full{0,0,size.Width,size.Height,0,1};d->SetViewport(&full);d->SetVertexDeclaration(decl.Get());d->SetVertexShader(vs.Get());d->SetPixelShader(ps.Get());d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
 struct V{float x,y,z,w,u,v;};const V quad[]={{-.5f,.5f,0,1,0,0},{.5f,.5f,0,1,1,0},{-.5f,-.5f,0,1,0,1},{.5f,-.5f,0,1,1,1}};
 auto paint=[&](DWORD src,DWORD dst,const float*colour,DWORD writes=15,bool old=false){d->SetRenderState(D3DRS_SRCBLEND,src);d->SetRenderState(D3DRS_DESTBLEND,dst);d->SetRenderState(D3DRS_COLORWRITEENABLE,writes);d->SetPixelShaderConstantF(0,colour,1);if(FAILED(d->BeginScene()))return false;HRESULT hr{};{Scope scope(d,D3DPT_TRIANGLESTRIP,2);if(!scope.changed)return false;if(old){d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_INVSRCALPHA);}hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V));}d->EndScene();return SUCCEEDED(hr);};
 auto pixel=[&](BYTE out[4]){D3DSURFACE_DESC desc{};capture->GetDesc(&desc);ComPtr<IDirect3DSurface9>cpu;if(FAILED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr))||FAILED(d->GetRenderTargetData(capture.Get(),cpu.Get())))return false;D3DLOCKED_RECT r{};if(FAILED(cpu->LockRect(&r,nullptr,D3DLOCK_READONLY)))return false;memcpy(out,static_cast<BYTE*>(r.pBits)+(desc.Height/2)*r.Pitch+(desc.Width/2)*4,4);cpu->UnlockRect();return true;};
 unsigned cases{},oldOpaque{};const float neutral[]={.5f,.5f,.5f,.75f},base[]={.4f,.6f,.2f,.5f},black[]={0,0,0,.5f},glow[]={.1f,.2f,.3f,.75f};
 const DWORD pairs[4][2]={{D3DBLEND_DESTCOLOR,D3DBLEND_ZERO},{D3DBLEND_ZERO,D3DBLEND_SRCCOLOR},{D3DBLEND_ZERO,D3DBLEND_INVSRCALPHA},{D3DBLEND_ZERO,D3DBLEND_SRCALPHA}};
 auto begin=[&](){begin_eye(-1,7000+cases);boundary=true;cleared=false;leftReady=false;};
 for(const auto&pair:pairs){begin();BYTE fixed[4]{},old[4]{};if(!paint(pair[0],pair[1],neutral)||!pixel(fixed)||fixed[3]!=0)return false;begin();if(!paint(pair[0],pair[1],neutral,15,true)||!pixel(old)||old[3]<190)return false;++oldOpaque;++cases;}
 begin();BYTE value[4]{};if(!paint(D3DBLEND_ONE,D3DBLEND_ONE,glow)||!pixel(value)||value[3]!=0||value[0]<75||value[2]<25)return false;++cases;
 begin();if(!paint(D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,base)||!pixel(value)||std::abs(int(value[3])-128)>1||std::abs(int(value[2])-51)>1)return false;++cases;
 if(!paint(D3DBLEND_DESTCOLOR,D3DBLEND_ZERO,neutral)||!pixel(value)||std::abs(int(value[3])-128)>1||std::abs(int(value[2])-26)>1)return false;++cases;
 for(DWORD dst:{DWORD(D3DBLEND_INVSRCALPHA),DWORD(D3DBLEND_SRCALPHA)}){begin();if(!paint(D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,base)||!paint(D3DBLEND_ZERO,dst,black)||!pixel(value)||std::abs(int(value[3])-64)>1||std::abs(int(value[2])-26)>1)return false;++cases;}
 for(DWORD writes:{DWORD(0),DWORD(D3DCOLORWRITEENABLE_ALPHA)}){begin();if(!paint(D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,neutral,writes)||!pixel(value)||value[3]!=0)return false;++cases;}
 printf("HUD blend GPU: %u production capture-hook cases; multiply/erase/mask leave empty sheet transparent, %u old controls add opaque black coverage; additive RGB glow/normal OVER preserved, erase/mask attenuate existing RGBA, stencil+alpha-only masks add no coverage PASS\n",cases,oldOpaque);return true;
}
bool fixture_tracking(IDirect3DDevice9* d,D3DPRIMITIVETYPE type,const void* vertices,UINT stride,IDirect3DSurface9* privateTarget){
 // Called within actual scene geometry before its depth is cleared.
 constexpr unsigned count=256;bool ok=true;double elapsed[2]{},classifier[2]{};unsigned reads[2]{};
 for(int phase=0;phase<2;++phase){set_fast_tracking(phase!=0,false);const auto before=bindingReads;const double classifiedAt=classifierMs,at=cpu_now();
  for(unsigned i=0;i<count;++i)if(FAILED(d->DrawPrimitiveUP(type,2,vertices,stride)))ok=false;
  elapsed[phase]=cpu_now()-at;classifier[phase]=classifierMs-classifiedAt;reads[phase]=bindingReads-before;
 }
 ok=reads[0]>=count&&reads[1]==1&&ok;
 ComPtr<IDirect3DStateBlock9> saved;ComPtr<IDirect3DSurface9> expected=sceneDepth;
 const auto restores=blockRestores;
 ok=SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&saved))&&ok;
 d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,privateTarget);D3DVIEWPORT9 vp{0,0,16,16,0,1};d->SetViewport(&vp);
 live_bindings();ok=bindings.valid&&!bindings.main&&bindings.rtDesc.Width==16&&ok;
 // RT/DS are outside D3D9 stateblocks; restore them explicitly like the
 // desktop mirror does. Prime a stale viewport/Z snapshot before Apply.
 d->SetRenderTarget(0,backbuffer.Get());d->SetDepthStencilSurface(expected.Get());d->SetViewport(&vp);
 d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
 live_bindings();live_states();ok=!cachedZ&&!cachedWrite&&bindings.vp.Width==16&&ok;
 if(saved)ok=SUCCEEDED(saved->Apply())&&ok;
 live_states();live_bindings();ok=blockRestores==restores+1&&bindings.depthValid&&same(bindings.ds.Get(),expected.Get())&&bindings.rtDesc.Width==bindings.bbDesc.Width&&bindings.vp.Width==bindings.bbDesc.Width&&cachedZ&&cachedWrite&&ok;
 ok=SUCCEEDED(d->DrawPrimitiveUP(type,2,vertices,stride))&&ok;
 printf("D3D9 tracking GPU: %u unchanged scene draws: uncached %u binding refreshes %.3fms classifier / %.3fms draw submission, cached %u %.3fms / %.3fms; private target and StateBlock Apply restore exact live depth/bindings %s\n",count,reads[0],classifier[0],elapsed[0],reads[1],classifier[1],elapsed[1],ok?"PASS":"FAIL");
 set_fast_tracking(true,false);return ok;
}
bool fixture_budget(){collect_gpu();const bool ok=gpuSupported&&renderGpu[0].count&&renderGpu[1].count&&renderGpu[0].total>0&&renderGpu[1].total>0&&!gpuRejected&&!slowChunks[0].empty()&&!slowChunks[1].empty()&&!timelineRejected;printf("D3D9 scene budget GPU: nonblocking valid LEFT/RIGHT timestamp results %.3f/%.3fms and ranked64-draw GPU chunks, no flush polling or rejected samples %s\n",renderGpu[0].mean(),renderGpu[1].mean(),ok?"PASS":"FAIL");return ok;}
}
