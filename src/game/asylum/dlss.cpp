// Asylum's x86 client. City/Origins handoff method, target-owned depth and
// camera tickets. Shared fences order native overlap; safe/AFW handoff finishes on CPU.
#include "dlss.h"
#include "scene.h"
#include "core/gfx/blit.h"
#include "core/util/log.h"
#include "../../../helper64/dlss_shared.h"
#include "../../../helper64/gpu_finish.h"
#include <d3dcompiler.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <imgui.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vector>
#include <psapi.h>
#include <cmath>
using Microsoft::WRL::ComPtr;
namespace bvr::aa::dlss {
namespace {
std::atomic<int> requested{3};std::atomic<bool> running{},fault{};
std::atomic<unsigned> evaluations{},fallbacks{},noDepth{},timeouts{};
HANDLE mapping{},request{},done{},job{},helperProcess{};
bcdlss::Shared* shared{};unsigned serial{};bool initTried{},leftAccepted{},rollback{};
bool resetPending=true;uint32_t leftGeneration{};
std::atomic<unsigned> resetEpoch{1};unsigned seenEpoch{};
wchar_t fixtureHelper[MAX_PATH]{},settings[MAX_PATH]{};
ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11ComputeShader> shader;ComPtr<ID3D11Buffer> constants;
bcdlss::GpuFinish finish;
std::atomic<bool> overlapEnabled{true};bool overlapTried{};
ComPtr<ID3D11Fence> inputFence,outputFence;ComPtr<ID3D11DeviceContext4> fenceContext;
HANDLE outputEvent{};
struct Pending {uint32_t serial{},epoch{};int mode{};native::Camera camera{};bool valid{};};Pending pending[2];
unsigned queuedLeft{},pairWaits{};double pairWaitTotal{};
bool shared_fences(){
 if(inputFence&&outputFence&&fenceContext)return true;if(overlapTried)return false;overlapTried=true;
 ComPtr<ID3D11Device5> d5;HANDLE local[2]{},remote[2]{};bool ok=SUCCEEDED(device.As(&d5))&&SUCCEEDED(context.As(&fenceContext));
 if(ok)ok=SUCCEEDED(d5->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&inputFence)))&&SUCCEEDED(d5->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&outputFence)));
 if(ok)ok=SUCCEEDED(inputFence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&local[0]))&&SUCCEEDED(outputFence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&local[1]));
 if(ok)for(int i=0;i<2;++i)if(!DuplicateHandle(GetCurrentProcess(),local[i],helperProcess,&remote[i],0,FALSE,DUPLICATE_SAME_ACCESS))ok=false;
 for(auto h:local)if(h)CloseHandle(h);
 if(ok){outputEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);ok=outputEvent!=nullptr;}
 if(ok){shared->fenceInHandle=uint64_t(uintptr_t(remote[0]));shared->fenceOutHandle=uint64_t(uintptr_t(remote[1]));MemoryBarrier();BVR_LOG("[aa DLSS] native overlap available: shared input/output fences, LEFT deferred, CPU wait once at RIGHT");}
 else{for(auto h:remote)if(h){HANDLE copy{};if(DuplicateHandle(helperProcess,h,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_CLOSE_SOURCE)&&copy)CloseHandle(copy);}inputFence.Reset();outputFence.Reset();fenceContext.Reset();BVR_LOG("[aa DLSS] shared fences unavailable; synchronous safe handoff retained");}
 return ok;
}
struct Set {
 ComPtr<ID3D11Texture2D> texture[4];ComPtr<IDXGIKeyedMutex> mutex[4];ComPtr<ID3D11UnorderedAccessView> depth,motion;
 math::M previousProjection{},previousView{};bool previousValid{};unsigned rw{},rh{},ow{},oh{};int evaluatedMode{};bool centred{};
};Set sets[2];
float halton(unsigned i,unsigned base){float f=1,r=0;while(i){f/=float(base);r+=f*(i%base);i/=base;}return r;}
unsigned jitter_period(unsigned output,unsigned input){const double ratio=double(output)/input;return std::clamp(unsigned(std::ceil(8*ratio*ratio)),8u,128u);}
// PIMAXSTART (Pimax Crystal player log 2026-10-17): start() used to wait here, on the render thread, at the FIRST 3D
// frame, until the helper reported NGX ready - 6.5 s on that PC. The Pimax runtime took the headset away from the
// "hung" game (FOCUSED -> VISIBLE) and the player saw nothing more. City/Origins never wait: the helper now starts in
// the background as soon as the game's device exists (prewarm), and each frame only asks whether it is ready yet.
ULONGLONG startedAt{};bool readyLogged{};
bool spawn(ID3D11Device* d){
 device=d;d->GetImmediateContext(&context);
 wchar_t path[MAX_PATH]{},folder[MAX_PATH]{};
 if(fixtureHelper[0])wcscpy_s(path,fixtureHelper);
 else{GetModuleFileNameW(nullptr,path,MAX_PATH);auto* slash=wcsrchr(path,L'\\');if(!slash)return false;wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"AsylumVR\\DLSS\\aa_dlss64.exe");}
 wcscpy_s(folder,path);auto* slash=wcsrchr(folder,L'\\');if(!slash||GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return false;*slash=0;
 wchar_t session[64]{},mapName[128]{},reqName[128]{},doneName[128]{},command[2*MAX_PATH]{};
 swprintf_s(session,L"%lu_%llu",GetCurrentProcessId(),GetTickCount64());
 swprintf_s(mapName,L"%s_%s",bcdlss::kMapName,session);swprintf_s(reqName,L"%s_%s",bcdlss::kReqName,session);swprintf_s(doneName,L"%s_%s",bcdlss::kDoneName,session);
 mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(bcdlss::Shared),mapName);
 if(!mapping||GetLastError()==ERROR_ALREADY_EXISTS){BVR_LOG("[aa DLSS] shared block already in use or unavailable");return false;}
 shared=static_cast<bcdlss::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(bcdlss::Shared)));if(!shared)return false;
 *shared={};shared->version=bcdlss::kVersion;shared->gamePid=GetCurrentProcessId();shared->syncMode=2;shared->mvScaleX=shared->mvScaleY=1;
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC ad{};
 if(FAILED(d->QueryInterface(IID_PPV_ARGS(&dx)))||FAILED(dx->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&ad)))return false;
 shared->luidLow=ad.AdapterLuid.LowPart;shared->luidHigh=ad.AdapterLuid.HighPart;
 request=CreateEventW(nullptr,FALSE,FALSE,reqName);done=CreateEventW(nullptr,FALSE,FALSE,doneName);job=CreateJobObjectW(nullptr,nullptr);
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!request||!done||!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info)))return false;
 STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
 swprintf_s(command,L"\"%s\" --session %s",path,session);
 if(!CreateProcessW(path,command,nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,folder,&si,&pi))return false;
 if(!AssignProcessToJobObject(job,pi.hProcess)){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return false;}
 helperProcess=pi.hProcess;ResumeThread(pi.hThread);CloseHandle(pi.hThread);startedAt=GetTickCount64();
 BVR_LOG("[aa DLSS] helper pid %lu started in the background; the game never waits for it",pi.dwProcessId);
 return true;
}
// 1 ready, 0 still starting (stock picture meanwhile), -1 failed (stock picture until restart). Never blocks.
int start(ID3D11Device* d){
 if(!shared){if(initTried)return -1;initTried=true;if(!spawn(d))return -1;}
 if(fault)return -1;
 MemoryBarrier();
 if(shared->helperReady){
  if(!readyLogged){readyLogged=true;BVR_LOG("[aa DLSS] helper ready after %llu ms, NGX %d; input ownership finishes on CPU",GetTickCount64()-startedAt,shared->ngxState);}
  return shared->ngxState==1?1:-1;
 }
 if(!helperProcess||WaitForSingleObject(helperProcess,0)!=WAIT_TIMEOUT)return -1;
 return GetTickCount64()-startedAt<60000?0:-1; // a first NGX start can be slow (driver model download); a minute at most
}
std::atomic<unsigned> helperWaits{};
bool compute_shader(){
 if(shader&&constants)return true;
 const char source[]=
 "Texture2D<float> depthT:register(t0);RWTexture2D<float> outD:register(u0);RWTexture2D<float2> outM:register(u1);"
 "cbuffer C:register(b0){float4 r0,r1,r2,r3;float4 size;float4 jit;float4 crop;float4 ds;}"
 "[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=uint(size.x)||id.y>=uint(size.y))return;"
 "float2 uv=(float2(id.xy)+.5)*size.zw;float2 duv=uv*crop.xy+crop.zw;"
 "int2 xy=clamp(int2(duv*ds.xy),int2(0,0),int2(ds.xy)-1);float d=depthT.Load(int3(xy,0));outD[id.xy]=d;"
 "if(ds.w>.5?d<=ds.z+1e-7:d>=ds.z-1e-7)d=ds.z;" // clear depth is infinity for motion; NGX retains raw depth
 "float2 n=float2(uv.x*2-1,1-uv.y*2);float4 p=n.x*r0+n.y*r1+d*r2+r3;"
 "float2 mv=abs(p.w)>1e-6?p.xy/p.w-(n-jit.xy):float2(0,0);"
 "mv=clamp(mv,float2(-2,-2),float2(2,2));outM[id.xy]=float2(mv.x*size.x*.5,-mv.y*size.y*.5);} ";
 ComPtr<ID3DBlob> bytes,error;if(FAILED(D3DCompile(source,sizeof(source)-1,"aa_camera_motion",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytes,&error)))return false;
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=128;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_DEFAULT;
 return SUCCEEDED(device->CreateComputeShader(bytes->GetBufferPointer(),bytes->GetBufferSize(),nullptr,&shader))&&SUCCEEDED(device->CreateBuffer(&bd,nullptr,&constants));
}
bool texture(Set& s,int i,unsigned w,unsigned h,DXGI_FORMAT format,bool uav,uint64_t& handle){
 D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.Format=format;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
 d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|(uav?D3D11_BIND_UNORDERED_ACCESS:0);d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
 const auto hr=device->CreateTexture2D(&d,nullptr,&s.texture[i]);if(FAILED(hr)){BVR_LOG("[aa DLSS] keyed texture %u %ux%u format %u failed %08lX",i,w,h,format,hr);return false;}
 ComPtr<IDXGIResource> resource;HANDLE hnd{};
 if(FAILED(s.texture[i].As(&s.mutex[i]))||FAILED(s.texture[i].As(&resource))||FAILED(resource->GetSharedHandle(&hnd))||!hnd)return false;
 handle=uint64_t(uintptr_t(hnd));
 if(i==bcdlss::kDepth)return SUCCEEDED(device->CreateUnorderedAccessView(s.texture[i].Get(),nullptr,&s.depth));
 if(i==bcdlss::kMotion)return SUCCEEDED(device->CreateUnorderedAccessView(s.texture[i].Get(),nullptr,&s.motion));return true;
}
bool ensure_sets(unsigned rw,unsigned rh,unsigned ow,unsigned oh){
 if(sets[0].texture[0]&&sets[0].rw==rw&&sets[0].rh==rh&&sets[0].ow==ow&&sets[0].oh==oh&&sets[1].texture[0])return true;
 Set candidate[2];uint64_t handles[2][4]{};
 for(int e=0;e<2;++e){auto& s=candidate[e];s.rw=rw;s.rh=rh;s.ow=ow;s.oh=oh;
  if(!texture(s,0,rw,rh,DXGI_FORMAT_R8G8B8A8_UNORM,false,handles[e][0])||!texture(s,1,rw,rh,DXGI_FORMAT_R32_FLOAT,true,handles[e][1])||
     !texture(s,2,rw,rh,DXGI_FORMAT_R16G16_FLOAT,true,handles[e][2])||!texture(s,3,ow,oh,DXGI_FORMAT_R8G8B8A8_UNORM,true,handles[e][3]))return false;
 }
 sets[0]=std::move(candidate[0]);sets[1]=std::move(candidate[1]);memcpy(shared->handle,handles,sizeof(handles));shared->renderW=rw;shared->renderH=rh;shared->outW=ow;shared->outH=oh;
 MemoryBarrier();++shared->gen;BVR_LOG("[aa DLSS] actual render %ux%u -> DLSS %ux%u; independent eye textures/history",rw,rh,ow,oh);return true;
}
bool motion(Set& s,const native::Camera& c,ID3D11ShaderResourceView* depth,bool reset){
 if(!compute_shader())return false;
 math::M transform{};
 auto unjittered=c.projection;unjittered.v[2][0]-=2*c.jitterX/s.rw;unjittered.v[2][1]+=2*c.jitterY/s.rh;
 const bool current=reset||!s.previousValid;
 if(!math::relative_projection(c.view,c.projection,current?c.view:s.previousView,current?unjittered:s.previousProjection,transform))return false;
 ComPtr<ID3D11Resource> dep;ComPtr<ID3D11Texture2D> depthTexture;depth->GetResource(&dep);if(FAILED(dep.As(&depthTexture)))return false;
 D3D11_TEXTURE2D_DESC dd{};depthTexture->GetDesc(&dd);
 float cb[32]{};memcpy(cb,transform.v,64);cb[16]=float(s.rw);cb[17]=float(s.rh);cb[18]=1.f/s.rw;cb[19]=1.f/s.rh;
 cb[20]=2*c.jitterX/s.rw;cb[21]=-2*c.jitterY/s.rh;memcpy(cb+24,c.crop,16);
 // The viewport occupies the upper-left of a possibly padded depth target.
 cb[24]*=float(c.width)/dd.Width;cb[25]*=float(c.height)/dd.Height;cb[26]*=float(c.width)/dd.Width;cb[27]*=float(c.height)/dd.Height;cb[28]=float(dd.Width);cb[29]=float(dd.Height);
 cb[30]=c.projection.v[2][2];cb[31]=c.projection.v[3][2]>0?1.f:0.f;
 ComPtr<ID3D11ComputeShader> oldShader;ComPtr<ID3D11ShaderResourceView> oldSrv;ComPtr<ID3D11Buffer> oldCb;ComPtr<ID3D11UnorderedAccessView> oldUav[2];
 context->CSGetShader(&oldShader,nullptr,nullptr);context->CSGetShaderResources(0,1,&oldSrv);context->CSGetConstantBuffers(0,1,&oldCb);context->CSGetUnorderedAccessViews(0,1,&oldUav[0]);context->CSGetUnorderedAccessViews(1,1,&oldUav[1]);
 context->UpdateSubresource(constants.Get(),0,nullptr,cb,0,0);ID3D11UnorderedAccessView* views[]={s.depth.Get(),s.motion.Get()};ID3D11Buffer* buffers[]={constants.Get()};
 // Clear vectors every time; a missing dispatch cannot expose stale motion.
 const float zero[4]{};context->ClearUnorderedAccessViewFloat(s.motion.Get(),zero);context->CSSetShader(shader.Get(),nullptr,0);context->CSSetShaderResources(0,1,&depth);context->CSSetUnorderedAccessViews(0,2,views,nullptr);context->CSSetConstantBuffers(0,1,buffers);context->Dispatch((s.rw+7)/8,(s.rh+7)/8,1);
 ID3D11ShaderResourceView* nullSrv{};ID3D11UnorderedAccessView* nullViews[2]{};context->CSSetShaderResources(0,1,&nullSrv);context->CSSetUnorderedAccessViews(0,2,nullViews,nullptr);
 auto* os=oldSrv.Get();auto* ob=oldCb.Get();ID3D11UnorderedAccessView* ou[]={oldUav[0].Get(),oldUav[1].Get()};context->CSSetShader(oldShader.Get(),nullptr,0);context->CSSetShaderResources(0,1,&os);context->CSSetConstantBuffers(0,1,&ob);context->CSSetUnorderedAccessViews(0,2,ou,nullptr);
 s.previousProjection=unjittered;return true;
}
void failure(const char* reason){running=false;fault=true;++fallbacks;BVR_LOG("[aa DLSS] stopped: %s; stock scene retained until restart",reason);}
bool wait_output(uint32_t wanted){
 if(outputFence->GetCompletedValue()>=wanted)return true;
 LARGE_INTEGER a,b,f;QueryPerformanceCounter(&a);QueryPerformanceFrequency(&f);
 const auto deadline=GetTickCount64()+2000;
 while(outputFence->GetCompletedValue()<wanted){const auto now=GetTickCount64();
  if(now>=deadline||FAILED(outputFence->SetEventOnCompletion(wanted,outputEvent))||WaitForSingleObject(outputEvent,DWORD(deadline-now))!=WAIT_OBJECT_0){++timeouts;failure("overlap completion fence timeout");return false;}}
 QueryPerformanceCounter(&b);++pairWaits;pairWaitTotal+=1000.*double(b.QuadPart-a.QuadPart)/f.QuadPart;
 return true;
}
bool answer_ok(int e){MemoryBarrier();const auto& q=shared->req[e];return q.answered==pending[e].serial&&!q.status&&q.evaluatedMode==pending[e].mode&&!shared->fenceWaitTimeouts;}
bool copy_output(int e,ID3D11Texture2D* output,bool finishCpu){
 auto& s=sets[e];unsigned held{};for(;held<4;++held)if(s.mutex[held]->AcquireSync(0,100)!=S_OK)break;
 if(held!=4){for(unsigned i=0;i<held;++i)s.mutex[i]->ReleaseSync(0);failure("output ownership timeout");return false;}
 ComPtr<ID3D11ShaderResourceView> src;ComPtr<ID3D11RenderTargetView> rt;D3D11_TEXTURE2D_DESC od{};output->GetDesc(&od);
 D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=od.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:od.Format;rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
 const bool copied=SUCCEEDED(device->CreateShaderResourceView(s.texture[3].Get(),nullptr,&src))&&SUCCEEDED(device->CreateRenderTargetView(output,&rtv,&rt))&&blit::shift_copy(context.Get(),rt.Get(),od.Width,od.Height,src.Get(),0);
 const bool complete=!finishCpu||finish.wait(device.Get(),context.Get(),1000);
 for(int i=0;i<4;++i)s.mutex[i]->ReleaseSync(0);
 if(!finishCpu)context->Flush(); // submit copy/release; no CPU drain. The next input fence follows this copy.
 if(!copied||!complete){failure("output copy/completion");return false;}return true;
}
void accepted(const native::Camera& c,int selected){auto& s=sets[c.eye];s.previousValid=true;s.previousView=c.view;s.evaluatedMode=selected;s.centred=c.centred;++evaluations;}
bool complete_left(ID3D11Texture2D* output,bool keep){
 if(!pending[0].valid)return false;
 bool ok=wait_output(pending[0].serial)&&answer_ok(0);
 if(!ok&&!fault)failure("LEFT overlap evaluation rejected");
 keep=keep&&pending[0].epoch==resetEpoch.load()&&!fault;
 if(ok&&keep){ok=copy_output(0,output,false);if(ok)accepted(pending[0].camera,pending[0].mode);else rollback=true;}
 if(!ok)rollback=true;
 if(!ok||!keep)sets[0].previousValid=false;
 pending[0].valid=false;return ok&&keep;
}
bool evaluate(const native::Camera& c,ID3D11Texture2D* scene,ID3D11ShaderResourceView* depth,ID3D11Texture2D* output,int selected,bool deferLeftAllowed){
 if(c.afw&&pending[0].valid)complete_left(output,false);
 if(c.eye==1&&pending[0].valid&&(pending[0].mode!=selected||pending[0].epoch!=resetEpoch.load()))return false;
 ComPtr<ID3D11Device> dev;scene->GetDevice(&dev);
 const int helper=start(dev.Get());
 if(helper<0){failure("helper unavailable or NGX not ready");return false;}
 if(!helper){++helperWaits;return false;} // PIMAXSTART: still starting - this eye keeps the stock picture, no wait
 D3D11_TEXTURE2D_DESC rd{},od{};scene->GetDesc(&rd);output->GetDesc(&od);
 // The launcher's mode determines the engine's render cost. NGX reconstructs
 // to the requested output, never to input*1.5 above the headset by accident.
 const wchar_t* tx=wcsstr(GetCommandLineW(),L"-AAUpscaleX="),*ty=wcsstr(GetCommandLineW(),L"-AAUpscaleY=");
 const unsigned requestedW=tx?wcstoul(tx+12,nullptr,10):od.Width,requestedH=ty?wcstoul(ty+12,nullptr,10):od.Height;
 const unsigned ow=std::clamp(requestedW,32u,8192u)&~1u,oh=std::clamp(requestedH,32u,8192u)&~1u;
 if(pending[0].valid&&(sets[0].rw!=rd.Width||sets[0].rh!=rd.Height||sets[0].ow!=ow||sets[0].oh!=oh))return false; // retain LEFT resources until completion
 if(!ensure_sets(rd.Width,rd.Height,ow,oh)){failure("shared eye allocation");return false;}auto& s=sets[c.eye];
 unsigned held{};for(;held<4;++held)if(s.mutex[held]->AcquireSync(0,100)!=S_OK)break;
 if(held!=4){for(unsigned i=0;i<held;++i)s.mutex[i]->ReleaseSync(0);failure("input ownership timeout");return false;}
 if((c.eye==0||c.afw)&&s.previousValid){math::M currentWorld{},oldWorld{};float facing{};for(int i=0;i<3;++i)facing+=c.view.v[i][2]*s.previousView.v[i][2];
  if(facing<.707f)resetPending=true;
  if(math::inverse(c.view,currentWorld)&&math::inverse(s.previousView,oldWorld)){float distance{};for(int j=0;j<3;++j){const float delta=currentWorld.v[3][j]-oldWorld.v[3][j];distance+=delta*delta;}if(distance>900.f*900.f)resetPending=true;}
 }
 const bool reset=resetPending||!s.previousValid||s.evaluatedMode!=selected||s.centred!=c.centred;
 const bool overlap=!c.afw&&((c.eye==0&&deferLeftAllowed&&overlapEnabled&&shared_fences())||(c.eye==1&&pending[0].valid));
 context->CopyResource(s.texture[0].Get(),scene);
 const bool prepared=motion(s,c,depth,reset);
 const bool finished=overlap||finish.wait(device.Get(),context.Get(),1000);
 for(int i=0;i<4;++i)s.mutex[i]->ReleaseSync(prepared&&finished?1:0);
 if(!prepared||!finished){failure("motion preparation or input GPU completion");return false;}
 shared->eye=c.eye;shared->frame=c.generation;shared->mode=1;shared->dlssMode=selected;shared->jitterX=c.jitterX;shared->jitterY=c.jitterY;shared->depthInverted=c.projection.v[3][2]>0;shared->reset=reset;shared->preset=0;shared->evaluatedMode=0;
 if(overlap){
  const auto number=++serial;shared->syncMode=3;
  if(FAILED(fenceContext->Signal(inputFence.Get(),number))){failure("overlap input fence signal");return false;}context->Flush();
  auto& p=pending[c.eye];p={number,resetEpoch.load(),selected,c,true};
  auto& q=shared->req[c.eye];q.jitterX=c.jitterX;q.jitterY=c.jitterY;q.reset=reset;q.dlssMode=selected;q.evaluatedMode=0;
  MemoryBarrier();q.serial=number;SetEvent(request);
  if(!c.eye){++queuedLeft;return true;} // RIGHT rendering can proceed before LEFT NGX finishes.
  const bool replies=wait_output(number)&&answer_ok(0)&&answer_ok(1);
  p.valid=false;if(!replies){failure("overlap pair evaluation rejected");return false;}
  if(!copy_output(1,output,false))return false;
  accepted(c,selected);shared->evaluatedMode=selected;return true;
 }
 shared->syncMode=2;
 ResetEvent(done);MemoryBarrier();shared->serial=++serial;SetEvent(request);
 const auto deadline=GetTickCount64()+2000;
 while(shared->answered!=serial){const auto now=GetTickCount64();if(now>=deadline||WaitForSingleObject(done,DWORD(deadline-now))!=WAIT_OBJECT_0){++timeouts;failure("helper completion timeout");return false;}}
 MemoryBarrier();if(shared->status||shared->evaluatedMode!=selected){failure("NGX evaluation rejected");return false;}
 if(!copy_output(c.eye,output,true))return false;
 accepted(c,selected);return true;
}
}
void init_settings(){swprintf_s(settings,L"%s\\aa_rendering.ini",log::data_dir());requested=std::clamp(int(GetPrivateProfileIntW(L"AsylumVR",L"DLSSMode",3,settings)),0,5);overlapEnabled=GetPrivateProfileIntW(L"AsylumVR",L"DLSSOverlap",1,settings)!=0;}
void set_mode(int m,bool persist){requested=std::clamp(m,0,5);++resetEpoch;running=false;if(persist&&settings[0]){wchar_t text[8];swprintf_s(text,L"%d",requested.load());WritePrivateProfileStringW(L"AsylumVR",L"DLSSMode",text,settings);}BVR_LOG("[aa DLSS] requested mode %d",requested.load());}
int mode(){return requested;}
bool active(){return running&&!fault;}
bool jitter(unsigned generation,unsigned w,unsigned h,float&x,float&y){x=y=0;if(!active()||!mode()||!w||!h)return false;const unsigned count=sets[0].ow&&sets[0].rw?jitter_period(sets[0].ow,sets[0].rw):8;const unsigned phase=generation%count+1;x=halton(phase,2)-.5f;y=halton(phase,3)-.5f;return true;}
bool process(ID3D11DeviceContext* ctx,const native::Camera& c,ID3D11Texture2D* scene,ID3D11ShaderResourceView* depth,ID3D11Texture2D* out,bool deferLeftAllowed){
 if((!c.eye||c.afw)&&pending[0].valid)complete_left(out,false); // interrupted pair: finish ownership before reuse
 const auto epoch=resetEpoch.load();if(epoch!=seenEpoch){seenEpoch=epoch;resetPending=true;for(auto& s:sets)s.previousValid=false;}
 if(c.afw){rollback=false;leftAccepted=false;const bool accepted=mode()&&!fault&&c.valid&&depth&&evaluate(c,scene,depth,out,mode(),false);running=accepted;resetPending=!accepted;if(!accepted){++fallbacks;if(!depth)++noDepth;for(auto& s:sets)s.previousValid=false;}return accepted;}
 rollback=false;if(!c.eye){leftAccepted=false;leftGeneration=c.generation;}
 bool accepted=false;if(mode()&&!fault&&c.valid&&depth&&(!c.eye||leftAccepted&&leftGeneration==c.generation))accepted=evaluate(c,scene,depth,out,mode(),deferLeftAllowed);
 else if(mode()&&!depth)++noDepth;
 if(!c.eye)leftAccepted=accepted;
 else{rollback=leftAccepted&&!accepted;leftAccepted=false;running=accepted;resetPending=!accepted;if(!accepted)for(auto& s:sets)s.previousValid=false;}
 if(!accepted)++fallbacks;return accepted;
}
bool rollback_left(){return rollback;}
bool finish_left(ID3D11DeviceContext*,ID3D11Texture2D* output){return complete_left(output,!rollback);}
bool left_pending(){return pending[0].valid;}
bool overlap(){return overlapEnabled;}
void set_overlap(bool value,bool persist){overlapEnabled=value;if(persist&&settings[0])WritePrivateProfileStringW(L"AsylumVR",L"DLSSOverlap",value?L"1":L"0",settings);BVR_LOG("[aa DLSS] native transfer %s (applies at next LEFT)",value?"overlap":"synchronous safe");}
void reset(){++resetEpoch;running=false;}
void shutdown(){
 reset();if(device&&context)finish.wait(device.Get(),context.Get(),2000);for(auto& s:sets)s=Set{};shader.Reset();constants.Reset();inputFence.Reset();outputFence.Reset();fenceContext.Reset();
 if(finish.query)finish.query->Release();if(finish.c4)finish.c4->Release();if(finish.fence)finish.fence->Release();if(finish.ev)CloseHandle(finish.ev);finish={};
 if(context){context->ClearState();context->Flush();}context.Reset();device.Reset();
 if(job){CloseHandle(job);job=nullptr;}if(helperProcess){WaitForSingleObject(helperProcess,2000);CloseHandle(helperProcess);helperProcess=nullptr;}
 if(outputEvent){CloseHandle(outputEvent);outputEvent=nullptr;}if(shared){UnmapViewOfFile(shared);shared=nullptr;}
 for(auto* h:{&mapping,&request,&done})if(*h){CloseHandle(*h);*h=nullptr;}
}
void status_line(char* out,unsigned n){snprintf(out,n,"requested=%d active=%u fault=%u evaluations=%u fallback=%u noDepth=%u ngx=%d evaluatedMode=%d input=%ux%u output=%ux%u cameraMotionOnly=1 driverPreset=%d jitterPhases=%u overlap=%u sync=%d queuedLeft=%u blockedWaits=%u blockedAvgMs=%.3f fenceTimeouts=%u",mode(),active(),fault.load(),evaluations.load(),fallbacks.load(),noDepth.load(),shared?shared->ngxState:0,shared?shared->evaluatedMode:0,shared?shared->renderW:0,shared?shared->renderH:0,shared?shared->outW:0,shared?shared->outH:0,shared?shared->driverPreset:0,sets[0].ow&&sets[0].rw?jitter_period(sets[0].ow,sets[0].rw):0,overlap(),shared?shared->syncMode:0,queuedLeft,pairWaits,pairWaits?pairWaitTotal/pairWaits:0,shared?shared->fenceWaitTimeouts:0);}
void status(){char line[768]{};status_line(line,sizeof(line));BVR_LOG("[aa DLSS] %s completionTimeouts=%u helperGpuMs=%.3f helperStartingFrames=%u",line,timeouts.load(),shared?shared->gpuMs:0,helperWaits.load());}
void prewarm(ID3D11Device* d){if(d&&mode()&&!initTried)start(d);}
void draw_ui(){
 const char* modes[]={"Off","DLAA","DLSS Ultra Quality","DLSS Quality","DLSS Balanced","DLSS Performance"};int selected=mode();if(ImGui::Combo("Anti-aliasing / upscale",&selected,modes,6))set_mode(selected);
 ImGui::Text("DLSS: %s",!mode()?"off; stock rendering":active()?"evaluating independent eye histories":fault?"failed; stock rendering, restart required":"waiting for native scene depth");
 if(shared)ImGui::Text("Input %ux%u / DLSS output %ux%u",shared->renderW,shared->renderH,shared->outW,shared->outH);
 ImGui::TextWrapped("Restart the game after changing this. Fast-moving objects can smear slightly.");
 if(!scene::test_options())return;
 if(shared&&shared->driverPreset>0&&shared->driverPreset<=26)ImGui::TextWrapped("NVIDIA driver forces model preset %c. The driver overrides the app's model preference.",'A'+shared->driverPreset-1);
 bool enabled=overlap();if(ImGui::Checkbox("Faster DLSS timing (leave on)",&enabled))set_overlap(enabled);
 ImGui::TextWrapped("Native transfer: %s",shared&&shared->syncMode==3?"overlap active":overlapTried&&!inputFence?"shared fences unavailable; safe transfer":overlap()?"overlap selected; AFW uses safe transfer":"synchronous safe transfer");
 if(shared&&shared->syncMode==3)ImGui::Text("Blocked pair wait %.2f ms average (%u waits)",pairWaits?pairWaitTotal/pairWaits:0,pairWaits);
}
bool fixture(ID3D11DeviceContext* ctx,ID3D11Texture2D* scene,ID3D11ShaderResourceView* depth,const wchar_t* path){
 if(jitter_period(64,64)!=8||jitter_period(2284,1522)!=19||jitter_period(2284,1322)!=24)return false;
 overlapEnabled=false; // Keep the synchronous regression, then test real shared-fence overlap below.
 wcscpy_s(fixtureHelper,path);fault=false;requested=1;D3D11_TEXTURE2D_DESC d{};scene->GetDesc(&d);ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);ComPtr<ID3D11Texture2D> output;
 // PIMAXSTART: the helper starts without blocking; the fixture itself waits for it (the game never does).
 if(!shared)initTried=false; // the device's prewarm looked beside the test exe, before this helper path was set
 int helperState=start(dev.Get());const auto readyBy=GetTickCount64()+15000;
 while(!helperState&&GetTickCount64()<readyBy){Sleep(5);helperState=start(dev.Get());}
 printf("DLSS helper background start: %s\n",helperState>0?"ready":helperState<0?"FAILED":"timed out");if(helperState<=0)return false;
 if(FAILED(dev->CreateTexture2D(&d,nullptr,&output)))return false;
 native::Camera camera{};camera.valid=true;camera.width=d.Width;camera.height=d.Height;camera.generation=123;camera.view.v[0][0]=camera.view.v[1][1]=camera.view.v[2][2]=camera.view.v[3][3]=1;
 camera.projection.v[0][0]=camera.projection.v[1][1]=camera.projection.v[2][3]=1;camera.projection.v[2][2]=1.001f;camera.projection.v[3][2]=-.1f;
 bool ok=true;for(int e=0;e<2;++e){camera.eye=e;if(!process(ctx,camera,scene,depth,output.Get()))ok=false;}
 if(shared)ok=ok&&shared->ngxState==1&&shared->evaluatedMode==1&&evaluations==2;
 // A missing RIGHT depth must reject the pair and request LEFT rollback.
 camera.eye=0;ok=process(ctx,camera,scene,depth,output.Get())&&ok;camera.eye=1;
 ok=!process(ctx,camera,scene,nullptr,output.Get())&&rollback_left()&&!active()&&ok;
 output.Reset();d.Width=d.Height=96;ok=SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&output))&&ok;set_mode(3,false);
 for(int e=0;e<2;++e){camera.eye=e;ok=process(ctx,camera,scene,depth,output.Get())&&ok;}
 ok=shared&&shared->evaluatedMode==3&&active()&&ok;
 // Shift the camera over a known constant .75-depth plane, with declared
 // jitter. Verify copied depth and jitter-free motion, not just NGX status.
 ++camera.generation;camera.view.v[3][0]=.125f;jitter(camera.generation,camera.width,camera.height,camera.jitterX,camera.jitterY);
 camera.projection.v[2][0]=2*camera.jitterX/camera.width;camera.projection.v[2][1]=-2*camera.jitterY/camera.height;
 for(int e=0;e<2;++e){camera.eye=e;ok=process(ctx,camera,scene,depth,output.Get())&&ok;}
 if(!shared||!sets[0].mutex[0]||!sets[0].mutex[1]||!sets[0].mutex[2]||!sets[0].mutex[3]){printf("DLSS fixture initialization failed before readback: shared=%u ready=%u NGX=%d fault=%u error=%lu\n",shared!=nullptr,shared?shared->helperReady:0,shared?shared->ngxState:0,fault.load(),GetLastError());return false;}
 auto& s=sets[0];
 auto read_center=[&](int slot,void* value,unsigned bytes)->bool{
  if(s.mutex[slot]->AcquireSync(0,100)!=S_OK)return false;
  D3D11_TEXTURE2D_DESC read{};s.texture[slot]->GetDesc(&read);read.BindFlags=read.MiscFlags=0;read.Usage=D3D11_USAGE_STAGING;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;bool got=SUCCEEDED(device->CreateTexture2D(&read,nullptr,&staging));
  if(got){context->CopyResource(staging.Get(),s.texture[slot].Get());context->Flush();D3D11_MAPPED_SUBRESOURCE map{};got=SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));if(got){memcpy(value,static_cast<const BYTE*>(map.pData)+(read.Height/2)*map.RowPitch+(read.Width/2)*bytes,bytes);context->Unmap(staging.Get(),0);}}
  s.mutex[slot]->ReleaseSync(0);return got;
 };
 float z{};uint16_t mv[2]{};ok=read_center(1,&z,4)&&read_center(2,mv,4)&&ok;
 auto half=[](uint16_t h){const int sign=h&0x8000?-1:1,exponent=(h>>10)&31,mantissa=h&1023;return sign*std::ldexp(exponent?1.f+mantissa/1024.f:mantissa/1024.f,exponent?exponent-15:-14);};
 const float expectedX=-.125f*(1.001f-.75f)/.1f*(camera.width*.5f);
 ok=std::fabs(z-.75f)<.00001f&&std::fabs(half(mv[0])-expectedX)<.02f&&std::fabs(half(mv[1]))<.0001f&&ok;
 printf("DLSS GPU: real DLAA+Quality evaluations both eyes; depth %.6f, camera motion %.4f/%.4f expected %.4f/0, missing-right pair rollback %s\n",z,half(mv[0]),half(mv[1]),expectedX,ok?"PASS":"FAIL");
 BVR_LOG("[aa DLSS fixture] depth %.6f camera motion %.4f/%.4f expected %.4f/0, pair rollback and Quality96x96 checked",z,half(mv[0]),half(mv[1]),expectedX);status();
 // Real cross-process GPU-fence regression: distinct colours prove both
 // outputs came from their own eye; the LEFT call must only enqueue work.
 ComPtr<ID3D11Texture2D> red,blue,leftOut;
 D3D11_TEXTURE2D_DESC colour{};scene->GetDesc(&colour);colour.Format=DXGI_FORMAT_R8G8B8A8_UNORM;colour.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;colour.MiscFlags=0;
 std::vector<uint32_t> solid(size_t(colour.Width)*colour.Height,0xff0000ff);D3D11_SUBRESOURCE_DATA pixels{solid.data(),colour.Width*4,0};
 bool overlapOk=SUCCEEDED(dev->CreateTexture2D(&colour,&pixels,&red));
 std::fill(solid.begin(),solid.end(),0xffff0000);overlapOk=SUCCEEDED(dev->CreateTexture2D(&colour,&pixels,&blue))&&overlapOk;
 output->GetDesc(&colour);overlapOk=SUCCEEDED(dev->CreateTexture2D(&colour,nullptr,&leftOut))&&overlapOk;
 auto pixel=[&](ID3D11Texture2D* image,uint8_t* rgba){
  D3D11_TEXTURE2D_DESC rd{};image->GetDesc(&rd);rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;rd.BindFlags=rd.MiscFlags=0;
  ComPtr<ID3D11Texture2D> rb;if(FAILED(dev->CreateTexture2D(&rd,nullptr,&rb)))return false;
  ctx->CopyResource(rb.Get(),image);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(ctx->Map(rb.Get(),0,D3D11_MAP_READ,0,&m)))return false;
  memcpy(rgba,static_cast<const BYTE*>(m.pData)+(rd.Height/2)*m.RowPitch+(rd.Width/2)*4,4);ctx->Unmap(rb.Get(),0);return true;
 };
 set_overlap(true,false);reset();camera.afw=false;
 const unsigned priorEval=evaluations.load();++camera.generation;camera.eye=0;
 overlapOk=process(ctx,camera,red.Get(),depth,leftOut.Get())&&pending[0].valid&&evaluations==priorEval&&inputFence&&outputFence&&overlapOk;
 camera.eye=1;overlapOk=process(ctx,camera,blue.Get(),depth,output.Get())&&pending[0].valid&&overlapOk;
 overlapOk=finish_left(ctx,leftOut.Get())&&!left_pending()&&sets[0].previousValid&&sets[1].previousValid&&evaluations==priorEval+2&&overlapOk;
 uint8_t lp[4]{},rp[4]{};overlapOk=pixel(leftOut.Get(),lp)&&pixel(output.Get(),rp)&&lp[0]>200&&lp[2]<20&&rp[2]>200&&rp[0]<20&&overlapOk;
 const int soakPairs=GetEnvironmentVariableW(L"AA_DLSS_SOAK",nullptr,0)?4096:16;
 auto memory=[](int pair){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));printf("DLSS soak pair %d private %.1f MiB\n",pair,m.PrivateUsage/1048576.);};memory(0);
 for(int i=0;i<soakPairs&&overlapOk;++i){if(i%512==0)memory(i);++camera.generation;camera.eye=0;overlapOk=process(ctx,camera,red.Get(),depth,leftOut.Get());camera.eye=1;overlapOk=process(ctx,camera,blue.Get(),depth,output.Get())&&finish_left(ctx,leftOut.Get())&&!left_pending()&&active()&&overlapOk;}
 overlapOk=shared&&shared->syncMode==3&&!shared->fenceWaitTimeouts&&evaluations==priorEval+2+2*soakPairs&&!fault&&overlapOk;
 printf("DLSS overlap GPU: LEFT enqueues without evaluation wait, %d paired completions; LEFT RGBA %u/%u/%u/%u RIGHT %u/%u/%u/%u, valid separate histories, zero fence timeouts %s\n",soakPairs+1,lp[0],lp[1],lp[2],lp[3],rp[0],rp[1],rp[2],rp[3],overlapOk?"PASS":"FAIL");ok=overlapOk&&ok;
 // Rejecting RIGHT must leave the stock LEFT pixels intact, finish ownership,
 // invalidate histories, and never commit half of the NGX pair.
 std::vector<uint32_t> green(size_t(colour.Width)*colour.Height,0xff00ff00);ctx->UpdateSubresource(leftOut.Get(),0,nullptr,green.data(),colour.Width*4,0);
 unsigned before=evaluations.load();++camera.generation;camera.eye=0;
 bool missing=process(ctx,camera,red.Get(),depth,leftOut.Get());camera.eye=1;
 missing=!process(ctx,camera,blue.Get(),nullptr,output.Get())&&rollback_left()&&!finish_left(ctx,leftOut.Get())&&!left_pending()&&evaluations==before&&!active()&&!fault&&!sets[0].previousValid&&!sets[1].previousValid&&missing;
 missing=pixel(leftOut.Get(),lp)&&lp[0]<20&&lp[1]>200&&lp[2]<20&&missing;
 printf("DLSS overlap missing-RIGHT: queued LEFT drained, original green output kept, both histories invalid %s\n",missing?"PASS":"FAIL");ok=missing&&ok;
 // Teardown/interrupt has a bounded LEFT-only completion. A subsequent
 // complete pair must still be able to reuse the shared textures.
 ++camera.generation;camera.eye=0;before=evaluations.load();
 bool early=process(ctx,camera,red.Get(),depth,leftOut.Get())&&finish_left(ctx,leftOut.Get())&&!left_pending()&&evaluations==before+1&&!fault;
 ++camera.generation;camera.eye=0;early=process(ctx,camera,red.Get(),depth,leftOut.Get())&&early;camera.eye=1;
 early=process(ctx,camera,blue.Get(),depth,output.Get())&&finish_left(ctx,leftOut.Get())&&early;
 printf("DLSS overlap early LEFT: bounded completion, next stereo pair succeeds %s\n",early?"PASS":"FAIL");ok=early&&ok;
 // Reset between eyes cancels the pending pair without replacing stock.
 ++camera.generation;camera.eye=0;before=evaluations.load();bool cancelled=process(ctx,camera,red.Get(),depth,leftOut.Get());reset();camera.eye=1;
 cancelled=!process(ctx,camera,blue.Get(),depth,output.Get())&&rollback_left()&&!finish_left(ctx,leftOut.Get())&&!left_pending()&&evaluations==before&&!fault&&cancelled;
 // Switching DLSS off between eyes must also discard the queued LEFT.
 ++camera.generation;camera.eye=0;before=evaluations.load();bool disabled=process(ctx,camera,red.Get(),depth,leftOut.Get());set_mode(0,false);camera.eye=1;
 disabled=!process(ctx,camera,blue.Get(),depth,output.Get())&&rollback_left()&&!finish_left(ctx,leftOut.Get())&&!left_pending()&&evaluations==before&&!fault&&disabled;set_mode(3,false);
 printf("DLSS overlap mode-off between eyes: queued LEFT discarded, stock pair required %s\n",disabled?"PASS":"FAIL");ok=disabled&&ok;
 // No held XR image (pair pacing disabled): requesting overlap must still
 // use synchronous transfer; never write a released XR image later.
 ++camera.generation;camera.eye=0;bool noHold=process(ctx,camera,scene,depth,output.Get(),false)&&!left_pending()&&shared->syncMode==2;camera.eye=1;
 noHold=process(ctx,camera,scene,depth,output.Get(),false)&&active()&&noHold;
 printf("DLSS overlap cancellation / no held XR image: stock pair on reset / safe synchronous fallback %s\n",cancelled&&noHold?"PASS":"FAIL");ok=cancelled&&noHold&&ok;
 set_overlap(false,false);
 // AFW uses independent single-eye generations, not a LEFT/RIGHT native
 // pair. Both history banks must still evaluate through the real helper.
 reset();camera.afw=true;
 for(int i=0;i<4;++i){camera.eye=i&1;++camera.generation;ok=process(ctx,camera,scene,depth,output.Get())&&active()&&ok;}
 printf("DLSS GPU: AFW alternating single-eye generations %s\n",ok?"PASS":"FAIL");
 // Cleared Asylum reversed depth must remain zero in NGX, while camera
 // translation produces NO sky motion. Old reconstruction yielded vectors
 // from a point behind the camera (same root as AFW's discarded sky).
 D3D11_TEXTURE2D_DESC skyDesc{};skyDesc.Width=skyDesc.Height=64;skyDesc.MipLevels=skyDesc.ArraySize=skyDesc.SampleDesc.Count=1;skyDesc.Format=DXGI_FORMAT_R32_FLOAT;skyDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 float skyValues[64*64]{};D3D11_SUBRESOURCE_DATA skyData{skyValues,64*4,0};ComPtr<ID3D11Texture2D> sky;ComPtr<ID3D11ShaderResourceView> skySrv;
 ok=SUCCEEDED(device->CreateTexture2D(&skyDesc,&skyData,&sky))&&SUCCEEDED(device->CreateShaderResourceView(sky.Get(),nullptr,&skySrv))&&ok;
 camera.view={};camera.view.v[0][0]=camera.view.v[1][1]=camera.view.v[2][2]=camera.view.v[3][3]=1;
 camera.projection={};camera.projection.v[0][0]=camera.projection.v[1][1]=camera.projection.v[2][3]=1;camera.projection.v[2][2]=.001f;camera.projection.v[3][2]=9.99f;
 camera.jitterX=camera.jitterY=0;s.previousView=camera.view;s.previousProjection=camera.projection;s.previousValid=true;camera.view.v[3][0]=1000;
 unsigned held{};for(;held<4;++held)if(s.mutex[held]->AcquireSync(0,100)!=S_OK)break;
 bool skyOk=held==4&&skySrv&&motion(s,camera,skySrv.Get(),false)&&finish.wait(device.Get(),context.Get(),1000);
 for(unsigned i=0;i<held;++i)s.mutex[i]->ReleaseSync(0);
 float skyZ{};uint16_t skyMv[2]{};skyOk=skyOk&&read_center(1,&skyZ,4)&&read_center(2,skyMv,4)&&skyZ==0&&std::fabs(half(skyMv[0]))<.0001f&&std::fabs(half(skyMv[1]))<.0001f;ok=skyOk&&ok;
 printf("DLSS sky regression: raw inverted clear depth %.5f, translation motion %.5f/%.5f expected0/0 %s\n",skyZ,half(skyMv[0]),half(skyMv[1]),skyOk?"PASS":"FAIL");
 // Asylum hardware-DS route: forward depth at large world coordinates,
 // independent analytical pixel motion across an actual GPU dispatch.
 camera.view={};camera.view.v[0][0]=camera.view.v[1][1]=camera.view.v[2][2]=camera.view.v[3][3]=1;
 camera.view.v[3][0]=120000;camera.view.v[3][1]=-70000;camera.view.v[3][2]=300000;
 camera.projection.v[2][2]=.999f;camera.projection.v[3][2]=-9.99f;
 camera.jitterX=.375f;camera.jitterY=-.3125f;
 s.previousView=camera.view;s.previousProjection=camera.projection;s.previousValid=true;
 camera.view.v[3][0]+=.125f;camera.projection.v[2][0]=2*camera.jitterX/s.rw;camera.projection.v[2][1]=-2*camera.jitterY/s.rh;
 held=0;for(;held<4;++held)if(s.mutex[held]->AcquireSync(0,100)!=S_OK)break;
 bool forwardOk=held==4&&motion(s,camera,depth,false)&&finish.wait(device.Get(),context.Get(),1000);
 for(unsigned i=0;i<held;++i)s.mutex[i]->ReleaseSync(0);
 float hardwareZ{};uint16_t hardwareMv[2]{};const float expectedHardware=-.125f*(.999f-.75f)/9.99f*(s.rw*.5f);
 forwardOk=forwardOk&&read_center(1,&hardwareZ,4)&&read_center(2,hardwareMv,4)&&std::fabs(hardwareZ-.75f)<.00001f&&std::fabs(half(hardwareMv[0])-expectedHardware)<.0001f&&std::fabs(half(hardwareMv[1]))<.0001f;
 printf("DLSS physical forward-depth/large-origin regression: %.6f motion %.6f/%.6f expected %.6f/0 %s\n",hardwareZ,half(hardwareMv[0]),half(hardwareMv[1]),expectedHardware,forwardOk?"PASS":"FAIL");ok=forwardOk&&ok;
 // Deliberately omit input-fence signal. Helper must reject on CPU in100ms
 // and publish a completion answer, without any NGX/keyed-mutex GPU wait.
 const auto missed=++serial;shared->syncMode=3;auto& rq=shared->req[0];rq.dlssMode=3;rq.reset=1;rq.evaluatedMode=0;MemoryBarrier();rq.serial=missed;SetEvent(request);
 bool failClosed=wait_output(missed);MemoryBarrier();failClosed=rq.answered==missed&&rq.status==-12&&rq.evaluatedMode==0&&shared->fenceWaitTimeouts==1&&failClosed;
 printf("DLSS overlap unsignaled input fence: CPU timeout rejected before NGX, bounded output completion %s\n",failClosed?"PASS":"FAIL");ok=failClosed&&ok;
 // Fixture alone ends its own helper; this never closes the game process.
 if(job){CloseHandle(job);job=nullptr;}if(helperProcess){WaitForSingleObject(helperProcess,2000);CloseHandle(helperProcess);helperProcess=nullptr;}
 return ok;
}
}
