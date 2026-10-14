// A2n: engine-cached eye-relative transforms. UE3 uploads LocalToWorld already
// translated by the view origin (PreViewTranslation), so every ordinary object's
// translation row differs between the eyes by exactly the eye shift. Particle
// meshes (shotgun shells) are transformed once per frame for the FIRST view;
// the right eye then reuses the left eye's translated rows and the object moves
// with the eye (reads as infinitely far). LIVE A2m: ordinary rows differ by
// -5.0/+2.8/+0.6 units; shells measured 554 px disparity at every distance.
#include "xform9.h"
#include "markers9.h"
#include "core/util/log.h"
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::xform9 {
namespace {
std::atomic<bool> on{false};wchar_t settings[MAX_PATH]{};bool loaded{};
SRWLOCK shaderLock=SRWLOCK_INIT;std::unordered_map<void*,int> registers;
std::unordered_set<uint64_t> leftRows;uint32_t leftGen{},gen{};int eye{};UINT width{},height{};float shift[3]{};bool shiftValid{};
std::atomic<unsigned> recorded{},checked{},stale{},patched{},noShift{},pairs{};unsigned shiftLogs{},patchLogs{};
uint64_t row_key(const float*r){uint64_t h=1469598103934665603ull;auto*b=reinterpret_cast<const unsigned char*>(r);for(int i=0;i<12;++i){h^=b[i];h*=1099511628211ull;}return h;}
// LocalToWorld register from the shader's own constant table; -1 when absent.
int local_to_world(IDirect3DVertexShader9*vs){
 AcquireSRWLockShared(&shaderLock);auto it=registers.find(vs);const bool known=it!=registers.end();const int cached=known?it->second:-1;ReleaseSRWLockShared(&shaderLock);if(known)return cached;
 int reg=-1;UINT n{};std::vector<DWORD>code;
 if(SUCCEEDED(vs->GetFunction(nullptr,&n))&&n>=8&&n<=262144&&n%4==0){code.resize(n/4);if(SUCCEEDED(vs->GetFunction(code.data(),&n))){
  for(size_t at=1;at<code.size()&&reg<0;){const DWORD op=code[at]&0xffff;if(op==0xffff)break;const size_t c=op==0xfffe?(code[at]>>16)&0x7fff:(code[at]>>24)&15;
   if(op==0xfffe&&c>=8&&at+1<code.size()&&code[at+1]==0x42415443){const auto*base=reinterpret_cast<const unsigned char*>(&code[at+2]);const size_t bytes=(c-1)*4;
    auto u32=[&](size_t o)->uint32_t{uint32_t v{};if(o+4<=bytes)memcpy(&v,base+o,4);return v;};auto u16=[&](size_t o)->uint16_t{uint16_t v{};if(o+2<=bytes)memcpy(&v,base+o,2);return v;};
    const uint32_t count=u32(12),info=u32(16);for(uint32_t i=0;i<count&&i<64;++i){const size_t e=info+i*20;const uint32_t name=u32(e);if(name>=bytes)continue;
     const char*s=reinterpret_cast<const char*>(base+name);if(strnlen(s,bytes-name)==12&&!memcmp(s,"LocalToWorld",12)&&u16(e+4)==2&&u16(e+8)==4){reg=u16(e+6);break;}}}
   at+=c+1;}}}
 AcquireSRWLockExclusive(&shaderLock);if(registers.size()>=4096)registers.clear();registers[vs]=reg;ReleaseSRWLockExclusive(&shaderLock);return reg;
}
}
void begin_eye(int e,uint32_t g,UINT w,UINT h){
 if(!loaded){loaded=true;swprintf_s(settings,L"%s\\aa_rendering.ini",log::data_dir());on=GetPrivateProfileIntW(L"AsylumVR",L"FixStaleParticles",0,settings)!=0;BVR_LOG("[aa xform] engine-cached eye-relative transforms fix %s",on.load()?"ON":"off");}
 eye=e;gen=g;width=w;height=h;
 if(e<0){leftRows.clear();leftGen=g;shiftValid=false;return;}
 if(e>0){float l[3]{},r[3]{};shiftValid=leftGen==g&&markers9::eye_origins(g,l,r);if(shiftValid){for(int j=0;j<3;++j)shift[j]=l[j]-r[j];const float len=std::sqrt(shift[0]*shift[0]+shift[1]*shift[1]+shift[2]*shift[2]);if(!std::isfinite(len)||len>30.f)shiftValid=false;
  if(++shiftLogs<=4)BVR_LOG("[aa xform] eye shift generation %u: %.3f %.3f %.3f (%.2f units) valid%u; left rows %zu",g,shift[0],shift[1],shift[2],len,shiftValid,leftRows.size());}++pairs;}
}
void vertex_created(IDirect3DVertexShader9*vs){AcquireSRWLockExclusive(&shaderLock);registers.erase(vs);ReleaseSRWLockExclusive(&shaderLock);}
Scope::Scope(IDirect3DDevice9*d,bool sceneDraw):device(d){
 if(!on||!sceneDraw||!eye||!width)return;
 ComPtr<IDirect3DVertexShader9>vs;d->GetVertexShader(&vs);if(!vs)return;const int r=local_to_world(vs.Get());if(r<0||r+3>=256)return;
 // Main scene target only: shadow/light passes are not eye-relative.
 ComPtr<IDirect3DSurface9>rt;D3DSURFACE_DESC rd{};if(FAILED(d->GetRenderTarget(0,&rt))||!rt||FAILED(rt->GetDesc(&rd))||rd.Width<width||rd.Width>width+64||rd.Height<height||rd.Height>height+64)return;
 float row[4]{};if(FAILED(d->GetVertexShaderConstantF(r+3,row,1))||!std::isfinite(row[0]+row[1]+row[2]))return;
 // A row at the eye itself is camera-attached (sky, weather volume): it SHOULD match.
 if(std::fabs(row[0])+std::fabs(row[1])+std::fabs(row[2])<1.f)return;const uint64_t key=row_key(row);
 if(eye<0){leftRows.insert(key);++recorded;return;}
 if(leftGen!=gen)return;++checked;if(!leftRows.count(key))return;++stale;if(!shiftValid){++noShift;return;}
 memcpy(saved,row,16);float fixed[4]={row[0]+shift[0],row[1]+shift[1],row[2]+shift[2],row[3]};
 if(FAILED(d->SetVertexShaderConstantF(r+3,fixed,1)))return;reg=r;changed=true;++patched;
 if(++patchLogs<=8)BVR_LOG("[aa xform] right eye reused left translation %.2f %.2f %.2f (LocalToWorld c%d) -> shifted by eye %.2f %.2f %.2f",row[0],row[1],row[2],r,shift[0],shift[1],shift[2]);
}
Scope::~Scope(){if(changed)device->SetVertexShaderConstantF(reg+3,saved,1);}
bool command(const char*args){
 if(_stricmp(args,"on")&&_stricmp(args,"off"))return false;on=!_stricmp(args,"on");if(settings[0])WritePrivateProfileStringW(L"AsylumVR",L"FixStaleParticles",on?L"1":L"0",settings);BVR_LOG("[aa xform] engine-cached eye-relative transforms fix %s",on.load()?"ON":"off");return true;
}
void status_line(char*out,size_t n){snprintf(out,n,"on%u pairs%u leftRows%u rightChecked%u rightReused%u patched%u noShift%u",on.load(),pairs.load(),recorded.load(),checked.load(),stale.load(),patched.load(),noShift.load());}
}
