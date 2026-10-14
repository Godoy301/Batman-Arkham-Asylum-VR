#include "billboard9.h"
#include "xform9.h"
#include "core/util/log.h"
#include "dlss.h"
#include <MinHook.h>
#include <imgui.h>
#include <wrl/client.h>
#include <array>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <d3dcompiler.h>
#include "stereo_math.h"
#include "hud9.h"
#include "transitions9.h"
#include "markers9.h"
using Microsoft::WRL::ComPtr;
namespace bvr::aa::billboard9 {
namespace {
IDirect3DDevice9* owned{};
using CreateShader=HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**);
using SetShader=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DVertexShader9*);
CreateShader createShader{};SetShader setShader{};
struct Layout {int right{-1},up{-1},position{-1};bool sprite{};};
std::unordered_map<IDirect3DVertexShader9*,Layout> shaders;SRWLOCK shaderLock=SRWLOCK_INIT;
constexpr size_t maxShaders=4096;
Layout current{};bool shaderDirty{true};
std::atomic<bool> on{true};wchar_t settings[MAX_PATH]{};
std::atomic<unsigned> named{},patched{},rejected{},missingPass{},failures{},evictions{},directAxes{},swappedAxes{};
std::atomic<unsigned> menuSkipped{},unknownSkipped{};
struct Pass {uint32_t serial{};bool valid{};menu_state::Mode mode{};float eye[2][3]{},game[2][3]{},positionDelta[3]{};};
Pass passes[64];SRWLOCK passLock=SRWLOCK_INIT;uint32_t renderSlot{};bool renderValid{};
uint16_t word(const BYTE* p){uint16_t v;memcpy(&v,p,2);return v;}
uint32_t dword(const BYTE* p){uint32_t v;memcpy(&v,p,4);return v;}
// DX9 CTAB offsets are relative to the header following the fourcc. Only the
// bounded comment payload is inspected; no engine offsets/register guesses.
Layout table(const BYTE* base,size_t bytes){
 Layout out;if(bytes<28||dword(base)!=28||dword(base+8)!=0xfffe0300)return out;
 const unsigned count=dword(base+12),info=dword(base+16);if(count>256||info>bytes||count>(bytes-info)/20)return out;
 bool scalars=false,local=false,alignment=false;
 for(unsigned i=0;i<count;++i){const BYTE* c=base+info+i*20;const unsigned at=dword(c),reg=word(c+6),n=word(c+8);
  if(at>=bytes||word(c+4)!=2||reg>=256||n>256-reg)continue;const char* name=reinterpret_cast<const char*>(base+at);if(!memchr(name,0,bytes-at))continue;
  if(!strcmp(name,"CameraRight")&&n==1)out.right=int(reg);
  else if(!strcmp(name,"CameraUp")&&n==1)out.up=int(reg);
  else if(!strcmp(name,"CameraWorldPosition")&&n==1)out.position=int(reg);
  else if(!strcmp(name,"ParticleUpRightResultScalars"))scalars=true;
  else if(!strcmp(name,"LocalToWorld")&&n==4)local=true;
  else if(!strcmp(name,"ScreenAlignment"))alignment=true;
 }
 out.sprite=scalars&&local&&alignment&&out.right>=0&&out.up>=0&&out.right!=out.up;
 if(!out.sprite)return {};return out;
}
Layout parse(const DWORD* code){
 Layout out;if(!code)return out;
 __try{
  if(code[0]!=0xfffe0300)return out;
  for(size_t i=1;i<65536;){const DWORD token=code[i];const unsigned op=token&0xffff;if(op==0xffff)return out;
   if(op==0xfffe){const unsigned n=(token>>16)&0x7fff;if(n>65536-i-1)return out;if(n>1&&code[i+1]==MAKEFOURCC('C','T','A','B'))return table(reinterpret_cast<const BYTE*>(code+i+2),size_t(n-1)*4);i+=n+1;}
   else i+=1+((token>>24)&15);
  }
 }__except(EXCEPTION_EXECUTE_HANDLER){return {};}
 return out;
}
void cache(IDirect3DVertexShader9* shader,const Layout& layout){
 if(!shader)return;AcquireSRWLockExclusive(&shaderLock);
 if(shaders.size()>=maxShaders&&!shaders.contains(shader)){shaders.erase(shaders.begin());++evictions;}
 shaders[shader]=layout;ReleaseSRWLockExclusive(&shaderLock);
 if(layout.sprite)++named;
}
Layout lookup(IDirect3DVertexShader9* shader){
 if(!shader)return {};AcquireSRWLockShared(&shaderLock);const auto found=shaders.find(shader);const bool got=found!=shaders.end();Layout l=got?found->second:Layout{};ReleaseSRWLockShared(&shaderLock);if(got)return l;
 UINT bytes{};if(FAILED(shader->GetFunction(nullptr,&bytes))||bytes<8||bytes>256*1024||bytes%4)return {};
 std::vector<DWORD> code(bytes/4);if(SUCCEEDED(shader->GetFunction(code.data(),&bytes)))l=parse(code.data());cache(shader,l);return l;
}
HRESULT WINAPI create(IDirect3DDevice9* d,const DWORD* code,IDirect3DVertexShader9** out){const auto h=createShader(d,code,out);if(d==owned&&SUCCEEDED(h)&&out&&*out){cache(*out,parse(code));transitions9::vertex_created(*out);markers9::vertex_created(*out);xform9::vertex_created(*out);}return h;}
HRESULT WINAPI set(IDirect3DDevice9* d,IDirect3DVertexShader9* shader){const auto h=setShader(d,shader);if(d==owned&&SUCCEEDED(h)){current=lookup(shader);shaderDirty=false;}return h;}
float dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool axes(const float r[4],const float u[4],const Pass& p,float& sr,float& su,unsigned& rightAxis){
 for(unsigned i=0;i<4;++i)if(!std::isfinite(r[i])||!std::isfinite(u[i]))return false;
 if(std::fabs(dot(r,r)-1)>.02f||std::fabs(dot(u,u)-1)>.02f||std::fabs(dot(r,u))>.02f)return false;
 // Asylum's actual particle binder (0xb540a0) uploads normalized NEGATIVE
 // inverse-VP rows 1/0 as CameraRight/Up, not rows 0/1. Respect that ordering
 // and each sign; never substitute conventional axes by shader name alone.
 for(unsigned axis=0;axis<2;++axis){sr=dot(r,p.eye[axis]);su=dot(u,p.eye[axis^1]);
  if(std::fabs(std::fabs(sr)-1)<.003f&&std::fabs(std::fabs(su)-1)<.003f){rightAxis=axis;return true;}}
 return false;
}
}
Scope::Scope(IDirect3DDevice9* d,bool nativeDraw){
 if(!nativeDraw||d!=owned||!on||!renderValid)return;
 if(shaderDirty){ComPtr<IDirect3DVertexShader9> shader;if(FAILED(d->GetVertexShader(&shader)))return;current=lookup(shader.Get());shaderDirty=false;}
 if(!current.sprite)return;
 Pass p;AcquireSRWLockShared(&passLock);p=passes[renderSlot%64];ReleaseSRWLockShared(&passLock);
 if(!p.valid||p.serial!=renderSlot+1){++missingPass;return;}
 if(p.mode!=menu_state::Mode::gameplay){if(p.mode==menu_state::Mode::menu)++menuSkipped;else ++unknownSkipped;return;}
 right=current.right;up=current.up;position=current.position;
 if(FAILED(d->GetVertexShaderConstantF(right,saved[0],1))||FAILED(d->GetVertexShaderConstantF(up,saved[1],1))){++failures;return;}
 float sr{},su{};unsigned rightAxis{};if(!axes(saved[0],saved[1],p,sr,su,rightAxis)){
  if(rejected.fetch_add(1)<4)BVR_LOG("[aa particles] facing guard slot %u regs %d/%d: R %.5f %.5f %.5f U %.5f %.5f %.5f; eyeR %.5f %.5f %.5f eyeU %.5f %.5f %.5f",
   renderSlot,right,up,saved[0][0],saved[0][1],saved[0][2],saved[1][0],saved[1][1],saved[1][2],p.eye[0][0],p.eye[0][1],p.eye[0][2],p.eye[1][0],p.eye[1][1],p.eye[1][2]);return;}
 float corrected[3][4];memcpy(corrected,saved,sizeof(saved));
 for(unsigned i=0;i<3;++i){corrected[0][i]=p.game[rightAxis][i]*(sr<0?-1:1);corrected[1][i]=p.game[rightAxis^1][i]*(su<0?-1:1);}
 if(position>=0){if(FAILED(d->GetVertexShaderConstantF(position,saved[2],1))){++failures;return;}memcpy(corrected[2],saved[2],16);for(unsigned i=0;i<4;++i)if(!std::isfinite(saved[2][i])){++rejected;return;}for(unsigned i=0;i<3;++i)corrected[2][i]+=p.positionDelta[i];}
 device=d;changed=true;
 const bool ok=SUCCEEDED(d->SetVertexShaderConstantF(right,corrected[0],1))&&SUCCEEDED(d->SetVertexShaderConstantF(up,corrected[1],1))&&(position<0||SUCCEEDED(d->SetVertexShaderConstantF(position,corrected[2],1)));
 if(ok){++patched;if(rightAxis)++swappedAxes;else ++directAxes;}else ++failures;
}
Scope::~Scope(){if(!changed)return;device->SetVertexShaderConstantF(right,saved[0],1);device->SetVertexShaderConstantF(up,saved[1],1);if(position>=0)device->SetVertexShaderConstantF(position,saved[2],1);}
bool install(IDirect3DDevice9* d){
 if(owned)return owned==d;owned=d;auto** v=*reinterpret_cast<void***>(d);
 if(MH_CreateHook(v[91],reinterpret_cast<void*>(create),reinterpret_cast<void**>(&createShader))!=MH_OK||MH_CreateHook(v[92],reinterpret_cast<void*>(set),reinterpret_cast<void**>(&setShader))!=MH_OK){owned=nullptr;return false;}
 MH_QueueEnableHook(v[91]);MH_QueueEnableHook(v[92]);if(MH_ApplyQueued()!=MH_OK){owned=nullptr;return false;}
 swprintf_s(settings,L"%s\\aa_rendering.ini",log::data_dir());on=GetPrivateProfileIntW(L"AsylumVR",L"StableParticles",1,settings)!=0;
 BVR_LOG("[aa particles] named DX9 sprite parameters: shared game-camera facing, real eye projection, per-draw restoration; default %u",on.load());return true;
}
void publish(uint32_t slot,const float* base,const float* eye,const float delta[3],menu_state::Mode mode){
 Pass p;p.serial=slot+1;p.valid=true;p.mode=mode;for(unsigned i=0;i<3;++i){p.game[0][i]=base[i*4];p.game[1][i]=base[i*4+1];p.eye[0][i]=eye[i*4];p.eye[1][i]=eye[i*4+1];p.positionDelta[i]=delta[i];}
 AcquireSRWLockExclusive(&passLock);passes[slot%64]=p;ReleaseSRWLockExclusive(&passLock);
}
void begin_render(uint32_t slot,bool nativeView){renderSlot=slot;renderValid=nativeView;}
void invalidate_shader(){shaderDirty=true;}
void reset(){renderValid=false;shaderDirty=true;current={};AcquireSRWLockExclusive(&shaderLock);shaders.clear();ReleaseSRWLockExclusive(&shaderLock);AcquireSRWLockExclusive(&passLock);for(auto& p:passes)p={};ReleaseSRWLockExclusive(&passLock);}
bool enabled(){return on;}
void set_enabled(bool value,bool persist){on=value;dlss::reset();if(persist&&settings[0])WritePrivateProfileStringW(L"AsylumVR",L"StableParticles",value?L"1":L"0",settings);}
void draw_ui(){bool value=on;if(ImGui::Checkbox("Stable smoke facing during gameplay",&value))set_enabled(value);}
void status_line(char* text,unsigned n){sprintf_s(text,n,"stableParticles=%u named=%u patched=%u axisMismatch=%u noPass=%u failures=%u direct=%u swapped=%u menuStock=%u unknownStock=%u",on.load(),named.load(),patched.load(),rejected.load(),missingPass.load(),failures.load(),directAxes.load(),swappedAxes.load(),menuSkipped.load(),unknownSkipped.load());}
void status(){char text[320];status_line(text,sizeof(text));BVR_LOG("[aa particles] %s; cache evictions %u",text,evictions.load());}
bool fixture(IDirect3DDevice9* d,const wchar_t* path){
 if(!install(d)||!menu_state::fixture())return false;FILE* f=_wfopen(path,L"rb");if(!f)return false;fseek(f,0,SEEK_END);const long size=ftell(f);rewind(f);if(size<8||size>256*1024||size%4){fclose(f);return false;}
 std::vector<DWORD> code(size/4);const bool read=fread(code.data(),1,size,f)==size;fclose(f);if(!read)return false;
 const Layout binding=parse(code.data());if(!binding.sprite||binding.position<0)return false;
 ComPtr<IDirect3DStateBlock9> state;ComPtr<IDirect3DSurface9> originalRt,originalDs;D3DVIEWPORT9 originalVp{};
 if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&state))||FAILED(d->GetRenderTarget(0,&originalRt))||FAILED(d->GetViewport(&originalVp)))return false;d->GetDepthStencilSurface(&originalDs);
 struct Restore{IDirect3DDevice9*d;IDirect3DStateBlock9*state;IDirect3DSurface9*rt,*ds;D3DVIEWPORT9 vp;~Restore(){hud9::end_present();d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,rt);d->SetDepthStencilSurface(ds);d->SetViewport(&vp);state->Apply();begin_render(0,false);set_enabled(true,false);}} restore{d,state.Get(),originalRt.Get(),originalDs.Get(),originalVp};
 ComPtr<IDirect3DVertexShader9> vs;ComPtr<IDirect3DPixelShader9> ps;ComPtr<ID3DBlob> bytes,error;
 const char* pixel="float4 main(float2 uv:TEXCOORD0):COLOR0{return float4(uv,1,1);}";
 if(FAILED(d->CreateVertexShader(code.data(),&vs))||FAILED(D3DCompile(pixel,strlen(pixel),nullptr,nullptr,nullptr,"main","ps_3_0",0,0,&bytes,&error))||FAILED(d->CreatePixelShader(static_cast<const DWORD*>(bytes->GetBufferPointer()),&ps)))return false;
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},{0,48,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BLENDWEIGHT,0},{0,64,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,80,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
 ComPtr<IDirect3DVertexDeclaration9> declaration;ComPtr<IDirect3DTexture9> texture;ComPtr<IDirect3DSurface9> rt,readback;
 if(FAILED(d->CreateVertexDeclaration(elements,&declaration))||FAILED(d->CreateTexture(128,128,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr))||FAILED(texture->GetSurfaceLevel(0,&rt))||FAILED(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr)))return false;
 d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,rt.Get());D3DVIEWPORT9 viewport{0,0,128,128,0,1};d->SetViewport(&viewport);d->SetVertexDeclaration(declaration.Get());d->SetVertexShader(vs.Get());d->SetPixelShader(ps.Get());d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
 struct Vertex{float centre[4],old[4],size[4],rotation[4],uv[4],colour[4];};
 Vertex vertices[4]{};for(unsigned i=0;i<4;++i){vertices[i].centre[2]=5;vertices[i].centre[3]=1;vertices[i].old[1]=-1;vertices[i].old[2]=5;vertices[i].old[3]=1;vertices[i].size[0]=2;vertices[i].size[1]=1;vertices[i].rotation[0]=.23f;vertices[i].uv[0]=i&1?1.f:0.f;vertices[i].uv[1]=i&2?1.f:0.f;for(float& c:vertices[i].colour)c=1;}
 auto image=[&](std::vector<BYTE>& out){if(FAILED(d->GetRenderTargetData(rt.Get(),readback.Get())))return false;D3DLOCKED_RECT m{};if(FAILED(readback->LockRect(&m,nullptr,D3DLOCK_READONLY)))return false;out.resize(128*128*4);for(unsigned y=0;y<128;++y)memcpy(out.data()+y*128*4,static_cast<const BYTE*>(m.pBits)+y*m.Pitch,128*4);readback->UnlockRect();return true;};
 unsigned checks{},controls{},oldGuardRejects{},targetCases{},routingChecks{};constexpr float rad=3.141592654f/180;
 const unsigned directBefore=directAxes,swappedBefore=swappedAxes;
 auto orientation=[&](float yaw,float pitch,float roll){vr::HeadPose q{};
  const float cy=std::cos(yaw*rad*.5f),sy=std::sin(yaw*rad*.5f),cx=std::cos(pitch*rad*.5f),sx=std::sin(pitch*rad*.5f),cz=std::cos(roll*rad*.5f),sz=std::sin(roll*rad*.5f);
  q.qw=cy*cx*cz+sy*sx*sz;q.qx=cy*sx*cz+sy*cx*sz;q.qy=sy*cx*cz-cy*sx*sz;q.qz=cy*cx*sz-sy*sx*cz;return q;};
 for(unsigned layout=0;layout<2;++layout)for(int alignment:{0,1})for(int gameTurn:{0,1})for(unsigned pose=0;pose<6;++pose)for(unsigned eye=0;eye<2;++eye){
  const auto game=orientation(gameTurn*27.f,gameTurn*11.f,gameTurn*7.f);const math::M base=math::pose_view(game,1);
  vr::HeadPose tracked{};tracked.qw=1;tracked.px=eye?.032f:-.032f;
  if(pose==1||pose==4){tracked.qy=std::sin(15*rad*.5f);tracked.qw=std::cos(15*rad*.5f);}
  if(pose==2){tracked.qz=std::sin(20*rad*.5f);tracked.qw=std::cos(20*rad*.5f);}
  if(pose==5){tracked=orientation(12,-18,14);tracked.px=eye?.032f:-.032f;}
  if(pose==3||pose==4){tracked.px+=.24f;tracked.py=-.1f;}
  const math::M view=math::mul(base,math::pose_view(tracked,1));math::M baseInverse{},eyeInverse{};if(!math::inverse(base,baseInverse)||!math::inverse(view,eyeInverse))return false;
  math::M projection{};projection.v[0][0]=projection.v[1][1]=1;projection.v[2][2]=1.01f;projection.v[2][3]=1;projection.v[3][2]=-.1f;const auto vp=math::mul(view,projection);
  float delta[3]{};const float local[]={tracked.px,tracked.py,-tracked.pz};for(int i=0;i<3;++i)for(int j=0;j<3;++j)delta[i]-=local[j]*baseInverse.v[j][i];
  const uint32_t slot=777+checks;publish(slot,&base.v[0][0],&view.v[0][0],delta,menu_state::Mode::gameplay);begin_render(slot,true);hud9::begin_eye(-1,slot);
  std::array<float,256*4> constants{};memcpy(constants.data(),&vp,64);
  // This target's original shader positions particles in world space.
  const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};memcpy(constants.data()+5*4,identity,64);
  const float axis0[]={0,1,0,1},axis1[]={1,0,0,1};memcpy(constants.data()+9*4,axis0,16);memcpy(constants.data()+10*4,axis1,16);
  const unsigned rightAxis=layout?1:0,upAxis=rightAxis^1;const float sign=layout?-1.f:1.f;
  for(int i=0;i<3;++i){constants[4*4+i]=eyeInverse.v[3][i];constants[binding.position*4+i]=eyeInverse.v[3][i];constants[binding.right*4+i]=sign*view.v[i][rightAxis];constants[binding.up*4+i]=sign*view.v[i][upAxis];}constants[4*4+3]=1;
  // Reproduce the SHIPPED binder independently: normalize inverse-VP rows,
  // then negate and upload row1 as right, row0 as up. A1u never tested this.
  auto targetFacing=[&](auto& data,const math::M& matrix){math::M inverseVp{};if(!math::inverse(matrix,inverseVp))return false;
   for(unsigned row=0;row<2;++row){const float length=std::sqrt(dot(inverseVp.v[row],inverseVp.v[row]));if(length<1e-6f)return false;
    const int reg=row?binding.right:binding.up;for(unsigned i=0;i<3;++i)data[reg*4+i]=-inverseVp.v[row][i]/length;data[reg*4+3]=0;}return true;};
  if(layout&&!targetFacing(constants,vp))return false;
  if(layout){++targetCases;float rr{},uu{};for(int i=0;i<3;++i){rr+=constants[binding.right*4+i]*view.v[i][0];uu+=constants[binding.up*4+i]*view.v[i][1];}
   if(std::fabs(std::fabs(rr)-1)>=.003f||std::fabs(std::fabs(uu)-1)>=.003f)++oldGuardRejects;else return false;}
  constants[14*4]=0;constants[15*4]=0;constants[16*4+(alignment?2:0)]=1;constants[17*4+2]=7;constants[17*4+3]=1;
  // Render a reference with game-facing geometry but the REAL eye VP. This
  // keeps head motion/stereo in the picture rather than freezing the view.
  auto reference=constants;for(int i=0;i<3;++i){reference[binding.right*4+i]=sign*base.v[i][rightAxis];reference[binding.up*4+i]=sign*base.v[i][upAxis];reference[binding.position*4+i]=baseInverse.v[3][i];}
  if(layout&&!targetFacing(reference,math::mul(base,projection)))return false;
  auto draw=[&](const auto& data,bool enabled,std::vector<BYTE>& out){set_enabled(enabled,false);if(FAILED(d->SetVertexShaderConstantF(0,data.data(),18))||FAILED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0))||FAILED(d->BeginScene())||FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)))||FAILED(d->EndScene()))return false;return image(out);};
  std::vector<BYTE> expected,corrected,old;if(!draw(reference,false,expected)||!draw(constants,true,corrected)||!draw(constants,false,old))return false;
  bool equal=true;for(size_t i=0;i<expected.size();++i)if(std::abs(int(expected[i])-int(corrected[i]))>1){equal=false;break;}
  if(!equal){printf("Particle GPU mismatch layout%u alignment%d gameTurn%d headPose%u eye%u\n",layout,alignment,gameTurn,pose,eye);return false;}
  if(pose&&old!=expected)++controls;
  std::array<float,18*4> restored{};if(FAILED(d->GetVertexShaderConstantF(0,restored.data(),18))||memcmp(restored.data(),constants.data(),sizeof(restored)))return false;
  // Same head pose and enabled preference: entering a menu must reproduce
  // STOCK pixels, gameplay restores A1v pixels, and unknown state is stock.
  for(const auto mode:{menu_state::Mode::menu,menu_state::Mode::gameplay,menu_state::Mode::unknown}){
   publish(slot,&base.v[0][0],&view.v[0][0],delta,mode);std::vector<BYTE> routed;
   if(!draw(constants,true,routed))return false;const auto& wanted=mode==menu_state::Mode::gameplay?expected:old;
   for(size_t i=0;i<wanted.size();++i)if(std::abs(int(wanted[i])-int(routed[i]))>1)return false;
   if(FAILED(d->GetVertexShaderConstantF(0,restored.data(),18))||memcmp(restored.data(),constants.data(),sizeof(restored)))return false;++routingChecks;
  }
  publish(slot,&base.v[0][0],&view.v[0][0],delta,menu_state::Mode::gameplay);
  ++checks;
 }
 if(!controls||targetCases!=48||oldGuardRejects!=targetCases||directAxes-directBefore!=96||swappedAxes-swappedBefore!=96||routingChecks!=288)return false;
 set_enabled(true,false);
 // Unrelated shader names, nonnative draws, foreign devices, stale tickets
 // and shadow/light camera axes must not receive the particle correction.
 std::vector<DWORD> unrelated=code;BYTE* content=reinterpret_cast<BYTE*>(unrelated.data());const char* name="ParticleUpRightResultScalars";bool renamed=false;for(size_t i=0;i+strlen(name)<size_t(size);++i)if(!memcmp(content+i,name,strlen(name))){content[i]='Q';renamed=true;break;}ComPtr<IDirect3DVertexShader9> other;if(!renamed||FAILED(d->CreateVertexShader(unrelated.data(),&other)))return false;d->SetVertexShader(other.Get());{Scope scope(d);if(scope.changed)return false;}
 d->SetVertexShader(vs.Get());{Scope scope(d,false);if(scope.changed)return false;}{Scope scope(nullptr);if(scope.changed)return false;}
 ComPtr<IDirect3DStateBlock9> savedShader;if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&savedShader)))return false;d->SetVertexShader(other.Get());if(FAILED(savedShader->Apply()))return false;
 const float axisRight[]={1,0,0,0},axisUp[]={0,1,0,0},zero[3]{};const float identityView[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};publish(4242,identityView,identityView,zero,menu_state::Mode::gameplay);begin_render(4242,true);d->SetVertexShaderConstantF(binding.right,axisRight,1);d->SetVertexShaderConstantF(binding.up,axisUp,1);{Scope scope(d);if(!scope.changed){ComPtr<IDirect3DVertexShader9> queried;d->GetVertexShader(&queried);printf("Particle stateblock guard failed: expectedVS%p actualVS%p sprite%u regs%d/%d dirty%u enabled%u slot%u rejected%u missing%u failures%u\n",vs.Get(),queried.Get(),current.sprite,current.right,current.up,shaderDirty,on.load(),renderSlot,rejected.load(),missingPass.load(),failures.load());return false;}}
 begin_render(123456,true);{Scope scope(d);if(scope.changed)return false;}
 begin_render(777,true);const float lightRight[]={.3f,.4f,.8660254f,0};d->SetVertexShaderConstantF(binding.right,lightRight,1);{Scope scope(d);if(scope.changed)return false;}
 printf("Particle GPU original Asylum shader: %u screen/axis-facing cases, BOTH eyes, yaw/pitch/roll/lean/changed game camera; direct AND target negative-swapped facing; true eye VP reference pixels within1LSB, authored spin kept; %u old-facing controls differ, A1u guard rejects ALL %u target-layout cases; all constants restore, stateblock shader refresh and unrelated/stale/light-view guards PASS\n",checks,controls,oldGuardRejects);
 printf("Particle gameplay-only GPU routing: %u enabled-preference menu/gameplay/unknown cases; menu and unknown reproduce stock pixels, gameplay reproduces A1v correction, constant restoration PASS\n",routingChecks);
 return true;
}
}
