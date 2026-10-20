#include "transitions9.h"
#include "scene.h"
#include "managed9.h"
#include "core/util/log.h"
#include "core/vr/openxr_runtime.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <imgui.h>
#include <array>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::transitions9 {
namespace {
IDirect3DDevice9* owned{};int eye{};uint32_t gen{};bool painted{},internal{};
bool enabled=true,removeFades=true;wchar_t settings[MAX_PATH]{};
bool fullViewFades=true;float pairFadeAlpha{};
UINT fixtureW{},fixtureH{};
unsigned managedReads{},removed{},bats{},unknown{},reads{},captureFailures{},bufferCopies{},shaderRejects{},stateRejects{},constantRejects{};ULONGLONG lastBat{};
float batStage[16]{};bool haveBatStage{};ULONGLONG batEpisodeAt{};
ComPtr<IDirect3DTexture9> image;ComPtr<IDirect3DSurface9> target,depth;
constexpr D3DRENDERSTATETYPE stateKeys[]={D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_COLORWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_SRGBWRITEENABLE};
struct Instruction{unsigned op{},n{};DWORD p[5]{};};
struct Shader{bool supported{},clipPosition{};int matrix{-1};bool rows{};std::array<int,16> inputColour;std::vector<Instruction> ops;Shader(){inputColour.fill(-1);}};
std::unordered_map<void*,Shader> shaders;SRWLOCK shaderLock=SRWLOCK_INIT;
unsigned type(DWORD t){return ((t>>28)&7)|((t>>8)&24);}unsigned index(DWORD t){return t&2047;}
unsigned operands(unsigned op){switch(op){case D3DSIO_MOV:case D3DSIO_LOG:case D3DSIO_EXP:return 2;case D3DSIO_ADD:case D3DSIO_MUL:case D3DSIO_DP4:case D3DSIO_M4x4:return 3;case D3DSIO_MAD:return 4;case D3DSIO_DCL:return 2;case D3DSIO_DEF:return 5;case D3DSIO_NOP:return 0;default:return 99;}}
Shader parse(const std::vector<DWORD>& code){
 Shader s;if(code.size()<2)return s;const bool vs=(code[0]>>16)==0xfffe;const unsigned major=(code[0]>>8)&255;if(major<1||major>3)return s;if(!vs&&major<3){s.inputColour[0]=0;s.inputColour[1]=1;}
 int transform=-1,dpBase=-1;unsigned output=0,dpMask=0;bool ended=false;
 for(size_t at=1;at<code.size();){const DWORD token=code[at];const unsigned op=token&65535;
  if(op==D3DSIO_END){ended=true;break;}
  if(op==D3DSIO_COMMENT){const unsigned n=(token>>16)&32767;if(n>code.size()-at-1)return {};
   if(vs&&n>8&&code[at+1]==MAKEFOURCC('C','T','A','B')){
    const BYTE* b=reinterpret_cast<const BYTE*>(code.data()+at+2);const size_t bytes=(n-1)*4;auto u32=[](const BYTE*p){DWORD v;memcpy(&v,p,4);return v;};auto u16=[](const BYTE*p){WORD v;memcpy(&v,p,2);return v;};
    const unsigned count=u32(b+12),off=u32(b+16);if(count<64&&off<=bytes&&count<=(bytes-off)/20)for(unsigned i=0;i<count;++i){const BYTE* c=b+off+20*i;const unsigned name=u32(c);if(name<bytes&&memchr(b+name,0,bytes-name)&&!strcmp(reinterpret_cast<const char*>(b+name),"Transform")&&u16(c+4)==2&&u16(c+8)==4)transform=u16(c+6);}
   }at+=n+1;continue;
  }
  const unsigned n=operands(op);if(n>5||n>code.size()-at-1||(major>=2&&((token>>24)&15)!=n))return {};
  Instruction instruction{op,n,{}};memcpy(instruction.p,code.data()+at+1,n*4);
  if(!vs&&op==D3DSIO_DCL&&type(instruction.p[1])==D3DSPR_INPUT&&index(instruction.p[1])<16&&(instruction.p[0]&31)==D3DDECLUSAGE_COLOR&&((instruction.p[0]>>16)&15)<2)s.inputColour[index(instruction.p[1])]=(instruction.p[0]>>16)&15;
  if(op==D3DSIO_DCL&&vs&&(instruction.p[0]&31)==D3DDECLUSAGE_POSITION&&type(instruction.p[1])==D3DSPR_OUTPUT)output=index(instruction.p[1]);
  if(vs&&op==D3DSIO_M4x4&&type(instruction.p[1])==D3DSPR_INPUT&&index(instruction.p[1])==0&&type(instruction.p[2])==D3DSPR_CONST&&((type(instruction.p[0])==D3DSPR_RASTOUT&&index(instruction.p[0])==0)||(type(instruction.p[0])==D3DSPR_OUTPUT&&index(instruction.p[0])==output))){s.matrix=index(instruction.p[2]);s.rows=true;}
  if(vs&&op==D3DSIO_DP4&&type(instruction.p[1])==D3DSPR_INPUT&&index(instruction.p[1])==0&&type(instruction.p[2])==D3DSPR_CONST&&((type(instruction.p[0])==D3DSPR_RASTOUT&&index(instruction.p[0])==0)||(type(instruction.p[0])==D3DSPR_OUTPUT&&index(instruction.p[0])==output))){const unsigned mask=(instruction.p[0]>>16)&15;for(unsigned component=0;component<4;++component)if(mask==(1u<<component)){const int base=int(index(instruction.p[2]))-int(component);if(dpBase<0)dpBase=base;if(base==dpBase)dpMask|=mask;}}
  if(vs&&op==D3DSIO_MOV&&((type(instruction.p[0])==D3DSPR_RASTOUT&&index(instruction.p[0])==0)||(type(instruction.p[0])==D3DSPR_OUTPUT&&index(instruction.p[0])==output))&&((instruction.p[0]>>16)&15)==15&&instruction.p[1]==0x90e40000)s.clipPosition=true;
  if(!vs&&op!=D3DSIO_DCL&&op!=D3DSIO_NOP){if(op==D3DSIO_M4x4)return {};s.ops.push_back(instruction);if(s.ops.size()>24)return {};}
  at+=n+1;
 }
 if(!ended)return {};if(vs){if(dpMask==15&&dpBase>=0){s.matrix=dpBase;s.rows=true;}if(s.matrix<0&&transform>=0){s.matrix=transform;s.rows=false;}s.supported=(s.matrix>=0&&s.matrix<=252)||s.clipPosition;if(s.clipPosition&&s.matrix<0)s.rows=true;}
 else s.supported=!s.ops.empty();return s;
}
template<class T> Shader shader(T* object){
 AcquireSRWLockExclusive(&shaderLock);const auto found=shaders.find(object);if(found!=shaders.end()){Shader s=found->second;ReleaseSRWLockExclusive(&shaderLock);return s;}Shader s;UINT bytes{};
 if(object&&SUCCEEDED(object->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=32768&&bytes%4==0){std::vector<DWORD> code(bytes/4);if(SUCCEEDED(object->GetFunction(code.data(),&bytes)))s=parse(code);}
 if(shaders.size()>=512)shaders.erase(shaders.begin());auto result=shaders.emplace(object,std::move(s));Shader value=result.first->second;ReleaseSRWLockExclusive(&shaderLock);return value;
}
// Save a bounded sample of rejected UI bindings once per unique shader pair.
// This is CPU shader/constant/declaration data, never a GPU readback.
void rejected_ui(IDirect3DDevice9*d,IDirect3DVertexShader9*v,IDirect3DPixelShader9*p,const Draw&draw){
 static const bool retail=[](){wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(nullptr,path,MAX_PATH))return false;const auto*name=wcsrchr(path,L'\\');return name&&!_wcsicmp(name+1,L"ShippingPC-BmGame.exe");}();
 static uint64_t seen[24]{};static unsigned used{};if(!retail||used>=24||!scene::test_options())return; // test recorder: only with test options on
 std::vector<DWORD>code[2];uint64_t hash=14695981039346656037ull;
 for(unsigned j=0;j<2;++j){UINT n{};HRESULT hr=E_FAIL;
  if(j){if(p)hr=p->GetFunction(nullptr,&n);else hr=E_FAIL;}else{if(v)hr=v->GetFunction(nullptr,&n);else hr=E_FAIL;}
  if(SUCCEEDED(hr)&&n&&n<=32768&&n%4==0){code[j].resize(n/4);if(j)p->GetFunction(code[j].data(),&n);else v->GetFunction(code[j].data(),&n);}
  for(DWORD word:code[j]){hash^=word;hash*=1099511628211ull;}hash^=j+1;
 }
 for(unsigned n=0;n<used;++n)if(seen[n]==hash)return;seen[used++]=hash;
 wchar_t root[MAX_PATH]{},file[MAX_PATH]{};swprintf_s(root,L"%s\\ui_bindings",log::data_dir());CreateDirectoryW(root,nullptr);
 for(unsigned j=0;j<2;++j)if(!code[j].empty()){swprintf_s(file,L"%s\\A2a_%016llX_%s.bin",root,hash,j?L"ps":L"vs");if(FILE*f=_wfopen(file,L"wb")){fwrite(code[j].data(),4,code[j].size(),f);fclose(f);}}
 swprintf_s(file,L"%s\\A2a_%016llX.txt",root,hash);if(FILE*f=_wfopen(file,L"wt")){fprintf(f,"Rejected actual retail UI binding: eye%d generation%u type%u count%u VS%p PS%p\n",eye,gen,unsigned(draw.type),draw.count,v,p);D3DVIEWPORT9 vp{};d->GetViewport(&vp);fprintf(f,"viewport %u %u %u %u\n",vp.X,vp.Y,vp.Width,vp.Height);
  for(auto key:{D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_COLORWRITEENABLE}){DWORD x{};d->GetRenderState(key,&x);fprintf(f,"state%u=%u\n",unsigned(key),x);}
  ComPtr<IDirect3DVertexDeclaration9>decl;UINT count=64;D3DVERTEXELEMENT9 elements[64]{};if(SUCCEEDED(d->GetVertexDeclaration(&decl))&&decl&&SUCCEEDED(decl->GetDeclaration(elements,&count)))for(unsigned n=0;n<count&&elements[n].Stream!=0xff;++n){const auto&e=elements[n];fprintf(f,"decl stream%u offset%u type%u usage%u index%u\n",e.Stream,e.Offset,e.Type,e.Usage,e.UsageIndex);}
  float vs[256][4]{},ps[32][4]{};if(SUCCEEDED(d->GetVertexShaderConstantF(0,&vs[0][0],256)))for(unsigned n=0;n<256;++n)if(vs[n][0]||vs[n][1]||vs[n][2]||vs[n][3])fprintf(f,"vs%u %.9g %.9g %.9g %.9g\n",n,vs[n][0],vs[n][1],vs[n][2],vs[n][3]);if(SUCCEEDED(d->GetPixelShaderConstantF(0,&ps[0][0],32)))for(unsigned n=0;n<32;++n)fprintf(f,"ps%u %.9g %.9g %.9g %.9g\n",n,ps[n][0],ps[n][1],ps[n][2],ps[n][3]);fclose(f);
 }BVR_LOG("[aa transitions] rejected UI binding saved %016llX (%u/24)",hash,used);
}
using CreatePS=HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);CreatePS createPS{};
HRESULT WINAPI ps_created(IDirect3DDevice9*d,const DWORD*c,IDirect3DPixelShader9**p){auto h=createPS(d,c,p);if(d==owned&&SUCCEEDED(h)&&p)vertex_created(reinterpret_cast<IDirect3DVertexShader9*>(*p));return h;}
// Copy only small dynamic buffers at their existing CPU upload, never read a
// WRITEONLY GPU buffer back or add a GPU wait. Weak bounded metadata, 4MiB max.
struct Buffer{std::vector<BYTE> bytes;UINT length{},lo{},hi{},offset{},size{};void* mapped{};DWORD flags{};};
std::unordered_map<void*,Buffer> buffers;SRWLOCK bufferLock=SRWLOCK_INIT;
using Lock=HRESULT(WINAPI*)(void*,UINT,UINT,void**,DWORD);using Unlock=HRESULT(WINAPI*)(void*);
struct Hook{std::atomic<void*> address{};void* original{};};Hook locks[8]{},unlocks[8]{};SRWLOCK hookLock=SRWLOCK_INIT;
void* original(void* object,unsigned slot,Hook (&list)[8]){void* address=(*reinterpret_cast<void***>(object))[slot];for(auto&h:list)if(h.address.load(std::memory_order_acquire)==address)return h.original;return nullptr;}
HRESULT WINAPI lock(void*b,UINT o,UINT n,void**p,DWORD f){auto fn=reinterpret_cast<Lock>(original(b,11,locks));if(!fn)return D3DERR_INVALIDCALL;const auto h=fn(b,o,n,p,f);
 if(SUCCEEDED(h)&&p){AcquireSRWLockExclusive(&bufferLock);auto i=buffers.find(b);if(i!=buffers.end()&&!(f&D3DLOCK_READONLY)&&o<=i->second.length){auto&v=i->second;n=n?n:v.length-o;if(n<=v.length-o){if(f&D3DLOCK_DISCARD)v.lo=v.hi=0;v.offset=o;v.size=n;v.mapped=*p;v.flags=f;}}ReleaseSRWLockExclusive(&bufferLock);}return h;}
