// City D2 MIPBIAS method, implemented for this target's D3D9 state interface.
// Mild log2(input/output), no extra -1 sharpening. Restrict to mipmapped scene
// textures; post-process targets and HUD retain the game's sampler values.
#include "detail9.h"
#include "dlss.h"
#include "core/vr/openxr_runtime.h"
#include "core/util/log.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::detail9 {
namespace {
IDirect3DDevice9* device{};bool scene{},restoring{};float correction{};DWORD original[16]{};bool known[16]{},mipmapped[16]{};
std::atomic<bool> sharper{true};wchar_t settings[MAX_PATH]{};
std::atomic<unsigned> actualInput{};
using SetSampler=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD);
using GetSampler=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD*);
using SetTexture=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
SetSampler setSampler{};GetSampler getSampler{};SetTexture setTexture{};
bool lastApplied[16]{};
unsigned metadataReads{},stockTextureBinds{};
bool correctionEnabled(){return scene&&sharper&&dlss::mode()!=0;}
bool scene_texture(IDirect3DBaseTexture9* t){
 ++metadataReads;
 if(!t||t->GetType()!=D3DRTYPE_TEXTURE)return false;
 auto* tex=static_cast<IDirect3DTexture9*>(t);D3DSURFACE_DESC desc{};
 return tex->GetLevelCount()>1&&SUCCEEDED(tex->GetLevelDesc(0,&desc))&&!(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL));
}
void apply(DWORD stage){
 if(stage>=16||!known[stage])return;
 float bias{};memcpy(&bias,&original[stage],4);if(!std::isfinite(bias))return;
 const bool adjusted=scene&&sharper&&mipmapped[stage]&&dlss::mode();
 if(adjusted)bias+=correction;
 DWORD bits{};memcpy(&bits,&bias,4);setSampler(device,stage,D3DSAMP_MIPMAPLODBIAS,bits);lastApplied[stage]=adjusted;
}
HRESULT WINAPI set(IDirect3DDevice9*d,DWORD stage,D3DSAMPLERSTATETYPE type,DWORD value){
 if(d!=device||stage>=16||type!=D3DSAMP_MIPMAPLODBIAS)return setSampler(d,stage,type,value);
 original[stage]=value;known[stage]=true;apply(stage);return S_OK;
}
HRESULT WINAPI get(IDirect3DDevice9*d,DWORD stage,D3DSAMPLERSTATETYPE type,DWORD* value){
 if(d==device&&stage<16&&type==D3DSAMP_MIPMAPLODBIAS&&known[stage]&&value){*value=original[stage];return S_OK;}
 return getSampler(d,stage,type,value);
}
HRESULT WINAPI texture(IDirect3DDevice9*d,DWORD stage,IDirect3DBaseTexture9* t){
 const HRESULT hr=setTexture(d,stage,t);if(d!=device||stage>=16||FAILED(hr))return hr;
 // The stock path does not need texture descriptions, COM probes or another
 // sampler setter for every game texture bind. Restore any prior correction
 // before forwarding stock state; the engine's texture bind always happens.
 if(!correctionEnabled()){++stockTextureBinds;if(lastApplied[stage])apply(stage);mipmapped[stage]=false;return hr;}
 mipmapped[stage]=scene_texture(t);
 if(!known[stage]&&SUCCEEDED(getSampler(d,stage,D3DSAMP_MIPMAPLODBIAS,&original[stage])))known[stage]=true;
 apply(stage);return hr;
}
}
bool install(IDirect3DDevice9*d){
 auto**v=*reinterpret_cast<void***>(d);const int slots[]={69,68,65};void* hooks[]={(void*)set,(void*)get,(void*)texture};void** originals[]={(void**)&setSampler,(void**)&getSampler,(void**)&setTexture};
 for(int i=0;i<3;++i){if(MH_CreateHook(v[slots[i]],hooks[i],originals[i])!=MH_OK)return false;MH_QueueEnableHook(v[slots[i]]);}
 if(MH_ApplyQueued()!=MH_OK)return false;device=d;
 swprintf_s(settings,L"%s\\aa_rendering.ini",log::data_dir());sharper=GetPrivateProfileIntW(L"AsylumVR",L"SharperTextures",1,settings)!=0;
 BVR_LOG("[aa detail] mild scene mip bias; owned D3D9 sampler reads retain originals, no recursive correction");return true;
}
void begin_scene(unsigned sourceW,unsigned inputW){
 if(!device)return;uint32_t outW{},outH{};vr::recommended_eye_size(&outW,&outH);
 if(!inputW)inputW=actualInput.load();if(!inputW)inputW=sourceW;
 const wchar_t* p=wcsstr(GetCommandLineW(),L"-AAUpscaleX=");if(p)outW=wcstoul(p+12,nullptr,10);
 correction=outW&&inputW?std::clamp(std::log2(float(inputW)/outW),-1.5f,0.f):0.f;
 scene=true;
 if(correctionEnabled())for(DWORD i=0;i<16;++i){
  // A switch can enable DLSS while textures are already bound. Reinspect at
  // the scene boundary, then normal SetTexture calls maintain their metadata.
  ComPtr<IDirect3DBaseTexture9> t;if(SUCCEEDED(device->GetTexture(i,&t)))mipmapped[i]=scene_texture(t.Get());
  if(!known[i]&&SUCCEEDED(getSampler(device,i,D3DSAMP_MIPMAPLODBIAS,&original[i])))known[i]=true;
  apply(i);
 }
 else for(DWORD i=0;i<16;++i)if(lastApplied[i])apply(i);
}
void input_width(unsigned v){actualInput=v;}
void end_scene(){scene=false;if(device)for(DWORD i=0;i<16;++i)if(lastApplied[i])apply(i);}
void draw_ui(){bool s=sharper;if(ImGui::Checkbox("Sharper scene textures",&s)){sharper=s;if(settings[0])WritePrivateProfileStringW(L"AsylumVR",L"SharperTextures",s?L"1":L"0",settings);}ImGui::TextWrapped("Matches texture detail to the reduced DLSS input. Uses mild filtering correction; post effects and HUD are excluded.");}
bool fixture(IDirect3DDevice9*d){
 if(!device&&!install(d))return false;
 ComPtr<IDirect3DTexture9> tex;if(FAILED(d->CreateTexture(16,16,3,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&tex,nullptr)))return false;
 const int savedMode=dlss::mode();dlss::set_mode(0,false);scene=true;
 const unsigned descriptions=metadataReads,stockBefore=stockTextureBinds;
 for(unsigned i=0;i<512;++i)if(FAILED(texture(d,0,i%2?tex.Get():nullptr)))return false;
 bool stockOK=metadataReads==descriptions&&stockTextureBinds-stockBefore==512;
 dlss::set_mode(3,false);
 const float bias=.25f;DWORD bits{};memcpy(&bits,&bias,4);scene=true;correction=-.5f;set(d,0,D3DSAMP_MIPMAPLODBIAS,bits);texture(d,0,tex.Get());
 DWORD raw{},seen{};getSampler(d,0,D3DSAMP_MIPMAPLODBIAS,&raw);get(d,0,D3DSAMP_MIPMAPLODBIAS,&seen);float actual{};memcpy(&actual,&raw,4);
 bool ok=stockOK&&std::fabs(actual+.25f)<.00001f&&seen==bits;
 set(d,0,D3DSAMP_MIPMAPLODBIAS,seen);getSampler(d,0,D3DSAMP_MIPMAPLODBIAS,&raw);memcpy(&actual,&raw,4);ok=std::fabs(actual+.25f)<.00001f&&ok;
 end_scene();getSampler(d,0,D3DSAMP_MIPMAPLODBIAS,&raw);ok=raw==bits&&ok;texture(d,0,nullptr);
 dlss::set_mode(savedMode,false);
 printf("D3D9 detail: 512 stock texture binds with ZERO metadata probes; reduced-input mip bias, original state reads, repeat restore without accumulation, HUD restore %s\n",ok?"PASS":"FAIL");return ok;
}
}
