// Debug toolkit (VR_DEBUG_TOOLKIT.md): eye-same draw finder. On request it
// records one LEFT/RIGHT scene pass pair draw by draw and logs every scene draw
// whose vertex constants are bit-identical in both eyes, and (A2m) every draw
// whose NON-camera inputs differ between the eyes (e.g. its own position). A world object drawn
// that way gets no stereo separation (it reads as infinitely far). Each listed
// shader's constant table names show which camera input it reads; its
// bytecode is saved beside the log for offline disassembly.
#include "eyesame9.h"
#include "core/util/log.h"
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::eyesame9 {
namespace {
constexpr UINT kRegisters=96;constexpr size_t kMaxDraws=6000;
struct Draw{void*vs{},*ps{},*tex{};UINT count{},rtWidth{},rtHeight{},texWidth{},texHeight{},texFormat{};uint64_t constants{};float rows[8][4]{};std::vector<float> c;};
enum State{Idle,Armed,Left,Right};std::atomic<int> state{Idle};uint32_t pairGeneration{};std::vector<Draw> eyes[2];unsigned reports{};
uint64_t fnv(const void*p,size_t n,uint64_t h=1469598103934665603ull){auto*b=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}return h;}
bool bytecode(IDirect3DVertexShader9*vs,std::vector<DWORD>&code){UINT n{};if(!vs||FAILED(vs->GetFunction(nullptr,&n))||n<8||n>262144||n%4)return false;code.resize(n/4);return SUCCEEDED(vs->GetFunction(code.data(),&n));}
// D3DXSHADER_CONSTANTTABLE inside the 'CTAB' comment: name@c<register>x<count>.
std::string constant_names(const std::vector<DWORD>&code){
 std::string out;for(size_t at=1;at<code.size();){const DWORD op=code[at]&0xffff;if(op==0xffff)break;const size_t n=op==0xfffe?(code[at]>>16)&0x7fff:(code[at]>>24)&15;
  if(op==0xfffe&&n>=8&&code[at+1]==0x42415443){const auto*base=reinterpret_cast<const unsigned char*>(&code[at+2]);const size_t bytes=(n-1)*4;
   auto u32=[&](size_t o)->uint32_t{uint32_t v{};if(o+4<=bytes)memcpy(&v,base+o,4);return v;};auto u16=[&](size_t o)->uint16_t{uint16_t v{};if(o+2<=bytes)memcpy(&v,base+o,2);return v;};
   const uint32_t count=u32(12),info=u32(16);for(uint32_t i=0;i<count&&i<64;++i){const size_t c=info+i*20;const uint32_t name=u32(c);if(name>=bytes)continue;
    const char*s=reinterpret_cast<const char*>(base+name);size_t len=strnlen(s,bytes-name);char item[96];snprintf(item,sizeof(item),"%s%.*s@%c%ux%u",out.empty()?"":" ",int(len>48?48:len),s,u16(c+4)==2?'c':u16(c+4)==0?'b':u16(c+4)==1?'i':'s',u16(c+6),u16(c+8));out+=item;}
   return out;}
  at+=n+1;}
 return "(no constant table)";
}
struct Range{std::string name;unsigned start{},count{};};
std::vector<Range> ranges(const std::vector<DWORD>&code){
 std::vector<Range> out;for(size_t at=1;at<code.size();){const DWORD op=code[at]&0xffff;if(op==0xffff)break;const size_t n=op==0xfffe?(code[at]>>16)&0x7fff:(code[at]>>24)&15;
  if(op==0xfffe&&n>=8&&code[at+1]==0x42415443){const auto*base=reinterpret_cast<const unsigned char*>(&code[at+2]);const size_t bytes=(n-1)*4;
   auto u32=[&](size_t o)->uint32_t{uint32_t v{};if(o+4<=bytes)memcpy(&v,base+o,4);return v;};auto u16=[&](size_t o)->uint16_t{uint16_t v{};if(o+2<=bytes)memcpy(&v,base+o,2);return v;};
   const uint32_t count=u32(12),info=u32(16);for(uint32_t i=0;i<count&&i<64;++i){const size_t c=info+i*20;const uint32_t name=u32(c);if(name>=bytes||u16(c+4)!=2)continue;
    const char*s=reinterpret_cast<const char*>(base+name);out.push_back({std::string(s,strnlen(s,bytes-name)),u16(c+6),u16(c+8)});}
   return out;}
  at+=n+1;}
 return out;
}
// Normal stereo: only camera inputs differ between the eyes. Anything else
// that differs (an object's own position, a particle's placement) is suspect.
bool camera_input(const std::string&n){return n=="ViewProjectionMatrix"||n.find("Camera")!=std::string::npos||n.find("ViewOrigin")!=std::string::npos||n.find("PreViewTranslation")!=std::string::npos||n.find("ScreenPosition")!=std::string::npos;}
void report(){
 // Pair the k-th LEFT draw of a kind (VS, PS, texture, primitives, target size)
 // with the k-th RIGHT draw of the same kind: the eyes need not draw the same
 // number of things (left-only work), so plain list order does not line up.
 const auto&l=eyes[0];const auto&r=eyes[1];unsigned unmatched=0,same=0,differ=0;std::vector<size_t> list;std::vector<long> partner(l.size(),-1);std::vector<bool> used(r.size());
 auto kind=[](const Draw&a,const Draw&b){return a.vs==b.vs&&a.ps==b.ps&&a.tex==b.tex&&a.count==b.count&&a.rtWidth==b.rtWidth&&a.rtHeight==b.rtHeight;};
 size_t from=0;for(size_t i=0;i<l.size();++i){for(size_t j=from;j<r.size();++j)if(!used[j]&&kind(l[i],r[j])){partner[i]=long(j);used[j]=true;if(j==from)++from;break;}if(partner[i]<0)++unmatched;}
 const size_t n=l.size();
 for(size_t i=0;i<n;++i){if(partner[i]<0)continue;const auto&a=l[i];const auto&b=r[size_t(partner[i])];if(a.constants==b.constants){++same;if(list.size()<80)list.push_back(i);}else ++differ;}
 BVR_LOG("[aa eyesame] pair generation %u: LEFT %zu RIGHT %zu scene draws; paired by kind %zu, LEFT without a RIGHT partner %u; constants DIFFER between eyes %u, IDENTICAL %u (listed below, first 80). Identical world draws have no stereo depth.",pairGeneration,l.size(),r.size(),n-unmatched,unmatched,differ,same);
 wchar_t dir[MAX_PATH];swprintf_s(dir,L"%s\\captures\\eyesame_shaders",log::data_dir());CreateDirectoryW(dir,nullptr);std::vector<void*> named;
 for(size_t i:list){const auto&a=l[i];std::vector<DWORD>code;uint64_t hash=0;if(bytecode(static_cast<IDirect3DVertexShader9*>(a.vs),code))hash=fnv(code.data(),code.size()*4);
  BVR_LOG("[aa eyesame] draw %zu prims %u RT %ux%u tex %ux%u fmt%u VS %016llx PS %p c0 %.4f %.4f %.4f %.4f c1 %.4f %.4f %.4f %.4f c3 %.4f %.4f %.4f %.4f",i,a.count,a.rtWidth,a.rtHeight,a.texWidth,a.texHeight,a.texFormat,(unsigned long long)hash,a.ps,a.rows[0][0],a.rows[0][1],a.rows[0][2],a.rows[0][3],a.rows[1][0],a.rows[1][1],a.rows[1][2],a.rows[1][3],a.rows[3][0],a.rows[3][1],a.rows[3][2],a.rows[3][3]);
  bool seen=false;for(void*v:named)seen|=v==a.vs;if(seen||code.empty())continue;named.push_back(a.vs);
  BVR_LOG("[aa eyesame] VS %016llx constants: %s",(unsigned long long)hash,constant_names(code).c_str());
  wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\vs_%016llx.bin",dir,(unsigned long long)hash);if(FILE*f=_wfopen(path,L"wb")){fwrite(code.data(),4,code.size(),f);fclose(f);}}
 // A2m: WHICH named inputs differ between the eyes, per draw kind.
 std::map<void*,std::vector<Range>> tables;std::map<std::string,unsigned> histogram;unsigned suspects=0;
 for(size_t i=0;i<n;++i){if(partner[i]<0)continue;const auto&a=l[i];const auto&b=r[size_t(partner[i])];if(a.constants==b.constants||a.c.size()!=b.c.size()||a.c.empty())continue;
  auto it=tables.find(a.vs);if(it==tables.end()){std::vector<DWORD>code;std::vector<Range> t;if(bytecode(static_cast<IDirect3DVertexShader9*>(a.vs),code))t=ranges(code);it=tables.emplace(a.vs,std::move(t)).first;}
  std::string odd;for(const auto&g:it->second){bool diff=false;for(unsigned k=g.start;k<g.start+g.count&&k<kRegisters&&!diff;++k)diff=memcmp(&a.c[k*4],&b.c[k*4],16)!=0;if(!diff)continue;++histogram[g.name];
   if(!camera_input(g.name)&&odd.empty()){odd=g.name;if(++suspects<=40){const unsigned k0=g.start,k3=std::min<unsigned>(g.start+g.count-1,kRegisters-1);std::vector<DWORD>code;uint64_t hash=0;if(bytecode(static_cast<IDirect3DVertexShader9*>(a.vs),code))hash=fnv(code.data(),code.size()*4);
    BVR_LOG("[aa eyesame] SUSPECT draw %zu prims %u RT %ux%u tex %ux%u fmt%u VS %016llx: '%s' differs between eyes; first row L %.3f %.3f %.3f %.3f R %.3f %.3f %.3f %.3f; last row L %.3f %.3f %.3f %.3f R %.3f %.3f %.3f %.3f",i,a.count,a.rtWidth,a.rtHeight,a.texWidth,a.texHeight,a.texFormat,(unsigned long long)hash,g.name.c_str(),
     a.c[k0*4],a.c[k0*4+1],a.c[k0*4+2],a.c[k0*4+3],b.c[k0*4],b.c[k0*4+1],b.c[k0*4+2],b.c[k0*4+3],a.c[k3*4],a.c[k3*4+1],a.c[k3*4+2],a.c[k3*4+3],b.c[k3*4],b.c[k3*4+1],b.c[k3*4+2],b.c[k3*4+3]);
    if(suspects<=12&&!code.empty())BVR_LOG("[aa eyesame] SUSPECT VS %016llx constants: %s",(unsigned long long)hash,constant_names(code).c_str());}}}}
 {std::string h;for(const auto&[name,count]:histogram){char item[96];snprintf(item,sizeof(item)," %s=%u",name.c_str(),count);h+=item;}BVR_LOG("[aa eyesame] inputs that differ between the eyes (draw counts):%s; non-camera suspects %u",h.c_str(),suspects);}
 // A2o: per shader kind, which named inputs change between the eyes and which
 // stay the same. A camera input (View..., Projection...) that stays the same
 // while another changes means that draw moves with the eye.
 {struct Kind{unsigned draws{},minPrims{~0u},maxPrims{};UINT texW{},texH{};std::map<std::string,unsigned> differ,same;};std::map<void*,Kind> kinds;
  for(size_t i=0;i<n;++i){if(partner[i]<0)continue;const auto&a=l[i];const auto&b=r[size_t(partner[i])];if(a.c.size()!=b.c.size()||a.c.empty())continue;
   auto it=tables.find(a.vs);if(it==tables.end()){std::vector<DWORD>code;std::vector<Range> t;if(bytecode(static_cast<IDirect3DVertexShader9*>(a.vs),code))t=ranges(code);it=tables.emplace(a.vs,std::move(t)).first;}
   auto&k=kinds[a.vs];++k.draws;k.minPrims=std::min(k.minPrims,a.count);k.maxPrims=std::max(k.maxPrims,a.count);if(a.texWidth>k.texW){k.texW=a.texWidth;k.texH=a.texHeight;}
   for(const auto&g:it->second){bool diff=false;for(unsigned q=g.start;q<g.start+g.count&&q<kRegisters&&!diff;++q)diff=memcmp(&a.c[q*4],&b.c[q*4],16)!=0;++(diff?k.differ:k.same)[g.name];}}
  for(const auto&[vs,k]:kinds){std::vector<DWORD>code;uint64_t hash=0;if(bytecode(static_cast<IDirect3DVertexShader9*>(vs),code))hash=fnv(code.data(),code.size()*4);std::string d,sm;
   for(const auto&[nm,c]:k.differ){char t[80];snprintf(t,sizeof(t)," %s=%u",nm.c_str(),c);d+=t;}for(const auto&[nm,c]:k.same){char t[80];snprintf(t,sizeof(t)," %s=%u",nm.c_str(),c);sm+=t;}
   BVR_LOG("[aa eyesame] KIND VS %016llx draws %u prims %u..%u tex %ux%u | change between eyes:%s | SAME in both eyes:%s",(unsigned long long)hash,k.draws,k.minPrims,k.maxPrims,k.texW,k.texH,d.c_str(),sm.c_str());
   wchar_t path[MAX_PATH];swprintf_s(path,L"%ss_%016llx.bin",dir,(unsigned long long)hash);if(!code.empty())if(FILE*f=_wfopen(path,L"wb")){fwrite(code.data(),4,code.size(),f);fclose(f);}}}
 {unsigned shown=0;for(size_t i=0;i<n&&shown<20;++i){if(partner[i]>=0)continue;const auto&a=l[i];++shown;BVR_LOG("[aa eyesame] LEFT-only draw %zu prims %u RT %ux%u tex %ux%u fmt%u VS %p",i,a.count,a.rtWidth,a.rtHeight,a.texWidth,a.texHeight,a.texFormat,a.vs);}}
 // A normal world draw for comparison: same table for one DIFFERING draw.
 for(size_t i=0;i<n;++i){if(partner[i]<0)continue;const auto&a=l[i];const auto&b=r[size_t(partner[i])];if(a.constants!=b.constants&&a.rtWidth>512){std::vector<DWORD>code;if(bytecode(static_cast<IDirect3DVertexShader9*>(a.vs),code))BVR_LOG("[aa eyesame] reference DIFFERING draw %zu VS %016llx constants: %s",i,(unsigned long long)fnv(code.data(),code.size()*4),constant_names(code).c_str());break;}}
 ++reports;
}
}
void request(){int idle=Idle;if(state.compare_exchange_strong(idle,Armed))BVR_LOG("[aa eyesame] armed: next LEFT/RIGHT scene pair is compared draw by draw");}
void begin_eye(int eye,uint32_t generation){
 const int s=state.load();
 if(s==Armed&&eye<0){eyes[0].clear();eyes[1].clear();pairGeneration=generation;state=Left;return;}
 if(s==Left&&eye>0){if(generation==pairGeneration)state=Right;else state=Armed;return;}
 if(s==Right&&eye<0){report();eyes[0].clear();eyes[1].clear();state=Idle;return;}
 if(s==Left&&eye<0){eyes[0].clear();pairGeneration=generation;}
}
bool active(){const int s=state.load();return s==Left||s==Right;}
void scene_draw(IDirect3DDevice9*d,UINT count){
 const int s=state.load();if(s!=Left&&s!=Right)return;auto&v=eyes[s==Left?0:1];if(v.size()>=kMaxDraws)return;Draw x{};x.count=count;
 ComPtr<IDirect3DVertexShader9>vs;ComPtr<IDirect3DPixelShader9>ps;ComPtr<IDirect3DBaseTexture9>tex;d->GetVertexShader(&vs);d->GetPixelShader(&ps);d->GetTexture(0,&tex);x.vs=vs.Get();x.ps=ps.Get();x.tex=tex.Get();
 ComPtr<IDirect3DTexture9>t;D3DSURFACE_DESC td{};if(tex&&SUCCEEDED(tex.As(&t))&&SUCCEEDED(t->GetLevelDesc(0,&td))){x.texWidth=td.Width;x.texHeight=td.Height;x.texFormat=td.Format;}
 ComPtr<IDirect3DSurface9>rt;D3DSURFACE_DESC rd{};if(SUCCEEDED(d->GetRenderTarget(0,&rt))&&rt&&SUCCEEDED(rt->GetDesc(&rd))){x.rtWidth=rd.Width;x.rtHeight=rd.Height;}
 float c[kRegisters*4]{};if(vs&&SUCCEEDED(d->GetVertexShaderConstantF(0,c,kRegisters))){x.constants=fnv(c,sizeof(c));memcpy(x.rows,c,sizeof(x.rows));x.c.assign(c,c+kRegisters*4);}
 v.push_back(x);
}
}
