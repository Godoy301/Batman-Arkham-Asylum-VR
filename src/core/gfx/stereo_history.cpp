// Capture at the submission image boundary, not the desktop mirror (STR-063).
// Three async readback slots, a bounded encoder queue and a compressed ten-second
// ring. A busy GPU/encoder drops evidence; it never waits or queues XR frames.
#include "stereo_history.h"
#include "blit.h"
#include "core/util/log.h"
#include "game/asylum/native_stereo.h"
#include "game/asylum/dlss.h"
#include <wrl/client.h>
#include <wincodec.h>
#include <imgui.h>
#include <atomic>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdio>
using Microsoft::WRL::ComPtr;
namespace bvr::stereo_history {
namespace {
constexpr unsigned kEyeWidth=960,kInterval=100,kSeconds=10;
constexpr size_t kBytes=64*1024*1024;
struct Frame {
 uint64_t ms{};uint32_t generation{};unsigned w{},h{},dlssMode{};bool afw{},warped{},dlss{};
 bvr::aa::native::Camera cameras[2]{};
 std::vector<unsigned char> pixels;
};
struct Encoded {Frame meta;std::vector<unsigned char> jpeg;};
struct State {
 std::mutex mutex;std::condition_variable wake;
 std::deque<Frame> input;
 std::deque<std::shared_ptr<Encoded>> history;
 size_t bytes{};bool save{},working{},stop{};
 uint64_t trigger{};std::atomic<unsigned> drops{},saved{},frames{},failed{};
 std::atomic<unsigned> span{};std::atomic<bool> writing{};
 HANDLE thread{};wchar_t lastDir[MAX_PATH]{};
};
State& state(){static auto* s=new State;return *s;} // worker lives until host exit
struct Slot {
 ComPtr<ID3D11Texture2D> image[2],read[2];ComPtr<ID3D11RenderTargetView> rt[2];ComPtr<ID3D11Query> ready;
 Frame meta;bool pending{},left{};unsigned mask{};
};
Slot slots[3];int current=-1;unsigned sizeW{},sizeH{};uint64_t nextAt{};
wchar_t fixtureRoot[MAX_PATH]{};
ComPtr<ID3D11Texture2D> sourceCopy;ComPtr<ID3D11ShaderResourceView> sourceView;
bool encode(IWICImagingFactory* factory,Frame& frame,std::vector<unsigned char>& out){
 ComPtr<IStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> image;ComPtr<IPropertyBag2> properties;
 if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&encoder))||
    FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&image,&properties)))return false;
 PROPBAG2 prop{};prop.pstrName=const_cast<LPOLESTR>(L"ImageQuality");VARIANT quality{};quality.vt=VT_R4;quality.fltVal=.94f;
 if(FAILED(properties->Write(1,&prop,&quality))||FAILED(image->Initialize(properties.Get()))||FAILED(image->SetSize(frame.w*2,frame.h)))return false;
 auto format=GUID_WICPixelFormat24bppBGR;
 if(FAILED(image->SetPixelFormat(&format))||format!=GUID_WICPixelFormat24bppBGR||
    FAILED(image->WritePixels(frame.h,frame.w*6,UINT(frame.pixels.size()),frame.pixels.data()))||FAILED(image->Commit())||FAILED(encoder->Commit()))return false;
 STATSTG info{};if(FAILED(stream->Stat(&info,STATFLAG_NONAME))||info.cbSize.HighPart||!info.cbSize.LowPart)return false;
 LARGE_INTEGER zero{};if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return false;
 out.resize(info.cbSize.LowPart);ULONG got{};
 return SUCCEEDED(stream->Read(out.data(),ULONG(out.size()),&got))&&got==out.size();
}
bool export_clip(const std::vector<std::shared_ptr<Encoded>>& inputPictures,uint64_t trigger){
 // User A1f clips contained out-of-order readback completions. Establish
 // chronological order at the export boundary, including timestamps/cameras.
 auto pictures=inputPictures;
 std::stable_sort(pictures.begin(),pictures.end(),[](const auto&a,const auto&b){return a->meta.ms<b->meta.ms;});
 auto& s=state();if(pictures.empty()){BVR_LOG("[aa history] no completed stereo frames before button");return false;}
 wchar_t root[MAX_PATH],dir[MAX_PATH],path[MAX_PATH];
 if(fixtureRoot[0])wcscpy_s(root,fixtureRoot);else{if(!log::data_dir()[0])return false;swprintf_s(root,L"%s\\captures",log::data_dir());}CreateDirectoryW(root,nullptr);
 SYSTEMTIME t{};GetLocalTime(&t);swprintf_s(dir,L"%s\\A2p_history_%04u%02u%02u_%02u%02u%02u_%03u",root,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
 if(!CreateDirectoryW(dir,nullptr))return false;swprintf_s(path,L"%s\\frames",dir);if(!CreateDirectoryW(path,nullptr))return false;
 swprintf_s(path,L"%s\\frames.csv",dir);FILE* manifest=_wfopen(path,L"wt");if(!manifest)return false;
 fprintf(manifest,"image,ms_before_button,generation,eye_width,eye_height,afw,warp_valid,dlss_active,dlss_requested_mode\n");
 bool ok=true;unsigned i{};
 for(const auto& p:pictures){swprintf_s(path,L"%s\\frames\\%04u.jpg",dir,i);FILE* f=_wfopen(path,L"wb");if(!f){ok=false;break;}
  ok=fwrite(p->jpeg.data(),1,p->jpeg.size(),f)==p->jpeg.size()&&ok;fclose(f);
  fprintf(manifest,"frames/%04u.jpg,%lld,%u,%u,%u,%u,%u,%u,%u\n",i,static_cast<long long>(trigger)-static_cast<long long>(p->meta.ms),p->meta.generation,p->meta.w,p->meta.h,p->meta.afw,p->meta.warped,p->meta.dlss,p->meta.dlssMode);++i;
 }
 fclose(manifest);
 swprintf_s(path,L"%s\\cameras.bin",dir);if(FILE* f=_wfopen(path,L"wb")){for(const auto&p:pictures)fwrite(p->meta.cameras,sizeof(p->meta.cameras),1,f);fclose(f);}else ok=false;
 swprintf_s(path,L"%s\\review.html",dir);if(FILE* f=_wfopen(path,L"wt")){
  fprintf(f,"<!doctype html><meta charset=utf-8><title>Asylum stereo replay</title><style>body{background:#14181d;color:#eee;font:16px system-ui;margin:18px}img{display:block;max-width:100%%;max-height:85vh}input{width:60%%}button{padding:8px}</style><h1>Asylum stereo replay</h1><p>Left eye | Right eye. Final submitted pictures, sampled at up to 10 pairs/s. 94%% JPEG; full-resolution stills are separate. Gaps retain actual timing.</p><button id=play>Play</button> <input id=s type=range min=0 max=%u value=0> <span id=label></span><img id=pic><script>const times=[",i?i-1:0);
  for(unsigned n=0;n<i;++n)fprintf(f,"%s%llu",n?",":"",pictures[n]->meta.ms-pictures.front()->meta.ms);
  fprintf(f,"],before=[");for(unsigned n=0;n<i;++n)fprintf(f,"%s%lld",n?",":"",static_cast<long long>(trigger)-static_cast<long long>(pictures[n]->meta.ms));
  fprintf(f,"];let timer=null;const s=document.getElementById('s'),p=document.getElementById('pic'),label=document.getElementById('label');function show(){p.src='frames/'+String(s.value).padStart(4,'0')+'.jpg';label.textContent=(before[+s.value]/1000).toFixed(2)+' s before button';}function next(){let n=+s.value;if(n+1>=times.length){timer=null;return}timer=setTimeout(()=>{s.value=n+1;show();next()},times[n+1]-times[n])}s.oninput=()=>{clearTimeout(timer);timer=null;show()};document.getElementById('play').onclick=()=>{if(timer){clearTimeout(timer);timer=null;return}if(+s.value==times.length-1)s.value=0;show();next()};show();</script>");fclose(f);
 }else ok=false;
 swprintf_s(path,L"%s\\README.txt",dir);if(FILE* f=_wfopen(path,L"wt")){fprintf(f,"Asylum A2p rolling stereo evidence\nActual span %.3fs; %u paired frames; trigger uptime %llu ms.\nOpen review.html for replay, or inspect frame JPEGs/frames.csv.\nLeft/right always share generation. AFW rows identify warped or flat fallback.\nCapture is reduced to %upx per eye and JPEG94 to bound x86 memory/cost.\nCamera records are two native::Camera structs per frames.csv row, source A2p.\nDoF/MotionBlur profile=False; shader-level absence not established.\nDrop count at export %u; encoder errors %u.\n",(pictures.back()->meta.ms-pictures.front()->meta.ms)/1000.,i,trigger,pictures.front()->meta.w,s.drops.load(),s.failed.load());fclose(f);}
 {std::lock_guard lock(s.mutex);wcscpy_s(s.lastDir,dir);}
 BVR_LOG("[aa history] saved %u stereo pictures, %.2fs ending %.2fs before button: %ls",i,(pictures.back()->meta.ms-pictures.front()->meta.ms)/1000.,(trigger-pictures.back()->meta.ms)/1000.,dir);return ok;
}
DWORD WINAPI worker(void*){
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IWICImagingFactory> factory;
 CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));auto& s=state();
 for(;;){Frame next;bool have{};std::vector<std::shared_ptr<Encoded>> copy;uint64_t trigger{};
  {std::unique_lock lock(s.mutex);s.wake.wait(lock,[&]{return s.stop||s.save||!s.input.empty();});if(s.stop)break;
   // Drain pending readbacks from the pre-button window before exporting.
   if(!s.input.empty()){next=std::move(s.input.front());s.input.pop_front();s.working=true;have=true;}
   else if(s.save){s.save=false;s.writing=true;trigger=s.trigger;for(auto& p:s.history)if(p->meta.ms<=trigger&&trigger-p->meta.ms<=kSeconds*1000)copy.push_back(p);}
  }
  if(have){auto item=std::make_shared<Encoded>();const bool ok=factory&&encode(factory.Get(),next,item->jpeg);next.pixels.clear();next.pixels.shrink_to_fit();item->meta=std::move(next);
   {std::lock_guard lock(s.mutex);s.working=false;if(ok){s.bytes+=item->jpeg.size();s.history.push_back(item);
     std::sort(s.history.begin(),s.history.end(),[](const auto&a,const auto&b){return a->meta.ms<b->meta.ms;});
     while(!s.history.empty()&&(s.bytes>kBytes||s.history.back()->meta.ms-s.history.front()->meta.ms>kSeconds*1000)){s.bytes-=s.history.front()->jpeg.size();s.history.pop_front();}
     s.frames=unsigned(s.history.size());s.span=s.history.empty()?0:unsigned(s.history.back()->meta.ms-s.history.front()->meta.ms);
    }else ++s.failed;
   }s.wake.notify_one();
  }else if(trigger){if(export_clip(copy,trigger))++s.saved;else ++s.failed;s.writing=false;}
 }
 CoUninitialize();return 0;
}
void ensure_worker(){auto& s=state();if(!s.thread)s.thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);}
bool ensure(ID3D11DeviceContext* ctx,ID3D11Texture2D* source){
 D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);const unsigned w=std::min(kEyeWidth,desc.Width),h=std::max(1u,unsigned(uint64_t(desc.Height)*w/desc.Width));
 if(w==sizeW&&h==sizeH&&slots[0].ready)return true;
 reset_gpu();ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);
 D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 for(auto& slot:slots){D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};if(FAILED(dev->CreateQuery(&q,&slot.ready)))return false;
  for(int e=0;e<2;++e){if(FAILED(dev->CreateTexture2D(&d,nullptr,&slot.image[e]))||FAILED(dev->CreateRenderTargetView(slot.image[e].Get(),nullptr,&slot.rt[e])))return false;
   auto r=d;r.BindFlags=0;r.Usage=D3D11_USAGE_STAGING;r.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(dev->CreateTexture2D(&r,nullptr,&slot.read[e])))return false;}
 }sizeW=w;sizeH=h;ensure_worker();return state().thread!=nullptr;
}
bool copy_eye(ID3D11DeviceContext* ctx,Slot& slot,int eye,ID3D11Texture2D* source){
 // XR only promises COLOR_ATTACHMENT/TRANSFER_DST, not a shader-readable
 // resource. Copy its acquired image into an owned SRV texture first.
 ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);D3D11_TEXTURE2D_DESC d{},old{};source->GetDesc(&d);if(sourceCopy)sourceCopy->GetDesc(&old);
 DXGI_FORMAT textureFormat{},viewFormat{};
 switch(d.Format){case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:textureFormat=DXGI_FORMAT_R8G8B8A8_TYPELESS;viewFormat=DXGI_FORMAT_R8G8B8A8_UNORM;break;
 case DXGI_FORMAT_B8G8R8A8_TYPELESS:case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:textureFormat=DXGI_FORMAT_B8G8R8A8_TYPELESS;viewFormat=DXGI_FORMAT_B8G8R8A8_UNORM;break;default:return false;}
 if(!sourceCopy||old.Width!=d.Width||old.Height!=d.Height||old.Format!=textureFormat){sourceCopy.Reset();sourceView.Reset();auto owned=d;owned.Format=textureFormat;owned.Usage=D3D11_USAGE_DEFAULT;owned.BindFlags=D3D11_BIND_SHADER_RESOURCE;owned.CPUAccessFlags=owned.MiscFlags=0;
  D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=viewFormat;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
  if(FAILED(dev->CreateTexture2D(&owned,nullptr,&sourceCopy))||FAILED(dev->CreateShaderResourceView(sourceCopy.Get(),&view,&sourceView)))return false;
 }
 ctx->CopyResource(sourceCopy.Get(),source);
 if(!blit::shift_copy(ctx,slot.rt[eye].Get(),sizeW,sizeH,sourceView.Get(),0))return false;
 ctx->CopyResource(slot.read[eye].Get(),slot.image[eye].Get());return true;
}
}
void poll(ID3D11DeviceContext* ctx){
 if(!ctx)return;auto& s=state();
 for(auto& slot:slots){if(!slot.pending||ctx->GetData(slot.ready.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)continue;
  {std::lock_guard lock(s.mutex);if(s.input.size()>=2){slot.pending=false;++s.drops;continue;}}
  D3D11_MAPPED_SUBRESOURCE map[2]{};
  if(ctx->Map(slot.read[0].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map[0])!=S_OK)continue;
  if(ctx->Map(slot.read[1].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map[1])!=S_OK){ctx->Unmap(slot.read[0].Get(),0);continue;}
  auto frame=slot.meta;frame.pixels.resize(size_t(sizeW)*sizeH*6);
  for(unsigned y=0;y<sizeH;++y)for(int e=0;e<2;++e){auto* dst=frame.pixels.data()+size_t(y)*sizeW*6+e*sizeW*3;const auto* src=static_cast<const unsigned char*>(map[e].pData)+y*map[e].RowPitch;
   for(unsigned x=0;x<sizeW;++x){dst[x*3]=src[x*4+2];dst[x*3+1]=src[x*4+1];dst[x*3+2]=src[x*4];}}
  ctx->Unmap(slot.read[0].Get(),0);ctx->Unmap(slot.read[1].Get(),0);slot.pending=false;
  {std::lock_guard lock(s.mutex);s.input.push_back(std::move(frame));}s.wake.notify_one();
 }
}
void eye(ID3D11DeviceContext* ctx,int e,ID3D11Texture2D* shown,uint32_t generation,bool afw,bool warped){
 if(!ctx||!shown||e<0||e>1||!generation)return;poll(ctx);if(!ensure(ctx,shown)){++state().failed;return;}
 const uint64_t now=GetTickCount64();
 if(current>=0&&(slots[current].meta.generation!=generation||slots[current].meta.afw!=afw)){slots[current].left=false;current=-1;++state().drops;}
 if(current<0){if(now<nextAt||(!afw&&e!=0))return;
  for(int i=0;i<3;++i)if(!slots[i].pending&&!slots[i].left){current=i;break;}if(current<0){++state().drops;return;}
  auto& slot=slots[current];slot.mask=0;slot.meta={};slot.meta.ms=now;slot.meta.generation=generation;slot.meta.w=sizeW;slot.meta.h=sizeH;slot.meta.afw=afw;slot.meta.warped=warped;slot.meta.dlss=aa::dlss::active();slot.meta.dlssMode=aa::dlss::mode();
 }
 auto& slot=slots[current];if(!copy_eye(ctx,slot,e,shown)){slot.left=false;current=-1;++state().failed;return;}
 if(slot.mask&(1u<<e)){slot.left=false;current=-1;++state().drops;return;}
 slot.mask|=1u<<e;
 aa::native::Camera camera{};if(aa::native::camera(camera)&&camera.eye==e)slot.meta.cameras[e]=camera;
 if(slot.mask!=3){slot.left=true;return;}
 slot.meta.dlss=aa::dlss::active(); // deferred LEFT and pair rollback have now completed
 slot.left=false;slot.pending=true;ctx->End(slot.ready.Get());current=-1;nextAt=now+kInterval;
}
void replace_left(ID3D11DeviceContext* ctx,ID3D11Texture2D* shown,uint32_t generation){if(current>=0&&slots[current].left&&slots[current].meta.generation==generation)copy_eye(ctx,slots[current],0,shown);}
void reset_gpu(){current=-1;sizeW=sizeH=0;for(auto& slot:slots)slot=Slot{};sourceView.Reset();sourceCopy.Reset();nextAt=0;}
void(*saveListener)()=nullptr;void set_save_listener(void(*listener)()){saveListener=listener;}
void request_save(){if(saveListener)saveListener();auto& s=state();ensure_worker();{std::lock_guard lock(s.mutex);if(s.save||s.writing)return;s.trigger=GetTickCount64();s.save=true;}s.wake.notify_one();BVR_LOG("[aa history] button: preserve preceding ten seconds of completed stereo pairs");}
void draw_ui(){auto& s=state();if(ImGui::Button("Save last 10 seconds (stereo)"))request_save();
 ImGui::Text("Buffered %.1fs / %u pairs; dropped %u",s.span.load()/1000.f,s.frames.load(),s.drops.load());
 ImGui::TextWrapped("Saves BEFORE the press. F2 also saves. Both eyes, up to 10 pairs/s at 960px per eye. Open review.html in Batman_Asylum/captures to replay.");
 if(s.writing)ImGui::TextUnformatted("Saving replay in background...");else if(s.saved)ImGui::Text("Saved %u replay(s) in Batman_Asylum/captures",s.saved.load());
}
bool fixture(ID3D11DeviceContext* ctx){
 // Real GPU: distinct eye colours and generation rejection, async Map path,
 // actual WIC JPEG encode/decode/export. The harness uses a TEMP evidence root.
 wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);swprintf_s(fixtureRoot,L"%sAsylumHistoryFixture-%lu",temp,GetCurrentProcessId());
 ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);D3D11_TEXTURE2D_DESC d{};d.Width=32;d.Height=24;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.BindFlags=D3D11_BIND_RENDER_TARGET; // runtime-like: NO SRV
 unsigned char pixels[32*24*4];for(unsigned i=0;i<32*24;++i){pixels[i*4]=255;pixels[i*4+1]=pixels[i*4+2]=0;pixels[i*4+3]=255;}
 D3D11_SUBRESOURCE_DATA data{pixels,32*4,0};ComPtr<ID3D11Texture2D> left,right;
 if(FAILED(dev->CreateTexture2D(&d,&data,&left)))return false;
 for(unsigned i=0;i<32*24;++i){pixels[i*4]=0;pixels[i*4+2]=255;}if(FAILED(dev->CreateTexture2D(&d,&data,&right)))return false;
 eye(ctx,0,left.Get(),1,false);eye(ctx,1,right.Get(),2,false);const bool reject=current<0;
 eye(ctx,0,left.Get(),3,false);eye(ctx,1,right.Get(),3,false);ctx->Flush();
 const auto deadline=GetTickCount64()+3000;while(GetTickCount64()<deadline){poll(ctx);if(state().frames.load())break;Sleep(1);}
 std::shared_ptr<Encoded> p;{std::lock_guard lock(state().mutex);if(!state().history.empty())p=state().history.back();}
 bool ok=reject&&p&&p->meta.generation==3&&p->meta.w==32&&p->meta.h==24;
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> image;ComPtr<IWICFormatConverter> convert;
 if(ok)ok=SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromMemory(p->jpeg.data(),DWORD(p->jpeg.size())))&&SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))&&SUCCEEDED(decoder->GetFrame(0,&image))&&SUCCEEDED(factory->CreateFormatConverter(&convert))&&SUCCEEDED(convert->Initialize(image.Get(),GUID_WICPixelFormat24bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
 unsigned char output[64*24*3]{};if(ok)ok=SUCCEEDED(convert->CopyPixels(nullptr,64*3,sizeof(output),output));
 const auto* a=output+(12*64+8)*3;const auto* b=output+(12*64+56)*3;ok=ok&&a[2]>240&&a[0]<15&&b[0]>240&&b[2]<15;
 // Save a period, not only a still: another native pair followed by an AFW
 // pair arriving RIGHT first, with changed LEFT pixels and fallback metadata.
 Sleep(110);eye(ctx,0,left.Get(),4,false);
 // Model the production deferred finish: LEFT initially contains stock red,
 // then final DLSS/HUD green replaces the pending snapshot before RIGHT.
 for(unsigned i=0;i<32*24;++i){pixels[i*4]=pixels[i*4+2]=0;pixels[i*4+1]=255;}
 ctx->UpdateSubresource(left.Get(),0,nullptr,pixels,32*4,0);replace_left(ctx,left.Get(),4);
 eye(ctx,1,right.Get(),4,false);ctx->Flush();
 Sleep(110);poll(ctx);for(unsigned i=0;i<32*24;++i){pixels[i*4]=pixels[i*4+2]=0;pixels[i*4+1]=255;}
 ctx->UpdateSubresource(left.Get(),0,nullptr,pixels,32*4,0);eye(ctx,1,right.Get(),5,true,false);eye(ctx,0,left.Get(),5,true,false);ctx->Flush();
 const auto end=GetTickCount64()+3000;while(GetTickCount64()<end){poll(ctx);if(state().frames>=3)break;Sleep(1);}
 std::vector<std::shared_ptr<Encoded>> clip;{std::lock_guard lock(state().mutex);for(const auto& frame:state().history)clip.push_back(frame);}
 ok=clip.size()==3&&clip[0]->meta.generation==3&&clip[1]->meta.generation==4&&clip[2]->meta.generation==5&&clip[2]->meta.afw&&!clip[2]->meta.warped&&clip[2]->meta.ms>clip[0]->meta.ms&&ok;
 if(ok){stream.Reset();decoder.Reset();image.Reset();convert.Reset();
  ok=SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromMemory(clip[1]->jpeg.data(),DWORD(clip[1]->jpeg.size())))&&SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))&&SUCCEEDED(decoder->GetFrame(0,&image))&&SUCCEEDED(factory->CreateFormatConverter(&convert))&&SUCCEEDED(convert->Initialize(image.Get(),GUID_WICPixelFormat24bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))&&SUCCEEDED(convert->CopyPixels(nullptr,64*3,sizeof(output),output));
  ok=ok&&a[1]>240&&a[0]<15&&a[2]<15&&b[0]>240&&b[2]<15;
  printf("Stereo history deferred finish: decoded final LEFT green (stock red replaced) / RIGHT blue %s\n",ok?"PASS":"FAIL");}
 if(ok){std::reverse(clip.begin(),clip.end());ok=export_clip(clip,GetTickCount64());
  wchar_t csvPath[MAX_PATH]{};swprintf_s(csvPath,L"%s\\frames.csv",state().lastDir);FILE* csv=_wfopen(csvPath,L"rt");
  if(!csv)ok=false;else{char line[256]{};fgets(line,sizeof(line),csv);long long prior=LLONG_MAX;unsigned count{};
   while(fgets(line,sizeof(line),csv)){const auto* comma=strchr(line,',');if(!comma){ok=false;break;}long long age=strtoll(comma+1,nullptr,10);ok=age<=prior&&ok;prior=age;++count;}
   fclose(csv);ok=count==3&&ok;}}
 printf("Stereo history GPU: matching pairs, mismatched generation rejected, async runtime-like NO-SRV readback, JPEG decoded LEFT red/RIGHT blue, multi-frame native+AFW replay export %s\n",ok?"PASS":"FAIL");
 CoUninitialize();reset_gpu();return ok;
}
}
