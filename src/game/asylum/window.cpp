// City window.cpp game-only import method; Asylum-owned logical client/mirror.
// The engine renders a full-size offscreen backbuffer; Windows sees a fitted
// visible window. Never spoof the mod, runtime or system DLL's client queries.
#include "window.h"
#include "core/ui/overlay.h"
#include "core/util/log.h"
#include "core/vr/openxr_runtime.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cmath>
namespace bvr::aa::window {
namespace {
unsigned renderW{},renderH{};HWND gameWindow{};WNDPROC oldProc{};
std::atomic<unsigned> actualW{},actualH{};
LPARAM fixtureSize{};
LRESULT CALLBACK fixture_proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_SIZE)fixtureSize=l;
 if(m==WM_WINDOWPOSCHANGING&&l){auto* p=reinterpret_cast<WINDOWPOS*>(l);if(!(p->flags&SWP_NOSIZE)){p->cx=2284;p->cy=2448;}}
 return DefWindowProcW(h,m,w,l);}
float scale=1;std::atomic<unsigned> queries{};wchar_t settings[MAX_PATH]{};
using Client=BOOL(WINAPI*)(HWND,LPRECT);Client clientOriginal=::GetClientRect;
using Create=HWND(WINAPI*)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
Create createOriginal=::CreateWindowExW;
using Pos=BOOL(WINAPI*)(HWND,HWND,int,int,int,int,UINT);Pos posOriginal=::SetWindowPos;
using Move=BOOL(WINAPI*)(HWND,int,int,int,int,BOOL);Move moveOriginal=::MoveWindow;
using Metrics=int(WINAPI*)(int);Metrics metricsOriginal=::GetSystemMetrics;
using Monitor=BOOL(WINAPI*)(HMONITOR,LPMONITORINFO);Monitor monitorOriginal=::GetMonitorInfoW;
using Params=BOOL(WINAPI*)(UINT,UINT,PVOID,UINT);Params paramsOriginal=::SystemParametersInfoW;
bool eligible(HWND h){
 if(!h||GetParent(h)||!(GetWindowLongW(h,GWL_STYLE)&WS_CAPTION))return false;
 wchar_t cls[128]{};GetClassNameW(h,cls,128);if(wcsstr(cls,L"Splash"))return false;
 DWORD pid{};GetWindowThreadProcessId(h,&pid);return pid==GetCurrentProcessId()&&
   reinterpret_cast<HMODULE>(GetClassLongPtrW(h,GCLP_HMODULE))==GetModuleHandleW(nullptr);
}
void fit(int& x,int& y,int& w,int& h,DWORD style,DWORD ex){
 RECT area{};::SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
 RECT border{0,0,0,0};AdjustWindowRectEx(&border,style,FALSE,ex);
 const int bw=border.right-border.left,bh=border.bottom-border.top;
 const float k=std::min(1.f,std::min(float(area.right-area.left-bw-32)/renderW,float(area.bottom-area.top-bh-32)/renderH));
 w=int(renderW*k)+bw;h=int(renderH*k)+bh;x=area.left+16;y=area.top+16;
}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
 // The engine changes placement after CreateWindowEx. Constrain its actual
 // window on the owning UI thread, regardless of which positioning API it uses.
 if(m==WM_WINDOWPOSCHANGING&&l){const auto result=CallWindowProcW(oldProc,h,m,w,l);auto* p=reinterpret_cast<WINDOWPOS*>(l);if(!(p->flags&SWP_NOSIZE)){
  int x=p->x,y=p->y,cx=p->cx,cy=p->cy;fit(x,y,cx,cy,DWORD(GetWindowLongW(h,GWL_STYLE)),DWORD(GetWindowLongW(h,GWL_EXSTYLE)));
  p->x=x;p->y=y;p->cx=cx;p->cy=cy;p->flags&=~SWP_NOMOVE;
 }return result;}
 if(m==WM_SIZE&&w!=SIZE_MINIMIZED)l=MAKELPARAM(renderW,renderH);
 return CallWindowProcW(oldProc,h,m,w,l);
}
BOOL WINAPI client(HWND h,LPRECT r){
 const BOOL ok=clientOriginal(h,r);if(ok&&r&&renderW&&(h==gameWindow||(!gameWindow&&eligible(h)))){
  r->right=r->left+renderW;r->bottom=r->top+renderH;++queries;
 }return ok;
}
BOOL WINAPI position(HWND h,HWND after,int x,int y,int w,int ht,UINT flags){
 if(renderW&&(h==gameWindow||(!gameWindow&&eligible(h)))){
  fit(x,y,w,ht,DWORD(GetWindowLongW(h,GWL_STYLE)),DWORD(GetWindowLongW(h,GWL_EXSTYLE)));flags&=~SWP_NOMOVE;
 }return posOriginal(h,after,x,y,w,ht,flags);
}
BOOL WINAPI move(HWND h,int x,int y,int w,int ht,BOOL repaint){
 if(renderW&&(h==gameWindow||(!gameWindow&&eligible(h))))fit(x,y,w,ht,DWORD(GetWindowLongW(h,GWL_STYLE)),DWORD(GetWindowLongW(h,GWL_EXSTYLE)));
 return moveOriginal(h,x,y,w,ht,repaint);
}
HWND WINAPI create(DWORD ex,LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE inst,LPVOID p){
 const bool splash=reinterpret_cast<uintptr_t>(cls)>0xffff&&wcsstr(cls,L"Splash");
 const bool main=renderW&&!parent&&(style&WS_CAPTION)&&inst==GetModuleHandleW(nullptr)&&title&&title[0]&&!splash;
 if(main)fit(x,y,w,h,style,ex);
 HWND hwnd=createOriginal(ex,cls,title,style,x,y,w,h,parent,menu,inst,p);if(main)bind(hwnd);return hwnd;
}
void grow(RECT& r){r.right=r.left+std::max(8192L,r.right-r.left);r.bottom=r.top+std::max(8192L,r.bottom-r.top);}
int WINAPI metrics(int n){switch(n){case SM_CXSCREEN:case SM_CYSCREEN:case SM_CXFULLSCREEN:case SM_CYFULLSCREEN:case SM_CXMAXTRACK:case SM_CYMAXTRACK:return 8192;default:return metricsOriginal(n);}}
BOOL WINAPI monitor(HMONITOR h,LPMONITORINFO p){const BOOL ok=monitorOriginal(h,p);if(ok&&p){grow(p->rcMonitor);grow(p->rcWork);}return ok;}
BOOL WINAPI params(UINT action,UINT n,PVOID p,UINT flags){const BOOL ok=paramsOriginal(action,n,p,flags);if(ok&&action==SPI_GETWORKAREA&&p)grow(*static_cast<RECT*>(p));return ok;}
void* patch(const char* fn,void* replacement){
 auto* b=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(b);
 auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(b+dos->e_lfanew);const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
 if(!dir.VirtualAddress)return nullptr;
 for(auto* d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(b+dir.VirtualAddress);d->Name;++d){
  if(_stricmp(reinterpret_cast<char*>(b+d->Name),"user32.dll")||!d->OriginalFirstThunk)continue;
  auto* names=reinterpret_cast<IMAGE_THUNK_DATA32*>(b+d->OriginalFirstThunk);auto* slots=reinterpret_cast<IMAGE_THUNK_DATA32*>(b+d->FirstThunk);
  for(;names->u1.AddressOfData;++names,++slots){if(IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))continue;
   auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(b+names->u1.AddressOfData);if(strcmp(reinterpret_cast<char*>(name->Name),fn))continue;
   DWORD old{};if(!VirtualProtect(&slots->u1.Function,4,PAGE_READWRITE,&old))return nullptr;
   auto* previous=reinterpret_cast<void*>(slots->u1.Function);InterlockedExchange(reinterpret_cast<LONG*>(&slots->u1.Function),LONG(replacement));
   DWORD ignored{};VirtualProtect(&slots->u1.Function,4,old,&ignored);return previous;
  }
 }return nullptr;
}
}
bool install(){
 swprintf_s(settings,L"%s\\aa_rendering.ini",log::data_dir());wchar_t value[32]{};
 GetPrivateProfileStringW(L"AsylumVR",L"ResolutionScale",L"1.0",value,32,settings);const float saved=float(wcstod(value,nullptr));scale=std::isfinite(saved)?std::clamp(saved,.6f,1.8f):1;
 const wchar_t* x=wcsstr(GetCommandLineW(),L"-AARenderX="),*y=wcsstr(GetCommandLineW(),L"-AARenderY=");
 if(!x||!y)return false;renderW=wcstoul(x+11,nullptr,10);renderH=wcstoul(y+11,nullptr,10);
 if(renderW<512||renderH<512||renderW>8192||renderH>8192){renderW=renderH=0;return false;}
 bool ok=true;
 #define PATCH(name,original,hook) {auto* p=patch(name,reinterpret_cast<void*>(&hook));if(p)original=reinterpret_cast<decltype(original)>(p);else ok=false;}
 PATCH("GetClientRect",clientOriginal,client);PATCH("CreateWindowExW",createOriginal,create);
 PATCH("SetWindowPos",posOriginal,position);PATCH("MoveWindow",moveOriginal,move);
 PATCH("GetMonitorInfoW",monitorOriginal,monitor);PATCH("SystemParametersInfoW",paramsOriginal,params);PATCH("GetSystemMetrics",metricsOriginal,metrics);
 #undef PATCH
 BVR_LOG("[aa window] game-only logical client %ux%u, fitted desktop mirror; imports %s",renderW,renderH,ok?"complete":"INCOMPLETE");return ok;
}
void bind(HWND h){
 if(!h||!renderW||gameWindow)return;gameWindow=h;
 oldProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&proc)));
 if(!oldProc){gameWindow=nullptr;return;}
 wchar_t cls[128]{};GetClassNameW(h,cls,128);BVR_LOG("[aa window] owned main window %p class %ls style %08lX",h,cls,GetWindowLongW(h,GWL_STYLE));
 int x{},y{},w{},ht{};fit(x,y,w,ht,DWORD(GetWindowLongW(h,GWL_STYLE)),DWORD(GetWindowLongW(h,GWL_EXSTYLE)));
 posOriginal(h,nullptr,x,y,w,ht,SWP_NOZORDER|SWP_NOACTIVATE);
 status();
}
bool save_scale(float v){if(!std::isfinite(v))return false;scale=std::clamp(v,.6f,1.8f);wchar_t text[32];swprintf_s(text,L"%.2f",scale);return WritePrivateProfileStringW(L"AsylumVR",L"ResolutionScale",text,settings)!=FALSE;}
void draw_ui(){
 uint32_t w{},h{};vr::recommended_eye_size(&w,&h);ImGui::Text("Game render %ux%u / headset %ux%u",actualW.load(),actualH.load(),w,h);
 if(actualW&&actualH&&(actualW!=renderW||actualH!=renderH))ImGui::TextWrapped("Game size differs from the launch request. Save a capture for diagnosis.");
 float v=scale;if(overlay::SliderStep("Resolution scale",&v,.6f,1.8f))save_scale(v);
 ImGui::Text("Next output resolution: %ux%u",unsigned(w*scale)&~1u,unsigned(h*scale)&~1u);
 ImGui::TextWrapped("1.00x targets the headset recommendation. DLSS lowers the game's input size; compatibility adds lens margins. Higher is sharper and slower. Applies next launch.");
}
void observe_source(unsigned w,unsigned h){actualW=w;actualH=h;}
void observe_crop(float v){static float last{};if(std::isfinite(v)&&v>=.4f&&v<=1.f&&fabs(v-last)>.005f&&settings[0]){
 last=v;wchar_t text[24];swprintf_s(text,L"%.6f",v);WritePrivateProfileStringW(L"AsylumVR",L"LensCrop",text,settings);
 BVR_LOG("[aa window] measured lens crop %.6f for next-launch DLSS input",v);
}}
void status(){RECT real{};if(gameWindow)::GetClientRect(gameWindow,&real);BVR_LOG("[aa window] logical %ux%u actual game source %ux%u desktop client %ldx%ld; game client queries %u",renderW,renderH,actualW.load(),actualH.load(),real.right-real.left,real.bottom-real.top,queries.load());}
bool fixture(){
 // Exercise the actual logical query / physical positioning functions on an
 // owned harness window. Never patch a foreign executable's import table.
 const auto oldW=renderW,oldH=renderH;const HWND oldWindow=gameWindow;const auto savedProc=oldProc;renderW=2284;renderH=2448;gameWindow=nullptr;
 HWND h=create(0,L"STATIC",L"Asylum render-size fixture",WS_OVERLAPPEDWINDOW,16,16,512,512,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 if(!h){renderW=oldW;renderH=oldH;gameWindow=oldWindow;oldProc=savedProc;return false;}gameWindow=h;oldProc=fixture_proc;
 SendMessageW(h,WM_SIZE,SIZE_RESTORED,MAKELPARAM(33,44));
 const bool moved=position(h,nullptr,0,0,2284,2448,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
 const bool direct=::SetWindowPos(h,nullptr,0,0,2284,2448,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
 RECT real{},logical{},work{};::GetClientRect(h,&real);client(h,&logical);::SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
 const bool ok=moved&&direct&&LOWORD(fixtureSize)==2284&&HIWORD(fixtureSize)==2448&&logical.right==2284&&logical.bottom==2448&&real.right<2284&&real.bottom<2448&&real.right>0&&real.bottom>0&&real.bottom<work.bottom-work.top;
 DestroyWindow(h);gameWindow=oldWindow;oldProc=savedProc;renderW=oldW;renderH=oldH;return ok;
}
}