bool safe_copy(void*d,const void*s,size_t n){__try{memcpy(d,s,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
HRESULT WINAPI unlock(void*b){AcquireSRWLockExclusive(&bufferLock);auto i=buffers.find(b);if(i!=buffers.end()&&i->second.mapped){auto&v=i->second;if(v.size<=256*1024){UINT lo=v.offset,hi=v.offset+v.size;const bool merge=v.lo<v.hi&&lo<=v.hi&&hi>=v.lo&&std::max(hi,v.hi)-std::min(lo,v.lo)<=256*1024;if(merge){lo=std::min(lo,v.lo);hi=std::max(hi,v.hi);}if(lo==v.lo&&hi==v.hi){if(safe_copy(v.bytes.data()+v.offset-lo,v.mapped,v.size))++bufferCopies;else v.lo=v.hi=0;}else{std::vector<BYTE>data(hi-lo);if(merge)memcpy(data.data()+v.lo-lo,v.bytes.data(),v.hi-v.lo);if(safe_copy(data.data()+v.offset-lo,v.mapped,v.size)){v.bytes=std::move(data);v.lo=lo;v.hi=hi;++bufferCopies;}else v.lo=v.hi=0;}}else v.lo=v.hi=0;v.mapped=nullptr;}ReleaseSRWLockExclusive(&bufferLock);
 auto fn=reinterpret_cast<Unlock>(original(b,12,unlocks));return fn?fn(b):D3DERR_INVALIDCALL;}
bool hook(void*object,unsigned slot,void*replacement,Hook (&list)[8]){void*address=(*reinterpret_cast<void***>(object))[slot];AcquireSRWLockExclusive(&hookLock);for(auto&h:list)if(h.address==address){ReleaseSRWLockExclusive(&hookLock);return true;}for(auto&h:list)if(!h.address){if(MH_CreateHook(address,replacement,&h.original)!=MH_OK){ReleaseSRWLockExclusive(&hookLock);return false;}h.address.store(address,std::memory_order_release);MH_QueueEnableHook(address);const bool ok=MH_ApplyQueued()==MH_OK;ReleaseSRWLockExclusive(&hookLock);return ok;}ReleaseSRWLockExclusive(&hookLock);return false;}
void register_buffer(void*b,UINT size,DWORD usage){if(!b||!size||size>32*1024*1024)return;if(!hook(b,11,reinterpret_cast<void*>(lock),locks)||!hook(b,12,reinterpret_cast<void*>(unlock),unlocks))return;
 AcquireSRWLockExclusive(&bufferLock);if(buffers.size()>=16&&!buffers.contains(b))buffers.erase(buffers.begin());buffers[b]=Buffer{{},size};ReleaseSRWLockExclusive(&bufferLock);}
using CreateVB=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);using CreateIB=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*);CreateVB createVB{};CreateIB createIB{};
HRESULT WINAPI vb_created(IDirect3DDevice9*d,UINT n,DWORD u,DWORD f,D3DPOOL pool,IDirect3DVertexBuffer9**p,HANDLE*h){auto r=createVB(d,n,u,f,pool,p,h);if(d==owned&&SUCCEEDED(r)&&p)register_buffer(*p,n,u);return r;}
HRESULT WINAPI ib_created(IDirect3DDevice9*d,UINT n,DWORD u,D3DFORMAT f,D3DPOOL pool,IDirect3DIndexBuffer9**p,HANDLE*h){auto r=createIB(d,n,u,f,pool,p,h);if(d==owned&&SUCCEEDED(r)&&p)register_buffer(*p,n,u);return r;}
bool copied(void*b,UINT offset,UINT n,std::vector<BYTE>& out){bool ok=false;AcquireSRWLockShared(&bufferLock);auto i=buffers.find(b);if(i!=buffers.end()&&offset>=i->second.lo&&offset<=i->second.hi&&n<=i->second.hi-offset){out.resize(n);memcpy(out.data(),i->second.bytes.data()+offset-i->second.lo,n);ok=true;}ReleaseSRWLockShared(&bufferLock);return ok;}
bool buffer_bytes(IDirect3DDevice9*d,IUnknown*b,UINT at,UINT n,std::vector<BYTE>&out){out.resize(n);if(managed9::copy_buffer(d,b,at,n,out.data())){++managedReads;return true;}return copied(b,at,n,out);}
struct Point{float p[4]{0,0,0,1},c[4]{0,0,0,1},c1[4]{0,0,0,1};bool hasColour[2]{};};
bool element(const BYTE*p,BYTE t,float*out){switch(t){case D3DDECLTYPE_FLOAT1:case D3DDECLTYPE_FLOAT2:case D3DDECLTYPE_FLOAT3:case D3DDECLTYPE_FLOAT4:memcpy(out,p,4*(t+1));return true;case D3DDECLTYPE_SHORT2:case D3DDECLTYPE_SHORT4:{short v[4]{};const unsigned n=t==D3DDECLTYPE_SHORT2?2:4;memcpy(v,p,2*n);for(unsigned i=0;i<n;++i)out[i]=float(v[i]);return true;}case D3DDECLTYPE_UBYTE4N:for(unsigned i=0;i<4;++i)out[i]=p[i]/255.f;return true;case D3DDECLTYPE_UBYTE4:for(unsigned i=0;i<4;++i)out[i]=float(p[i]);return true;case D3DDECLTYPE_D3DCOLOR:{DWORD c;memcpy(&c,p,4);out[0]=((c>>16)&255)/255.f;out[1]=((c>>8)&255)/255.f;out[2]=(c&255)/255.f;out[3]=(c>>24)/255.f;return true;}default:return false;}}
unsigned element_size(BYTE t){return t<=D3DDECLTYPE_FLOAT4?4*(t+1):t==D3DDECLTYPE_SHORT4?8:4;}
bool colour(const Shader&s,const Point&point,const float constants[32][4],float*out){float temp[32][4]{},defs[32][4]{};bool defined[32]{};bool output=false;float result[4]{};
 for(const auto&i:s.ops){if(i.op==D3DSIO_DEF){const unsigned r=index(i.p[0]);if(r>=32)return false;memcpy(defs[r],i.p+1,16);defined[r]=true;continue;}float args[3][4]{};
  for(unsigned a=1;a<i.n;++a){const DWORD token=i.p[a];const unsigned r=index(token),t=type(token);const float*src=nullptr;if(t==D3DSPR_CONST&&r<32)src=defined[r]?defs[r]:constants[r];else if(t==D3DSPR_TEMP&&r<32)src=temp[r];else if(t==D3DSPR_INPUT&&r<16&&s.inputColour[r]>=0&&point.hasColour[s.inputColour[r]])src=s.inputColour[r]?point.c1:point.c;else return false;
   const unsigned mod=(token>>24)&15;if(mod>1)return false;for(unsigned k=0;k<4;++k)args[a-1][k]=src[(token>>(16+2*k))&3]*(mod==1?-1:1);}
  const unsigned r=index(i.p[0]),t=type(i.p[0]);float*v=nullptr;if(t==D3DSPR_TEMP&&r<32)v=temp[r];else if(t==D3DSPR_COLOROUT&&r==0){v=result;output=true;}else return false;
  for(unsigned k=0;k<4;++k)if(i.p[0]&(1u<<(16+k))){float x{};switch(i.op){case D3DSIO_MOV:x=args[0][k];break;case D3DSIO_ADD:x=args[0][k]+args[1][k];break;case D3DSIO_MUL:x=args[0][k]*args[1][k];break;case D3DSIO_MAD:x=args[0][k]*args[1][k]+args[2][k];break;case D3DSIO_LOG:x=std::log2(std::fabs(args[0][0]));break;case D3DSIO_EXP:x=std::exp2(args[0][0]);break;default:return false;}v[k]=(i.p[0]&(1u<<20))?std::clamp(x,0.f,1.f):x;}
 }
 if(!output){memcpy(result,temp[0],16);}for(unsigned k=0;k<4;++k){out[k]=result[k];if(!std::isfinite(out[k]))return false;}return true;
}
struct Geometry{std::vector<Point> points;std::vector<unsigned> indices;};
bool geometry(IDirect3DDevice9*d,const Draw& draw,Geometry&g){
 const UINT n=draw.type==D3DPT_TRIANGLELIST?draw.count*3:draw.type==D3DPT_TRIANGLESTRIP?draw.count+2:draw.type==D3DPT_LINELIST?draw.count*2:draw.type==D3DPT_LINESTRIP?draw.count+1:0;if(!n||n>24000)return false;
 ComPtr<IDirect3DVertexDeclaration9> declaration;UINT count=64;D3DVERTEXELEMENT9 e[64]{};if(FAILED(d->GetVertexDeclaration(&declaration))||!declaration||FAILED(declaration->GetDeclaration(e,&count)))return false;
 int pos=-1,col=-1,col1=-1;for(unsigned i=0;i<count&&e[i].Stream!=0xff;++i)if(e[i].Stream==0){if(e[i].Usage==D3DDECLUSAGE_POSITION&&e[i].UsageIndex==0)pos=i;if(e[i].Usage==D3DDECLUSAGE_COLOR&&e[i].UsageIndex==0)col=i;if(e[i].Usage==D3DDECLUSAGE_COLOR&&e[i].UsageIndex==1)col1=i;}if(pos<0)return false;
 UINT stride=draw.stride,offset{},minimum=draw.indexed?draw.min:draw.start,vertices=draw.indexed?draw.num:n;ComPtr<IDirect3DVertexBuffer9> vb;ComPtr<IDirect3DIndexBuffer9> ib;std::vector<BYTE> vertexBytes,indexBytes;
 if(!vertices||vertices>24000)return false;const BYTE* data=static_cast<const BYTE*>(draw.vertices);
 if(!data){if(FAILED(d->GetStreamSource(0,&vb,&offset,&stride))||!vb)return false;const int64_t start=int64_t(draw.indexed?draw.base:0)+minimum;if(start<0||start>1000000||!stride||stride>128)return false;const uint64_t byteStart=offset+uint64_t(start)*stride,bytes=uint64_t(vertices)*stride;if(byteStart>UINT_MAX||bytes>256*1024||!buffer_bytes(d,vb.Get(),UINT(byteStart),UINT(bytes),vertexBytes))return false;data=vertexBytes.data();}
 else {if(!stride||stride>128||uint64_t(vertices)*stride>256*1024)return false;vertexBytes.resize(vertices*stride);if(!safe_copy(vertexBytes.data(),data+minimum*stride,vertexBytes.size()))return false;data=vertexBytes.data();}
 if(e[pos].Offset+element_size(e[pos].Type)>stride||(col>=0&&e[col].Offset+element_size(e[col].Type)>stride)||(col1>=0&&e[col1].Offset+element_size(e[col1].Type)>stride))return false;
 g.points.resize(vertices);for(unsigned i=0;i<vertices;++i){auto&p=g.points[i];if(!element(data+i*stride+e[pos].Offset,e[pos].Type,p.p)||(col>=0&&!element(data+i*stride+e[col].Offset,e[col].Type,p.c))||(col1>=0&&!element(data+i*stride+e[col1].Offset,e[col1].Type,p.c1)))return false;p.hasColour[0]=col>=0;p.hasColour[1]=col1>=0;}
 g.indices.resize(n);if(!draw.indexed){for(unsigned i=0;i<n;++i)g.indices[i]=i;return true;}
 const BYTE* indices=static_cast<const BYTE*>(draw.indices);D3DFORMAT format=draw.format;
 if(!indices){D3DINDEXBUFFER_DESC desc{};if(FAILED(d->GetIndices(&ib))||!ib||FAILED(ib->GetDesc(&desc)))return false;format=desc.Format;const UINT bytes=format==D3DFMT_INDEX16?2:format==D3DFMT_INDEX32?4:0;if(!bytes||!buffer_bytes(d,ib.Get(),draw.indexStart*bytes,n*bytes,indexBytes))return false;indices=indexBytes.data();}
 for(unsigned i=0;i<n;++i){unsigned v{};if(format==D3DFMT_INDEX16){WORD w;memcpy(&w,indices+i*2,2);v=w;}else if(format==D3DFMT_INDEX32)memcpy(&v,indices+i*4,4);else return false;if(v<minimum||v-minimum>=vertices)return false;g.indices[i]=v-minimum;}return true;
}
enum class Kind{other,fade,bat};
Kind classify(const Geometry&g,const Draw&draw,const Shader&vs,const Shader&ps,const float*m,const float constants[32][4],bool multiply=false,bool continuing=false,float* fade=nullptr,bool* blackRectangle=nullptr){
 struct Sample{float x{},y{},dark{-1};};std::vector<Sample>samples(g.points.size());
 float xmin=100,xmax=-100,ymin=100,ymax=-100,maxDark=0,referenceW=0;bool haveW=false;
 for(unsigned at:g.indices){auto&s=samples[at];if(s.dark>=0)continue;const auto&v=g.points[at];float c[4]{},clip[4]{};
  if(!colour(ps,v,constants,c)||c[3]<-.001f||c[3]>1.001f)return Kind::other;
  if(multiply){if(std::fabs(c[0]-c[1])>.002f||std::fabs(c[0]-c[2])>.002f||c[0]<-.001f||c[0]>1.001f)return Kind::other;s.dark=1-std::clamp(c[0],0.f,1.f);}
  else{if(c[3]>.001f&&(std::fabs(c[0])>.002f||std::fabs(c[1])>.002f||std::fabs(c[2])>.002f))return Kind::other;s.dark=std::clamp(c[3],0.f,1.f);}
  for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)clip[j]+=v.p[k]*m[vs.rows?j*4+k:k*4+j];if(!std::isfinite(clip[3])||std::fabs(clip[3])<.001f)return Kind::other;
  if(!haveW){referenceW=clip[3];haveW=true;}else if(std::fabs(clip[3]-referenceW)>std::fabs(referenceW)*.001f)return Kind::other;
  s.x=clip[0]/clip[3];s.y=clip[1]/clip[3];if(!std::isfinite(s.x)||!std::isfinite(s.y))return Kind::other;
  xmin=std::min(xmin,s.x);xmax=std::max(xmax,s.x);ymin=std::min(ymin,s.y);ymax=std::max(ymax,s.y);maxDark=std::max(maxDark,s.dark);
 }
 if(maxDark<.001f)return Kind::other;
 // Actual Asylum meshes include a 10-triangle AA rectangle and a16:9 stage
 // at clipY+/-0.747; a2-triangle +/-0.9 rectangle is not the game's contract.
 float left=100,right=-100,bottom=100,top=-100;const float core=maxDark-.002f;
 for(const auto&s:samples)if(s.dark>=core){left=std::min(left,s.x);right=std::max(right,s.x);bottom=std::min(bottom,s.y);top=std::max(top,s.y);}
 bool rectangle=draw.count<=32&&left<=-.9f&&right>=.9f&&bottom<=-.65f&&top>=.65f;unsigned corners=0;
 const float bx=.012f,by=.012f; // <=about12 pixels at current source; AA fringe only.
 if(rectangle)for(const auto&s:samples)if(s.dark>=0){const bool l=std::fabs(s.x-left)<=bx,r=std::fabs(s.x-right)<=bx,b=std::fabs(s.y-bottom)<=by,t=std::fabs(s.y-top)<=by;
  if(s.dark>=core){if(!(l||r||b||t)&&!(std::fabs(s.x-(left+right)*.5f)<=bx&&std::fabs(s.y-(bottom+top)*.5f)<=by)){rectangle=false;break;}if((l||r)&&(b||t))corners|=1u<<((r?1:0)+(t?2:0));}
  else if(s.x<left-bx||s.x>right+bx||s.y<bottom-by||s.y>top+by||!(l||r||b||t)){rectangle=false;break;}
 }
 double area=0;if(rectangle&&corners==15){for(unsigned tri=0;tri<draw.count;++tri){const unsigned i=draw.type==D3DPT_TRIANGLELIST?tri*3:tri;const auto&a=samples[g.indices[i]],&b=samples[g.indices[i+1]],&c=samples[g.indices[i+2]];if(a.dark>=core&&b.dark>=core&&c.dark>=core)area+=std::fabs(double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x))*.5;}
  const double box=double(right-left)*(top-bottom);rectangle=box>0&&std::fabs(area-box)<=box*.007;
 }else rectangle=false;
 if(rectangle){if(fade)*fade=maxDark;if(blackRectangle)*blackRectangle=true;return continuing&&maxDark>.999f?Kind::bat:Kind::fade;}
 if(continuing&&draw.count>=8)return Kind::bat;
 const bool coversStage=xmin<=-.9f&&xmax>=.9f&&ymin<=-.65f&&ymax>=.65f;
 // LIVE sweep begins at one edge, before it spans the authored stage:
 // count288/bounds[-1.4246,-.7441]x[-.5907,.3766], alpha1/stencil0.
 const bool edgeSweep=draw.count>=128&&maxDark>.999f&&(xmin<-1.2f||xmax>1.2f)&&ymin<-.1f&&ymax>.1f&&ymax-ymin>=.65f;
 if(draw.count>=8&&maxDark>.999f&&(coversStage||edgeSweep))return Kind::bat;
 return Kind::other;
}
// UPGRADEBLACK (JJ 2026-10-20: "Going into the upgrades section of the menu ... just shows a completely black screen ...
// on desktop it's there"; LIVE 18:48:30-18:49:37: batDraws +1 per frame for over a minute with no bat transition). The bat
// sweep changes its mesh every frame (startup log: 288, 439, 686 ... primitives); a menu's black backdrop / frame matches
// the same "opaque black, covers the stage" test but repeats one mesh. A bat-classified mesh + placement seen unchanged
// kStaticFrames times, each within 250 ms of the last, is menu artwork: it stays in the menu, not the full-view bat layer.
constexpr unsigned kStaticFrames=10;
bool staticArtwork=true;unsigned staticKept{};
struct StaticSeen{uint64_t key;unsigned count;ULONGLONG at;};StaticSeen staticSeen[8]{};
bool static_artwork(const Geometry&g,const Draw&draw,const float*transform,ULONGLONG now){
 if(!staticArtwork)return false;
 uint64_t h=1469598103934665603ull;auto mix=[&h](const void*p,size_t n){const BYTE*c=static_cast<const BYTE*>(p);for(size_t i=0;i<n;++i){h^=c[i];h*=1099511628211ull;}};
 mix(&draw.count,sizeof(draw.count));mix(transform,64);for(const auto&pt:g.points)mix(pt.p,sizeof(pt.p));
 StaticSeen*slot=nullptr;for(auto&s:staticSeen)if(s.key==h&&s.count&&now-s.at<250){slot=&s;break;}
 if(!slot){slot=&staticSeen[0];for(auto&s:staticSeen)if(s.at<slot->at)slot=&s;*slot={h,0,now};}
 slot->at=now;if(++slot->count<kStaticFrames)return false;
 if(slot->count==kStaticFrames&&staticKept++<8)BVR_LOG("[aa transitions] UPGRADEBLACK: static black menu artwork kept in the menu, not the bat layer: primitives%u vertices%u, unchanged %u times",draw.count,unsigned(g.points.size()),kStaticFrames);
 return true;
}
void geometry_evidence(IDirect3DDevice9*d,const Geometry&g,const Draw&draw,const Shader&vs,const Shader&ps,const float*m,const float constants[32][4],Kind kind){
 static unsigned saved{};static uint64_t keys[16]{};if(fixtureW||saved>=16||g.indices.empty())return;float c[4]{};if(!colour(ps,g.points[g.indices.front()],constants,c)||std::fabs(c[0])>.002f||std::fabs(c[1])>.002f||std::fabs(c[2])>.002f||c[3]<.001f)return;
 float xmin=100,xmax=-100,ymin=100,ymax=-100;for(unsigned n:g.indices){const auto&p=g.points[n].p;float x=0,y=0,w=0;for(unsigned k=0;k<4;++k){x+=p[k]*m[vs.rows?k:k*4];y+=p[k]*m[vs.rows?4+k:k*4+1];w+=p[k]*m[vs.rows?12+k:k*4+3];}if(std::fabs(w)<.001f||!std::isfinite(w))return;x/=w;y/=w;xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);}if(xmax-xmin<.6f||ymax-ymin<.6f)return;
 const uint64_t key=(uint64_t(draw.count)<<8)|(unsigned(kind)<<4)|(unsigned(vs.matrix)&15);for(unsigned n=0;n<saved;++n)if(keys[n]==key)return;keys[saved++]=key;DWORD stencil{},compare{},ref{};d->GetRenderState(D3DRS_STENCILENABLE,&stencil);d->GetRenderState(D3DRS_STENCILFUNC,&compare);d->GetRenderState(D3DRS_STENCILREF,&ref);
 BVR_LOG("[aa UI geometry] kind%u (0retained/1fade/2bat) eye%d gen%u primitives%u indexed%u vertices%u blackAlpha%.4f clipBounds %.5f..%.5f %.5f..%.5f transformC%d stencil%u compare%u ref%u; existing CPU geometry",unsigned(kind),eye,gen,draw.count,draw.indexed,unsigned(g.points.size()),c[3],xmin,xmax,ymin,ymax,vs.matrix,stencil,compare,ref);
}
bool ensure(){UINT w=fixtureW,h=fixtureH;if(!w&&!vr::output_eye_size(&w,&h)){D3DVIEWPORT9 vp{};owned->GetViewport(&vp);w=vp.Width;h=vp.Height;}if(w<32||h<32||w>8192||h>8192)return false;D3DSURFACE_DESC old{};if(target&&SUCCEEDED(target->GetDesc(&old))&&old.Width==w&&old.Height==h)return true;
 image.Reset();target.Reset();depth.Reset();return SUCCEEDED(owned->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&image,nullptr))&&SUCCEEDED(image->GetSurfaceLevel(0,&target))&&SUCCEEDED(owned->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));}
}
bool inspect_ui(IDirect3DDevice9*d,const Draw&draw,std::vector<UiVertex>&points,std::vector<unsigned>&indices){
 if(draw.count>128)return false;ComPtr<IDirect3DVertexShader9>v;ComPtr<IDirect3DPixelShader9>p;d->GetVertexShader(&v);d->GetPixelShader(&p);if(!v||!p)return false;const auto vs=shader(v.Get()),ps=shader(p.Get());if(!vs.supported||!ps.supported)return false;
 float m[16]{},constants[32][4]{};if(vs.matrix>=0){if(FAILED(d->GetVertexShaderConstantF(vs.matrix,m,4)))return false;}else if(vs.clipPosition){m[0]=m[5]=m[10]=m[15]=1;}else return false;
 if(FAILED(d->GetPixelShaderConstantF(0,&constants[0][0],32)))return false;Geometry g;if(!geometry(d,draw,g))return false;points.resize(g.points.size());for(size_t i=0;i<g.points.size();++i){auto&o=points[i];float clip[4]{};const auto&point=g.points[i];if(!colour(ps,point,constants,o.colour))return false;for(int j=0;j<4;++j)for(int k=0;k<4;++k)clip[j]+=point.p[k]*m[vs.rows?j*4+k:k*4+j];if(!std::isfinite(clip[3])||clip[3]<=.001f)return false;o.x=clip[0]/clip[3];o.y=clip[1]/clip[3];if(!std::isfinite(o.x)||!std::isfinite(o.y))return false;}
 indices=std::move(g.indices);return true;
}
Scope::Scope(IDirect3DDevice9*d,const Draw&draw,bool uiLane,bool skippedRight){
 if(draw.type!=D3DPT_TRIANGLELIST&&draw.type!=D3DPT_TRIANGLESTRIP)return;
 if(d!=owned||internal||skippedRight||(!enabled&&!removeFades))return;
 if(!uiLane){ComPtr<IDirect3DSurface9>bb,rt;d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb);d->GetRenderTarget(0,&rt);if(!bb||bb.Get()!=rt.Get())return;}
 DWORD z{},writes{};d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes);
 bool depthIndependent=!z;if(z&&uiLane){DWORD depthWrite{},compare{};d->GetRenderState(D3DRS_ZWRITEENABLE,&depthWrite);d->GetRenderState(D3DRS_ZFUNC,&compare);depthIndependent=!depthWrite&&compare==D3DCMP_ALWAYS;}
 if(!depthIndependent||!(writes&7)){++stateRejects;if(uiLane){ComPtr<IDirect3DVertexShader9>v;ComPtr<IDirect3DPixelShader9>p;d->GetVertexShader(&v);d->GetPixelShader(&p);rejected_ui(d,v.Get(),p.Get(),draw);}return;}
 ComPtr<IDirect3DVertexShader9>v;ComPtr<IDirect3DPixelShader9>p;if(FAILED(d->GetVertexShader(&v))||FAILED(d->GetPixelShader(&p))||!v||!p){++shaderRejects;if(uiLane)rejected_ui(d,v.Get(),p.Get(),draw);return;}const auto vs=shader(v.Get());const auto ps=shader(p.Get());if(!vs.supported||!ps.supported){++shaderRejects;if(uiLane)rejected_ui(d,v.Get(),p.Get(),draw);return;}
 if(vs.clipPosition&&vs.matrix<0){const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};memcpy(transform,identity,64);}else if(FAILED(d->GetVertexShaderConstantF(vs.matrix,transform,4))){++constantRejects;return;}float constants[32][4]{};if(FAILED(d->GetPixelShaderConstantF(0,&constants[0][0],32))){++constantRejects;return;}Geometry g;if(!geometry(d,draw,g)){++unknown;return;}++reads;
 DWORD blend{},src{},dst{};d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);const bool multiply=blend&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR));DWORD activeStencil{};d->GetRenderState(D3DRS_STENCILENABLE,&activeStencil);const bool composite=!blend||multiply||((src==D3DBLEND_SRCALPHA||src==D3DBLEND_ONE||src==D3DBLEND_ZERO)&&dst==D3DBLEND_INVSRCALPHA);if(activeStencil||!composite){geometry_evidence(d,g,draw,vs,ps,transform,constants,Kind::other);return;}const auto now=GetTickCount64();const bool continuing=haveBatStage&&now-lastBat<250&&now-batEpisodeAt<2000&&!memcmp(batStage,transform,64);float opacity{};bool blackRectangle=false;auto kind=classify(g,draw,vs,ps,transform,constants,multiply,continuing,&opacity,&blackRectangle);
 // UPGRADEBLACK2 (LIVE 19:03:59: the 10-triangle full-stage rectangle of a running bat sweep was kept as menu art): a
 // black rectangle counts as a bat only inside a running sweep (at most 2 s) - the sweep's own black hold, never menu art.
 if(kind==Kind::bat&&!blackRectangle&&static_artwork(g,draw,transform,now))kind=Kind::other;geometry_evidence(d,g,draw,vs,ps,transform,constants,kind);if(kind==Kind::fade&&removeFades){if(eye<=0)pairFadeAlpha=std::max(pairFadeAlpha,opacity);skip=true;++removed;return;}if(kind!=Kind::bat||!enabled||vs.matrix<0)return;
 if(!ensure()){++captureFailures;return;}device=d;reg=vs.matrix;d->GetRenderTarget(0,&rt);d->GetDepthStencilSurface(&ds);d->GetViewport(&viewport);d->GetScissorRect(&scissor);for(unsigned i=0;i<8;++i)d->GetRenderState(stateKeys[i],this->states+i);
 d->SetDepthStencilSurface(nullptr);if(FAILED(d->SetRenderTarget(0,target.Get()))||FAILED(d->SetDepthStencilSurface(depth.Get()))){d->SetRenderTarget(0,rt);d->SetDepthStencilSurface(ds);++captureFailures;device=nullptr;return;}
 changed=true;D3DSURFACE_DESC size{};target->GetDesc(&size);D3DVIEWPORT9 vp{0,0,size.Width,size.Height,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
 if(!painted)d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0);
 float zoomed[16];memcpy(zoomed,transform,64);for(unsigned i=0;i<4;++i){zoomed[vs.rows?i:i*4]*=1.6f;zoomed[vs.rows?4+i:i*4+1]*=1.6f;}if(FAILED(d->SetVertexShaderConstantF(reg,zoomed,4))){++captureFailures;d->SetVertexShaderConstantF(reg,transform,4);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,rt);d->SetDepthStencilSurface(ds);d->SetViewport(&viewport);d->SetScissorRect(&scissor);for(unsigned i=0;i<8;++i)d->SetRenderState(stateKeys[i],this->states[i]);changed=false;return;}painted=true;if(!continuing){memcpy(batStage,transform,64);haveBatStage=true;batEpisodeAt=now;}lastBat=now;++bats;
}
Scope::~Scope(){if(changed){device->SetVertexShaderConstantF(reg,transform,4);device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,rt);device->SetDepthStencilSurface(ds);device->SetViewport(&viewport);device->SetScissorRect(&scissor);for(unsigned i=0;i<8;++i)device->SetRenderState(stateKeys[i],states[i]);}if(rt)rt->Release();if(ds)ds->Release();}
bool install(IDirect3DDevice9*d){if(owned)return owned==d;owned=d;auto**v=*reinterpret_cast<void***>(d);const unsigned slots[]={26,27,106};void*hooks[]={reinterpret_cast<void*>(vb_created),reinterpret_cast<void*>(ib_created),reinterpret_cast<void*>(ps_created)};void**out[]={reinterpret_cast<void**>(&createVB),reinterpret_cast<void**>(&createIB),reinterpret_cast<void**>(&createPS)};
 for(unsigned i=0;i<3;++i){if(MH_CreateHook(v[slots[i]],hooks[i],out[i])!=MH_OK)return false;MH_QueueEnableHook(v[slots[i]]);}if(MH_ApplyQueued()!=MH_OK)return false;swprintf_s(settings,L"%s\\aa_rendering.ini",log::data_dir());enabled=GetPrivateProfileIntW(L"AsylumVR",L"FullViewBats",1,settings)!=0;removeFades=GetPrivateProfileIntW(L"AsylumVR",L"RemovePlainFades",1,settings)!=0;fullViewFades=GetPrivateProfileIntW(L"AsylumVR",L"FullViewMenuFades",1,settings)!=0;BVR_LOG("[aa transitions] uniform black full-stage rectangles omitted; original complex black geometry -> XR-size near-face layer; bounded CPU UI buffer uploads; authored fade alpha -> full-view background before HUD");return true;}
void vertex_created(IDirect3DVertexShader9*s){AcquireSRWLockExclusive(&shaderLock);shaders.erase(s);ReleaseSRWLockExclusive(&shaderLock);}
void begin_eye(int e,uint32_t generation){if(e<=0||generation!=gen)pairFadeAlpha=0;eye=e;gen=generation;if(e<=0)painted=false;}
float fade_alpha(uint32_t generation){return fullViewFades&&removeFades&&generation==gen?pairFadeAlpha:0;}
IDirect3DSurface9* sheet(){return eye<=0&&painted?target.Get():nullptr;}uint32_t generation(){return gen;}
void reset(){memset(staticSeen,0,sizeof(staticSeen));pairFadeAlpha=0;painted=false;lastBat=0;haveBatStage=false;batEpisodeAt=0;image.Reset();target.Reset();depth.Reset();AcquireSRWLockExclusive(&shaderLock);shaders.clear();ReleaseSRWLockExclusive(&shaderLock);AcquireSRWLockExclusive(&bufferLock);buffers.clear();ReleaseSRWLockExclusive(&bufferLock);}
void fallback(IDirect3DSurface9* destination){if(!owned||!image||!destination)return;internal=true;ComPtr<IDirect3DStateBlock9>s;ComPtr<IDirect3DSurface9>rt,ds;if(FAILED(owned->CreateStateBlock(D3DSBT_ALL,&s))){internal=false;return;}owned->GetRenderTarget(0,&rt);owned->GetDepthStencilSurface(&ds);D3DSURFACE_DESC desc{};destination->GetDesc(&desc);if(FAILED(owned->BeginScene())){internal=false;return;}owned->SetDepthStencilSurface(nullptr);owned->SetRenderTarget(0,destination);D3DVIEWPORT9 vp{0,0,desc.Width,desc.Height,0,1};owned->SetViewport(&vp);owned->SetVertexShader(nullptr);owned->SetPixelShader(nullptr);owned->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);owned->SetTexture(0,image.Get());owned->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);owned->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);owned->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);owned->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);owned->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);owned->SetRenderState(D3DRS_ZENABLE,FALSE);owned->SetRenderState(D3DRS_STENCILENABLE,FALSE);owned->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);owned->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);owned->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);owned->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);owned->SetRenderState(D3DRS_COLORWRITEENABLE,15);struct V{float x,y,z,w,u,v;};const V v[]={{-.5f,-.5f,0,1,0,0},{float(desc.Width)-.5f,-.5f,0,1,1,0},{-.5f,float(desc.Height)-.5f,0,1,0,1},{float(desc.Width)-.5f,float(desc.Height)-.5f,0,1,1,1}};owned->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V));owned->EndScene();owned->SetDepthStencilSurface(nullptr);owned->SetRenderTarget(0,rt.Get());owned->SetDepthStencilSurface(ds.Get());s->Apply();internal=false;}
void status_line(char*out,size_t bytes){if(out&&bytes)snprintf(out,bytes,"fullViewBats=%u removePlainFades=%u staticMenuArt=%u/%u blackRectsRemoved=%u batDraws=%u geometryReads=%u missingUpload=%u failedCapture=%u CPUuploads=%u shaderRejects=%u stateRejects=%u constantRejects=%u managedReads=%u",enabled,removeFades,staticArtwork,staticKept,removed,bats,reads,unknown,captureFailures,bufferCopies,shaderRejects,stateRejects,constantRejects,managedReads);}
void status(){char line[320]{};status_line(line,sizeof(line));BVR_LOG("[aa transitions] %s",line);BVR_LOG("[aa menu fade] enabled%u gen%u alpha%.4f; engine envelope, behind HUD",fullViewFades,gen,fade_alpha(gen));}
void draw_ui(){bool b=enabled,f=removeFades,m=fullViewFades;if(ImGui::Checkbox("Full-view fades behind menu text",&m)){fullViewFades=m;WritePrivateProfileStringW(L"AsylumVR",L"FullViewMenuFades",m?L"1":L"0",settings);}if(ImGui::Checkbox("Full-view bat transitions",&b)){enabled=b;WritePrivateProfileStringW(L"AsylumVR",L"FullViewBats",b?L"1":L"0",settings);}bool st=staticArtwork;if(ImGui::Checkbox("Keep still black menu art in the menu (upgrades screen)",&st))staticArtwork=st;if(ImGui::Checkbox("Remove windowed black fades",&f)){removeFades=f;WritePrivateProfileStringW(L"AsylumVR",L"RemovePlainFades",f?L"1":L"0",settings);}}
bool fixture(IDirect3DDevice9*d,const wchar_t*directory,bool (*completed)(IDirect3DSurface9*)){
 if(!install(d))return false;
 {// UPGRADEBLACK: one repeated mesh is kept in the menu from its 10th draw; a sweep changing every frame never is; a pause
  // over 250 ms starts the count again.
  Geometry g;g.points.resize(4);g.indices={0,1,2};Draw draw{};draw.count=8;const float t[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  ULONGLONG at=1000000;bool ok=true;memset(staticSeen,0,sizeof(staticSeen));
  for(unsigned i=1;i<=12;++i)ok=static_artwork(g,draw,t,at+=16)==(i>=kStaticFrames)&&ok;
  for(unsigned i=0;i<30;++i){draw.count=288+i;ok=!static_artwork(g,draw,t,at+=16)&&ok;}
  draw.count=8;ok=!static_artwork(g,draw,t,at+=400)&&ok;
  g.points[1].p[0]=.5f;for(unsigned i=0;i<5;++i)ok=!static_artwork(g,draw,t,at+=16)&&ok;
  memset(staticSeen,0,sizeof(staticSeen));staticKept=0;
  if(!ok){printf("Static menu art (UPGRADEBLACK) FAIL\n");return false;}
  printf("Static menu art (UPGRADEBLACK): repeated black mesh kept in the menu from draw %u, changing sweep stays a bat, 250 ms gap restarts PASS\n",kStaticFrames);
 }
 ComPtr<IDirect3DStateBlock9>state;ComPtr<IDirect3DSurface9>originalRt,originalDs;D3DVIEWPORT9 originalVp{};if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&state))||FAILED(d->GetRenderTarget(0,&originalRt))||FAILED(d->GetViewport(&originalVp)))return false;d->GetDepthStencilSurface(&originalDs);
 const bool oldEnabled=enabled,oldFades=removeFades,oldFullFades=fullViewFades;struct Restore{IDirect3DDevice9*d;IDirect3DStateBlock9*s;IDirect3DSurface9*rt,*ds;D3DVIEWPORT9 vp;bool b,f,m;~Restore(){d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,rt);d->SetDepthStencilSurface(ds);d->SetViewport(&vp);s->Apply();fixtureW=fixtureH=0;enabled=b;removeFades=f;fullViewFades=m;reset();}}restore{d,state.Get(),originalRt.Get(),originalDs.Get(),originalVp,oldEnabled,oldFades,oldFullFades};enabled=removeFades=fullViewFades=true;fixtureW=192;fixtureH=208;
 auto load=[&](const wchar_t*name,std::vector<DWORD>&out){wchar_t path[MAX_PATH]{};swprintf_s(path,L"%s\\%s.bin",directory,name);FILE*f=_wfopen(path,L"rb");if(!f)return false;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<=0||n%4||n>32768){fclose(f);return false;}out.resize(n/4);bool ok=fread(out.data(),1,n,f)==n;fclose(f);return ok;};
 std::vector<DWORD>v,p;if(!load(L"ui-solid-vs",v)||!load(L"ui-constant-ps",p))return false;ComPtr<IDirect3DVertexShader9>vs;ComPtr<IDirect3DPixelShader9>ps;if(FAILED(d->CreateVertexShader(v.data(),&vs))||FAILED(d->CreatePixelShader(p.data(),&ps)))return false;
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,1},D3DDECL_END()};ComPtr<IDirect3DVertexDeclaration9>decl;ComPtr<IDirect3DTexture9>scene;ComPtr<IDirect3DSurface9>rt,readback;
 if(FAILED(d->CreateVertexDeclaration(elements,&decl))||FAILED(d->CreateTexture(128,128,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scene,nullptr))||FAILED(scene->GetSurfaceLevel(0,&rt))||FAILED(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr)))return false;
 d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,rt.Get());D3DVIEWPORT9 viewport{0,0,128,128,0,1};d->SetViewport(&viewport);d->SetVertexDeclaration(decl.Get());d->SetVertexShader(vs.Get());d->SetPixelShader(ps.Get());d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
 const float matrix[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};d->SetVertexShaderConstantF(5,matrix,4);float black[]={0,0,0,1};d->SetPixelShaderConstantF(0,black,1);
 struct Vertex{float x,y,z,w,u,v;DWORD colour,colour1;};auto vertex=[](float x,float y){return Vertex{x,y,0,1,0,0,0xffffffff,0xffffffff};};Vertex quad[]={vertex(-1,1),vertex(1,1),vertex(-1,-1),vertex(1,-1)};
 Draw data{};data.type=D3DPT_TRIANGLESTRIP;data.count=2;data.vertices=quad;data.stride=sizeof(Vertex);
 auto read=[&](IDirect3DSurface9*from,std::vector<BYTE>&bytes){D3DSURFACE_DESC desc{};from->GetDesc(&desc);ComPtr<IDirect3DSurface9>cpu;if(FAILED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr))||FAILED(d->GetRenderTargetData(from,cpu.Get())))return false;D3DLOCKED_RECT m{};if(FAILED(cpu->LockRect(&m,nullptr,D3DLOCK_READONLY)))return false;bytes.resize(desc.Width*desc.Height*4);for(unsigned y=0;y<desc.Height;++y)memcpy(bytes.data()+y*desc.Width*4,static_cast<BYTE*>(m.pBits)+y*m.Pitch,desc.Width*4);cpu->UnlockRect();return true;};
 unsigned cases{};
 // Original game fade envelope: no polling or arbitrary per-button timer.
 // Both eyes use the LEFT sample, and a new render clears absent fades even
 // when the runtime repeats a pose generation. Small alpha stays small.
 const float envelope[]={0,.125f,.5f,1,1,.5f,.125f,0};
 for(unsigned i=0;i<8;++i){const uint32_t slot=970+i;begin_eye(-1,slot);black[3]=envelope[i];d->SetPixelShaderConstantF(0,black,1);
  {Scope scope(d,data,true,false);if(scope.skip!=(envelope[i]>0)||scope.changed)return false;}
  if(std::fabs(fade_alpha(slot)-envelope[i])>.0001f||fade_alpha(slot+1)!=0)return false;
  begin_eye(1,slot);if(std::fabs(fade_alpha(slot)-envelope[i])>.0001f)return false;
  {Scope scope(d,data,true,true);if(scope.skip||scope.changed)return false;}
  if(std::fabs(fade_alpha(slot)-envelope[i])>.0001f)return false;
  fullViewFades=false;if(fade_alpha(slot)!=0)return false;fullViewFades=true;
  begin_eye(1,slot+1);if(fade_alpha(slot+1)!=0)return false;
  begin_eye(-1,slot);if(fade_alpha(slot)!=0)return false;++cases;
 }
 reset();if(fade_alpha(977)!=0)return false;black[3]=1;d->SetPixelShaderConstantF(0,black,1);
 puts("Authored menu fade envelope: rising/peak/falling/zero from target shader; same pair alpha in both eyes, stale generation/repeated-generation empty render/reset/toggle clear; no synthetic black flashes PASS");
 std::vector<DWORD>gammaCode,gammaVSCode;if(!load(L"ui-solid-ps",gammaCode)||!load(L"ui-3a84",gammaVSCode))return false;const auto gammaBinding=parse(gammaCode);if(!gammaBinding.supported)return false;
 // Reproduce A1x's early parse rejection: LOG/EXP were unsupported there.
 bool oldReject=false;for(const auto&i:gammaBinding.ops)oldReject=oldReject||i.op==D3DSIO_LOG||i.op==D3DSIO_EXP;if(!oldReject)return false;
 ComPtr<IDirect3DPixelShader9>gammaPS;ComPtr<IDirect3DVertexShader9>gammaVS;if(FAILED(d->CreatePixelShader(gammaCode.data(),&gammaPS))||FAILED(d->CreateVertexShader(gammaVSCode.data(),&gammaVS)))return false;
 d->SetVertexShader(gammaVS.Get());d->SetPixelShader(gammaPS.Get());const float depthScale[]={.25f,0,0,0};d->SetVertexShaderConstantF(9,depthScale,1);
 for(float alpha:{.15f,.55f,1.f}){Vertex dark[4];for(unsigned i=0;i<4;++i){dark[i]=vertex(quad[i].x,quad[i].y*.9190523f);dark[i].colour=D3DCOLOR_ARGB(unsigned(alpha*255),0,0,0);}data.vertices=dark;const float gammaConstants[3][4]={{1,1,1,1},{0,0,0,0},{1,0,0,0}};d->SetPixelShaderConstantF(0,&gammaConstants[0][0],3);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO);lastBat=0;begin_eye(-1,920);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>before,after,control;if(!read(rt.Get(),before)||FAILED(d->BeginScene()))return false;
  {Scope scope(d,data,true,false);if(!scope.skip||scope.changed)return false;}d->EndScene();if(!read(rt.Get(),after)||after!=before)return false;removeFades=false;d->BeginScene();{Scope scope(d,data,true,false);if(scope.skip||FAILED(d->DrawPrimitiveUP(data.type,data.count,dark,sizeof(Vertex))))return false;}d->EndScene();removeFades=true;if(!read(rt.Get(),control)||control==before)return false;++cases;
 }
 // A gray/color panel under ordinary alpha blending must be kept.
 Vertex coloured[4];for(unsigned i=0;i<4;++i){coloured[i]=vertex(quad[i].x,quad[i].y);coloured[i].colour=0xff808080;}data.vertices=coloured;d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);{Scope scope(d,data,true,false);if(scope.skip||scope.changed)return false;}++cases;
 d->SetVertexShader(vs.Get());d->SetPixelShader(ps.Get());data.vertices=quad;d->SetPixelShaderConstantF(0,black,1);
 d->SetVertexShader(gammaVS.Get());d->SetPixelShader(gammaPS.Get());Vertex depthQuad[4];for(unsigned i=0;i<4;++i){depthQuad[i]=vertex(quad[i].x,quad[i].y);depthQuad[i].colour=0xff000000;}data.vertices=depthQuad;d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);{Scope scope(d,data,true,false);if(!scope.skip||scope.changed)return false;}++cases;
 d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS);{Scope scope(d,data,true,false);if(scope.skip||scope.changed)return false;}++cases;d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetVertexShader(vs.Get());d->SetPixelShader(ps.Get());data.vertices=quad;d->SetPixelShaderConstantF(0,black,1);
 printf("Target gamma UI GPU: LOG/EXP rejected by A1x reproduced; UiSimpleVS depth scale + UiSolidPS original cache programs,3 black multiplicative fade alpha controls removed; ordinary gray artwork and depth-tested draws retained; read-only ALWAYS UI depth accepted PASS\n");
 for(float alpha: {.15f,.55f,1.f}){lastBat=0;black[3]=alpha;d->SetPixelShaderConstantF(0,black,1);begin_eye(-1,910);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>before,after,control;if(!read(rt.Get(),before)||FAILED(d->BeginScene()))return false;{Scope scope(d,data,true,false);if(!scope.skip||scope.changed)return false;}d->EndScene();if(!read(rt.Get(),after)||after!=before)return false;
  removeFades=false;d->BeginScene();{Scope scope(d,data,true,false);if(scope.skip)return false;if(FAILED(d->DrawPrimitiveUP(data.type,data.count,quad,sizeof(Vertex))))return false;}d->EndScene();removeFades=true;if(!read(rt.Get(),control)||control==before)return false;++cases;
 }
 // Actual observed Asylum stage has narrow top/bottom margins, not clip+/-1.
 Vertex letterboxed[4];for(unsigned i=0;i<4;++i)letterboxed[i]=vertex(quad[i].x,quad[i].y*.9190523f);data.vertices=letterboxed;lastBat=0;black[3]=.55f;d->SetPixelShaderConstantF(0,black,1);begin_eye(-1,913);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>marginBefore,marginAfter,marginOld;if(!read(rt.Get(),marginBefore)||FAILED(d->BeginScene()))return false;{Scope scope(d,data,true,false);if(!scope.skip||scope.changed)return false;}d->EndScene();if(!read(rt.Get(),marginAfter)||marginAfter!=marginBefore)return false;removeFades=false;d->BeginScene();{Scope scope(d,data,true,false);if(scope.skip||FAILED(d->DrawPrimitiveUP(data.type,data.count,letterboxed,sizeof(Vertex))))return false;}d->EndScene();removeFades=true;if(!read(rt.Get(),marginOld)||marginOld==marginBefore)return false;++cases;
 black[3]=1;d->SetPixelShaderConstantF(0,black,1);Vertex smallQuad[4];for(unsigned i=0;i<4;++i)smallQuad[i]=vertex(quad[i].x*.4f,quad[i].y*.4f);data.vertices=smallQuad;{Scope scope(d,data,true,false);if(scope.skip||scope.changed)return false;}++cases;
 data.vertices=quad;const float colour[]={.8f,.2f,.1f,1};d->SetPixelShaderConstantF(0,colour,1);{Scope scope(d,data,true,false);if(scope.skip||scope.changed)return false;}++cases;d->SetPixelShaderConstantF(0,black,1);
 std::vector<Vertex>mesh;auto triangle=[&](float ax,float ay,float bx,float by,float cx,float cy){mesh.push_back(vertex(ax,ay));mesh.push_back(vertex(bx,by));mesh.push_back(vertex(cx,cy));};const float o=1,h=.35f;
 for(unsigned side=0;side<4;++side){const float ring[8][2]={{-o,-o},{o,-o},{o,o},{-o,o},{-h,-h},{h,-h},{h,h},{-h,h}};unsigned a=side,b=(side+1)%4;triangle(ring[a][0],ring[a][1],ring[b][0],ring[b][1],ring[a+4][0],ring[a+4][1]);triangle(ring[b][0],ring[b][1],ring[b+4][0],ring[b+4][1],ring[a+4][0],ring[a+4][1]);}
 data.type=D3DPT_TRIANGLELIST;data.count=8;data.vertices=mesh.data();begin_eye(-1,911);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>before,after,mask;if(!read(rt.Get(),before)||FAILED(d->BeginScene()))return false;
 {Scope scope(d,data,true,false);if(scope.skip||!scope.changed)return false;if(FAILED(d->DrawPrimitiveUP(data.type,data.count,mesh.data(),sizeof(Vertex))))return false;}d->EndScene();if(!read(rt.Get(),after)||after!=before||!sheet()||!read(sheet(),mask))return false;
 if(mask[(104*192+96)*4+3]!=0||mask[(8*192+8)*4+3]!=255)return false;if(completed&&!completed(sheet()))return false;float restored[16]{};if(FAILED(d->GetVertexShaderConstantF(5,restored,4))||memcmp(matrix,restored,64))return false;ComPtr<IDirect3DSurface9>actual;d->GetRenderTarget(0,&actual);D3DVIEWPORT9 vp{};d->GetViewport(&vp);if(actual.Get()!=rt.Get()||memcmp(&vp,&viewport,sizeof(vp)))return false;++cases;
 begin_eye(-1,912);for(auto&x:mesh){x.x*=.4f;x.y*=.4f;}data.vertices=mesh.data();{Scope scope(d,data,true,false);if(scope.skip||!scope.changed)return false;}++cases;
 // Contracted bat geometry stays on the original fixed stage, while the same
 // small black artwork outside a bat episode retains normal menu placement.
 lastBat=0;haveBatStage=false;begin_eye(-1,914);{Scope scope(d,data,true,false);if(scope.skip||scope.changed)return false;}++cases;
 // The production buffered path receives bytes from WRITEONLY dynamic uploads.
 ComPtr<IDirect3DVertexBuffer9>vb;ComPtr<IDirect3DIndexBuffer9>ib;const WORD indices[]={0,1,2,2,1,3};if(FAILED(d->CreateVertexBuffer(sizeof(quad),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr))||FAILED(d->CreateIndexBuffer(sizeof(indices),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr)))return false;void*mapped{};if(FAILED(vb->Lock(0,0,&mapped,D3DLOCK_DISCARD)))return false;memcpy(mapped,quad,sizeof(quad));vb->Unlock();if(FAILED(ib->Lock(0,0,&mapped,D3DLOCK_DISCARD)))return false;memcpy(mapped,indices,sizeof(indices));ib->Unlock();d->SetStreamSource(0,vb.Get(),0,sizeof(Vertex));d->SetIndices(ib.Get());data={};data.type=D3DPT_TRIANGLELIST;data.count=2;data.indexed=true;data.num=4;lastBat=0;{Scope scope(d,data,true,false);if(!scope.skip||scope.changed)return false;}++cases;
 // Actual retail UI shader/declaration and managed non-DYNAMIC CPU storage.
 std::vector<DWORD>liveV,liveP;if(!load(L"ui-live-solid-vs",liveV)||!load(L"ui-live-solid-ps",liveP))return false;ComPtr<IDirect3DVertexShader9>liveVS;ComPtr<IDirect3DPixelShader9>livePS;if(FAILED(d->CreateVertexShader(liveV.data(),&liveVS))||FAILED(d->CreatePixelShader(liveP.data(),&livePS)))return false;
 const D3DVERTEXELEMENT9 shortElements[]={{0,0,D3DDECLTYPE_SHORT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};ComPtr<IDirect3DVertexDeclaration9>shortDecl;if(FAILED(d->CreateVertexDeclaration(shortElements,&shortDecl)))return false;d->SetVertexDeclaration(shortDecl.Get());d->SetVertexShader(liveVS.Get());d->SetPixelShader(livePS.Get());const float stage[]={1.f/32000,0,0,0,0,.9190523f/32000,0,0,0,0,1,0,0,0,0,1};d->SetVertexShaderConstantF(5,stage,4);const float scale[]={1,0,0,0};d->SetVertexShaderConstantF(13,scale,1);
 struct ShortVertex{short x,y;};const ShortVertex shortQuad[]={{-32000,32000},{32000,32000},{-32000,-32000},{32000,-32000}};
 ComPtr<IDirect3DVertexBuffer9>staticVB;ComPtr<IDirect3DIndexBuffer9>staticIB;constexpr UINT resourceBytes=512*1024,vertexAt=128,indexAt=64;
 if(FAILED(d->CreateVertexBuffer(resourceBytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&staticVB,nullptr))||FAILED(d->CreateIndexBuffer(resourceBytes,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&staticIB,nullptr)))return false;
 const auto savedCopies=bufferCopies,savedManaged=managedReads;const WORD staticIndices[]={4,5,6,6,5,7};void*cpu{};
 if(FAILED(staticVB->Lock(vertexAt+12*sizeof(ShortVertex),sizeof(shortQuad),&cpu,0)))return false;memcpy(cpu,shortQuad,sizeof(shortQuad));staticVB->Unlock();if(FAILED(staticIB->Lock(indexAt,sizeof(staticIndices),&cpu,0)))return false;memcpy(cpu,staticIndices,sizeof(staticIndices));staticIB->Unlock();
 d->SetStreamSource(0,staticVB.Get(),vertexAt,4);d->SetIndices(staticIB.Get());Draw buffered{};buffered.type=D3DPT_TRIANGLELIST;buffered.count=2;buffered.indexed=true;buffered.base=8;buffered.min=4;buffered.num=4;buffered.indexStart=indexAt/2;
 for(float alpha:{1.f,176.f/255,158.f/255}){black[3]=alpha;d->SetPixelShaderConstantF(0,black,1);lastBat=0;haveBatStage=false;begin_eye(-1,930);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>before,after,control;if(!read(rt.Get(),before)||FAILED(d->BeginScene()))return false;{Scope scope(d,buffered,true,false);if(!scope.skip||scope.changed)return false;}d->EndScene();if(!read(rt.Get(),after)||after!=before)return false;removeFades=false;d->BeginScene();{Scope scope(d,buffered,true,false);if(scope.skip||FAILED(d->DrawIndexedPrimitive(buffered.type,buffered.base,buffered.min,buffered.num,buffered.indexStart,buffered.count)))return false;}d->EndScene();removeFades=true;if(!read(rt.Get(),control)||control==before)return false;++cases;}
 // The old DYNAMIC-only classifier had neither CPU source for these resources.
 D3DVERTEXBUFFER_DESC staticDesc{};staticVB->GetDesc(&staticDesc);if(staticDesc.Usage&D3DUSAGE_DYNAMIC||staticDesc.Size<=256*1024)return false;
 const bool nativeCPU=managed9::native_device(d)!=nullptr;if(nativeCPU&&managedReads<=savedManaged)return false;if(!nativeCPU&&bufferCopies<=savedCopies)return false;
 // Direct managed storage survives shadow eviction, works before upload, and
 // rejects locked/wrong-owner/out-of-range requests without touching the GPU.
 if(nativeCPU){ShortVertex actual[4]{};if(!managed9::copy_buffer(d,staticVB.Get(),vertexAt+12*4,sizeof(actual),actual)||memcmp(actual,shortQuad,sizeof(actual)))return false;if(managed9::copy_buffer(nullptr,staticVB.Get(),0,4,actual)||managed9::copy_buffer(d,staticVB.Get(),resourceBytes-1,4,actual))return false;if(FAILED(staticVB->Lock(vertexAt+12*4,sizeof(shortQuad),&cpu,D3DLOCK_READONLY)))return false;const bool rejected=!managed9::copy_buffer(d,staticVB.Get(),vertexAt+12*4,sizeof(actual),actual);staticVB->Unlock();if(!rejected)return false;++cases;}
 // Original retail SHORT2 bat mesh through the same non-DYNAMIC resources.
 const ShortVertex shortRing[]={{-32000,-32000},{32000,-32000},{32000,32000},{-32000,32000},{-11200,-11200},{11200,-11200},{11200,11200},{-11200,11200}};WORD batIndices[24]{};for(unsigned side=0;side<4;++side){const unsigned a=side,b=(side+1)%4;const WORD tri[]={WORD(a+4),WORD(b+4),WORD(a+8),WORD(b+4),WORD(b+8),WORD(a+8)};memcpy(batIndices+side*6,tri,sizeof(tri));}
 if(FAILED(staticVB->Lock(vertexAt+12*4,sizeof(shortRing),&cpu,0)))return false;memcpy(cpu,shortRing,sizeof(shortRing));staticVB->Unlock();if(FAILED(staticIB->Lock(indexAt,sizeof(batIndices),&cpu,0)))return false;memcpy(cpu,batIndices,sizeof(batIndices));staticIB->Unlock();buffered.count=8;buffered.num=8;black[3]=1;d->SetPixelShaderConstantF(0,black,1);begin_eye(-1,932);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>batBefore,batAfter,batMask;if(!read(rt.Get(),batBefore)||FAILED(d->BeginScene()))return false;
 {Scope scope(d,buffered,true,false);if(scope.skip||!scope.changed||FAILED(d->DrawIndexedPrimitive(buffered.type,buffered.base,buffered.min,buffered.num,buffered.indexStart,buffered.count)))return false;}d->EndScene();if(!read(rt.Get(),batAfter)||batAfter!=batBefore||!sheet()||!read(sheet(),batMask))return false;if(batMask[(104*192+96)*4+3]!=0||batMask[(8*192+8)*4+3]!=255)return false;++cases;
 printf("Retail bat GPU: recorded shader/SHORT2,512KiB static managed VB+IB with base/min/offset; original mesh redirected once, window stays clean, XR mask corners opaque/centre transparent PASS\n");
 printf("Retail UI geometry GPU: actual ConstantColor/SHORT2 shader pair;512KiB non-DYNAMIC managed VB+IB, byte offsets/base/min indices, observed fade alphas255/176/158; old DYNAMIC-only gate fails, existing CPU bytes/sparse uploads now classify, no GPU readback PASS\n");
 // A1z LIVE family:10 triangles/12 vertices includes an antialias border;
 // straight alpha and COLOR1 coverage programs are actual Asylum cache shaders.
 std::vector<DWORD>straightCode,coverageCode,complexVSCode;if(!load(L"ui-straight-6fe3",straightCode)||!load(L"ui-straight-74ad",coverageCode)||!load(L"ui-live-complex-vs",complexVSCode))return false;
 ComPtr<IDirect3DPixelShader9>straightPS,coveragePS;ComPtr<IDirect3DVertexShader9>complexVS;if(FAILED(d->CreatePixelShader(straightCode.data(),&straightPS))||FAILED(d->CreatePixelShader(coverageCode.data(),&coveragePS))||FAILED(d->CreateVertexShader(complexVSCode.data(),&complexVS)))return false;
 d->SetVertexDeclaration(decl.Get());d->SetVertexShader(complexVS.Get());d->SetVertexShaderConstantF(5,matrix,4);d->SetVertexShaderConstantF(13,scale,1);const float plainConstants[3][4]={{1,1,1,1},{0,0,0,0},{1,0,0,0}};d->SetPixelShaderConstantF(0,&plainConstants[0][0],3);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
 auto aaRectangle=[&](float hw,float hh,float alpha,bool second){std::vector<Vertex>v;const float xy[4][2]={{-hw,-hh},{hw,-hh},{hw,hh},{-hw,hh}};for(const auto&p:xy){auto x=vertex(p[0],p[1]);x.colour=second?0xff000000:D3DCOLOR_ARGB(unsigned(alpha*255),0,0,0);x.colour1=second?D3DCOLOR_ARGB(unsigned(alpha*255),0,0,0):0xff000000;v.push_back(x);}for(unsigned side=0;side<4;++side)for(unsigned k=0;k<2;++k){const auto&p=xy[(side+k)%4];auto x=vertex(p[0]+(p[0]<0?-.004f:.004f),p[1]+(p[1]<0?-.004f:.004f));x.colour=second?0xff000000:0;x.colour1=second?0:0xff000000;v.push_back(x);}return v;};
 const WORD aaIndices[]={0,1,2,0,2,3,0,4,1,1,4,5,1,6,2,2,6,7,2,8,3,3,8,9,3,10,0,0,10,11};
 Draw aa{};aa.type=D3DPT_TRIANGLELIST;aa.count=10;aa.indexed=true;aa.num=12;aa.indices=aaIndices;aa.stride=sizeof(Vertex);aa.format=D3DFMT_INDEX16;
 for(unsigned shaderCase=0;shaderCase<3;++shaderCase)for(float alpha:{1.f,176.f/255,158.f/255}){
  const bool second=shaderCase==1;auto verts=aaRectangle(2.002f,.915f,alpha,second);aa.vertices=verts.data();d->SetVertexShader(shaderCase==2?gammaVS.Get():complexVS.Get());d->SetPixelShader(shaderCase==2?gammaPS.Get():second?coveragePS.Get():straightPS.Get());d->SetVertexShaderConstantF(9,scale,1);d->SetRenderState(D3DRS_SRCBLEND,shaderCase==2?D3DBLEND_DESTCOLOR:D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,shaderCase==2?D3DBLEND_ZERO:D3DBLEND_INVSRCALPHA);lastBat=0;haveBatStage=false;begin_eye(-1,940);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>before,after,control;if(!read(rt.Get(),before)||FAILED(d->BeginScene()))return false;
  {Scope scope(d,aa,true,false);if(!scope.skip||scope.changed)return false;}d->EndScene();if(std::fabs(fade_alpha(940)-alpha)>.005f)return false;if(!read(rt.Get(),after)||after!=before)return false;removeFades=false;d->BeginScene();{Scope scope(d,aa,true,false);if(scope.skip||FAILED(d->DrawIndexedPrimitiveUP(aa.type,aa.min,aa.num,aa.count,aa.indices,aa.format,aa.vertices,aa.stride)))return false;}d->EndScene();removeFades=true;if(!read(rt.Get(),control)||control==before)return false;++cases;
 }
 d->SetVertexShader(complexVS.Get());d->SetPixelShader(straightPS.Get());d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
 auto colourArt=aaRectangle(2.002f,.915f,1,false);for(auto&v:colourArt)if(v.colour>>24)v.colour=0xffcc3311;aa.vertices=colourArt.data();{Scope scope(d,aa,true,false);if(scope.skip||scope.changed)return false;}++cases;
 auto smallArt=aaRectangle(.4f,.4f,1,false);aa.vertices=smallArt.data();{Scope scope(d,aa,true,false);if(scope.skip||scope.changed)return false;}++cases;
 auto fullAA=aaRectangle(2.002f,.915f,1,false);aa.vertices=fullAA.data();d->SetRenderState(D3DRS_STENCILENABLE,TRUE);{Scope scope(d,aa,true,false);if(scope.skip||scope.changed)return false;}d->SetRenderState(D3DRS_STENCILENABLE,FALSE);++cases;d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);{Scope scope(d,aa,true,false);if(scope.skip||scope.changed)return false;}d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);++cases;
 d->SetPixelShader(coveragePS.Get());const D3DVERTEXELEMENT9 noCoverageElements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};ComPtr<IDirect3DVertexDeclaration9>noCoverage;if(FAILED(d->CreateVertexDeclaration(noCoverageElements,&noCoverage)))return false;d->SetVertexDeclaration(noCoverage.Get());{Scope scope(d,aa,true,false);if(scope.skip||scope.changed)return false;}++cases;
 printf("Authored fade GPU:10triangle/12vertex AA rectangle; source alpha0 fringes/COLOR1 coverage/white multiplicative fringes at255/176/158 opacities, target straight/coverage/gamma programs; old2tri/edge-opacity rules reject; small/coloured/stencil/additive/missing-coverage controls retained PASS\n");
 // Bat sweep layouts come from A1z's actual per-draw bounds. Original full-
 // stage-only matcher cannot begin an episode on the first arriving edge.
 d->SetVertexDeclaration(shortDecl.Get());d->SetVertexShader(liveVS.Get());d->SetPixelShader(livePS.Get());const float authoredStage[]={1.f/16000,0,0,0,0,1.f/16000,0,0,0,0,1,0,0,0,0,1};d->SetVertexShaderConstantF(5,authoredStage,4);d->SetVertexShaderConstantF(13,scale,1);black[3]=1;d->SetPixelShaderConstantF(0,black,1);d->SetStreamSource(0,staticVB.Get(),vertexAt,4);d->SetIndices(staticIB.Get());
 for(unsigned phase=0;phase<3;++phase){const float l=-1.42462f,r=phase==1?1.42366f:-.74408f,b=phase==1?-.74625f:-.59072f,t=phase==1?.74724f:.37663f;const float cx=(l+r)*.5f,cy=(b+t)*.5f,ihw=(r-l)*.175f,ihh=(t-b)*.175f;auto sh=[](float x,float y){return ShortVertex{short(std::lround(x*16000)),short(std::lround(y*16000))};};const ShortVertex moving[]={sh(l,b),sh(r,b),sh(r,t),sh(l,t),sh(cx-ihw,cy-ihh),sh(cx+ihw,cy-ihh),sh(cx+ihw,cy+ihh),sh(cx-ihw,cy+ihh)};std::vector<WORD>dense;for(unsigned n=0;n<36;++n)dense.insert(dense.end(),batIndices,batIndices+24);
  if(FAILED(staticVB->Lock(vertexAt+12*4,sizeof(moving),&cpu,0)))return false;memcpy(cpu,moving,sizeof(moving));staticVB->Unlock();if(FAILED(staticIB->Lock(indexAt,UINT(dense.size()*2),&cpu,0)))return false;memcpy(cpu,dense.data(),dense.size()*2);staticIB->Unlock();buffered.count=288;buffered.num=8;lastBat=0;haveBatStage=false;begin_eye(-1,950+phase);black[3]=phase==2?.332f:1;d->SetPixelShaderConstantF(0,black,1);
  if(phase==2){ // Exactly the first edge layout at menu-shadow opacity must stay normal.
   {Scope scope(d,buffered,true,false);if(scope.skip||scope.changed)return false;}++cases;continue;
  }
  d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>before,after,mask;if(!read(rt.Get(),before)||FAILED(d->BeginScene()))return false;{Scope scope(d,buffered,true,false);if(scope.skip||!scope.changed||FAILED(d->DrawIndexedPrimitive(buffered.type,buffered.base,buffered.min,buffered.num,buffered.indexStart,buffered.count)))return false;}d->EndScene();if(!read(rt.Get(),after)||after!=before||!sheet()||!read(sheet(),mask))return false;if(phase==1&&(mask[(104*192+96)*4+3]!=0||mask[(8*192+8)*4+3]!=255))return false;++cases;
 }
 printf("Authored bat GPU: recorded first-edge/full16:9-stage bounds,288tri static SHORT2 geometry; old+/-0.9 fullstage condition rejects BOTH, new capture begins at the edge and fills XR corners; same-edge nonopaque menu shadow retained PASS\n");

 // Another shipped family writes clip position literally: no Transform CTAB.
 // A black rectangle can therefore fade without any vertex constants at all.
 std::vector<DWORD>clipCode;ComPtr<IDirect3DVertexShader9>clipVS;if(!load(L"ui-clip-vs",clipCode)||FAILED(d->CreateVertexShader(clipCode.data(),&clipVS)))return false;
 data={};data.type=D3DPT_TRIANGLESTRIP;data.count=2;data.stride=sizeof(Vertex);
 d->SetVertexDeclaration(decl.Get());d->SetVertexShader(clipVS.Get());d->SetPixelShader(ps.Get());d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);data.vertices=quad;haveBatStage=false;lastBat=0;
 for(float alpha:{.15f,.55f,1.f}){begin_eye(-1,961);black[3]=alpha;d->SetPixelShaderConstantF(0,black,1);d->Clear(0,nullptr,D3DCLEAR_TARGET,0xfff04020,1,0);std::vector<BYTE>before,after,control;if(!read(rt.Get(),before)||FAILED(d->BeginScene()))return false;{Scope scope(d,data,true,false);if(!scope.skip||scope.changed)return false;}d->EndScene();if(std::fabs(fade_alpha(961)-alpha)>.0001f||!read(rt.Get(),after)||after!=before)return false;removeFades=false;d->BeginScene();d->DrawPrimitiveUP(data.type,data.count,quad,sizeof(Vertex));d->EndScene();removeFades=true;if(!read(rt.Get(),control)||control==before)return false;++cases;}
 data.vertices=smallQuad;{Scope scope(d,data,true,false);if(scope.skip||scope.changed)return false;}++cases;data.vertices=quad;d->SetPixelShaderConstantF(0,colour,1);{Scope scope(d,data,true,false);if(scope.skip||scope.changed)return false;}++cases;
 puts("Literal clip-position fade GPU: actual target MOV-position shader with no Transform;3 black opacity controls omitted and envelope retained, small/coloured panels retained PASS");
 printf("Transition GPU original Asylum shaders: %u cases; fading rectangles omitted vs visible old controls; small panels/coloured title art unchanged; original complex geometry preserved at192x208 with transparent centre/full-field margins; matrix/RT/viewport restore; WRITEONLY dynamic VB+IB CPU upload classification PASS\n",cases);return true;
}

}
