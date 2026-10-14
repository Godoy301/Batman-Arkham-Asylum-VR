// aa_dlss64.exe - Asylum's 64-bit DLSS helper, adapted from City. Asylum is 32-bit and NVIDIA's DLSS library
// is 64-bit only, so the eye pictures come here through shared textures. Started by the game (batman_asylum.dll),
// ends with it. D0: copy input -> output. D1: one DLSS Super Resolution feature per eye (NGX), after AKN's dlss.cpp
// (akvr_native/src/dlss.cpp; method from Luma's Arkham Knight mod - Luma Framework, Filippo Tarpini / Garamond,
// "Custom MIT": reuse with credit).

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdarg>
#include <cstdio>

#include "dlss_shared.h"
#include "gpu_finish.h"
#include <psapi.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"
#include <nvapi.h>

namespace {

FILE* g_log = nullptr;
std::mutex g_logMutex;
void logf(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
}

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
wchar_t g_dir[MAX_PATH] = L"";
wchar_t g_libraryDir[MAX_PATH] = L"";
std::atomic<int> g_driverPreset{};
void read_override(){
    // Official NVAPI GetNGXOverrideState, process-local observation only.
    // Public SDK query IDs: nvapi_interface.h. Never write DRS or feedback.
    using Query=void*(__cdecl*)(NvU32);using Init=int(__cdecl*)();
    using Get=int(__cdecl*)(NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS*);
    static HMODULE module=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static Query query=module?reinterpret_cast<Query>(GetProcAddress(module,"nvapi_QueryInterface")):nullptr;
    static Init init=query?reinterpret_cast<Init>(query(0x0150e828)):nullptr;
    static Get get=query?reinterpret_cast<Get>(query(0x3fd96fba)):nullptr;
    static bool ready=init&&init()==0;
    if(!ready||!get)return;
    NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS state{};state.version=NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS_VER;state.processIdentifier=GetCurrentProcessId();
    const int status=get(&state);
    if(status==0)g_driverPreset=(state.feedbackMaskSR&NV_NGX_DLSS_OVERRIDE_FLAG_PRESET)?int(state.renderPreset):0;
    logf("NVIDIA override read-only own PID: status=%d SRflags=%llx renderPreset=%u appliedPreset=%d",status,state.feedbackMaskSR,state.renderPreset,g_driverPreset.load());
}
void log_library(){
 HMODULE modules[512]{};DWORD size{};
 if(!EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&size))return;
 for(unsigned i=0;i<std::min<unsigned>(512,size/sizeof(HMODULE));++i){wchar_t path[MAX_PATH]{};GetModuleFileNameW(modules[i],path,MAX_PATH);auto* name=wcsrchr(path,L'\\');if(!name||!wcsstr(name,L"nvngx_dlss"))continue;
  DWORD ignored{},n=GetFileVersionInfoSizeW(path,&ignored);std::vector<unsigned char> bytes(n);VS_FIXEDFILEINFO* info{};UINT len{};
  if(n&&GetFileVersionInfoW(path,0,n,bytes.data())&&VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&len))logf("actual DLSS library %ls version %u.%u.%u.%u",path,HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS));
 }
}

struct Set {
    ID3D11Texture2D* tex[bcdlss::kTexCount] = {};
    IDXGIKeyedMutex* km[bcdlss::kTexCount] = {};
};
Set g_set[2];
uint32_t g_gen = 0xFFFFFFFF;

// --- DLSS (NGX) ------------------------------------------------------------------------------------------
bool g_ngxTried = false, g_ngxOk = false;
NVSDK_NGX_Parameter* g_caps = nullptr;
struct Feature {
    NVSDK_NGX_Handle* h = nullptr;
    NVSDK_NGX_Parameter* p = nullptr;
    UINT rw = 0, rh = 0, ow = 0, oh = 0;
    int mode = -1, depthInv = -1, preset = -1;
    bool reset = true, failed = false;
    long evals = 0;
};
Feature g_feat[2];

// Evaluate cost on the graphics card (timestamp queries, read a few frames later).
struct Tq { ID3D11Query* dj = nullptr; ID3D11Query* a = nullptr; ID3D11Query* b = nullptr; bool pending = false; };
Tq g_tq[8];
int g_tqI = 0;
double g_tqSum = 0;
long g_tqN = 0;

void tq_collect() {
    for (Tq& q : g_tq) {
        if (!q.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 a = 0, b = 0;
        if (g_ctx->GetData(q.dj, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (g_ctx->GetData(q.a, &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            g_ctx->GetData(q.b, &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        q.pending = false;
        if (!dj.Disjoint && dj.Frequency && b > a) {
            g_tqSum += double(b - a) * 1000.0 / double(dj.Frequency);
            ++g_tqN;
        }
    }
}
Tq* tq_begin() {
    tq_collect();
    Tq& q = g_tq[g_tqI++ % 8];
    if (q.pending) return nullptr;
    if (!q.dj) {
        D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        g_dev->CreateQuery(&d, &q.dj);
        d.Query = D3D11_QUERY_TIMESTAMP;
        g_dev->CreateQuery(&d, &q.a);
        g_dev->CreateQuery(&d, &q.b);
    }
    if (!q.dj || !q.a || !q.b) return nullptr;
    g_ctx->Begin(q.dj);
    g_ctx->End(q.a);
    return &q;
}
void tq_end(Tq* q) {
    if (!q) return;
    g_ctx->End(q->b);
    g_ctx->End(q->dj);
    q->pending = true;
}

bool ngx_init() {
    if (g_ngxTried) return g_ngxOk;
    g_ngxTried = true;
    const wchar_t* paths[1] = {g_libraryDir};
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    info.LoggingInfo.LoggingCallback=[](const char* message,NVSDK_NGX_Logging_Level,NVSDK_NGX_Feature){
        constexpr char marker[]="NGX_OVERRIDE_RENDER_PRESET_SELECTION from DRS, value = ";
        if(auto* p=strstr(message,marker))g_driverPreset=atoi(p+sizeof(marker)-1);
        // Preserve provider-selected library and overrides, without per-frame
        // verbose file flushes in the game. Benchmarks retain the full trace.
        if(wcsstr(GetCommandLineW(),L"--benchmark")||strstr(message,"ReadValuesFromDRS")||strstr(message,"NGXInitLog")||strstr(message,"feature dlss snippet:")||strstr(message,"NGXLoadLibrary"))logf("NGX: %s",message);
    };
    info.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_ON;
    info.LoggingInfo.DisableOtherLoggingSinks=true;
    // our own project id (unregistered with NVIDIA, as Luma / AKN)
    NVSDK_NGX_Result r = NVSDK_NGX_D3D11_Init_with_ProjectID("8f3a1c62-4e9b-4d27-b1c5-6a2e9d4f0b31", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                                                             "1.0", g_dir, g_dev, &info);
    if (NVSDK_NGX_FAILED(r)) {
        logf("NGX init FAILED 0x%08x (nvngx_dlss.dll in %ls?)", unsigned(r), g_dir);
        return false;
    }
    r = NVSDK_NGX_D3D11_GetCapabilityParameters(&g_caps);
    int avail = 0;
    if (g_caps) g_caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &avail);
    g_ngxOk = NVSDK_NGX_SUCCEED(r) && avail;
    logf("NGX %s (super resolution available %d)", g_ngxOk ? "ready" : "NOT usable", avail);
    return g_ngxOk;
}

bool make_feature(Feature& f, const bcdlss::Shared* sh, int m) {
    if (f.h) { NVSDK_NGX_D3D11_ReleaseFeature(f.h); f.h = nullptr; }
    if (!f.p && NVSDK_NGX_FAILED(NVSDK_NGX_D3D11_AllocateParameters(&f.p))) return false;
    const int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure |
                      (sh->depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0);
    if (sh->preset > 0) {
        const char* const keys[] = {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance};
        for (const char* k : keys) NVSDK_NGX_Parameter_SetUI(f.p, k, unsigned(sh->preset));
    }
    NVSDK_NGX_DLSS_Create_Params cp{};
    cp.Feature.InWidth = sh->renderW;
    cp.Feature.InHeight = sh->renderH;
    cp.Feature.InTargetWidth = sh->outW;
    cp.Feature.InTargetHeight = sh->outH;
    cp.Feature.InPerfQualityValue = m == 1 ? NVSDK_NGX_PerfQuality_Value_DLAA
                                  : m == 2 ? NVSDK_NGX_PerfQuality_Value_UltraQuality
                                  : m == 3 ? NVSDK_NGX_PerfQuality_Value_MaxQuality
                                  : m == 4 ? NVSDK_NGX_PerfQuality_Value_Balanced
                                           : NVSDK_NGX_PerfQuality_Value_MaxPerf;
    cp.InFeatureCreateFlags = flags;
    NVSDK_NGX_Result r = NGX_D3D11_CREATE_DLSS_EXT(g_ctx, &f.h, f.p, &cp);
    log_library();
    read_override();
    // A rejected Quality/DLAA request must not silently become Balanced.
    logf("DLSS feature %ux%u -> %ux%u, mode %d, flags 0x%x, preset %d: %s (0x%08x)", sh->renderW, sh->renderH, sh->outW,
         sh->outH, m, flags, sh->preset, NVSDK_NGX_SUCCEED(r) ? "created" : "FAILED", unsigned(r));
    f.rw = sh->renderW; f.rh = sh->renderH; f.ow = sh->outW; f.oh = sh->outH;
    f.mode = m; f.depthInv = sh->depthInverted; f.preset = sh->preset;
    f.reset = true;
    f.failed = !(NVSDK_NGX_SUCCEED(r) && f.h);
    return !f.failed;
}

// --- shared textures -------------------------------------------------------------------------------------
void release_set(Set& s) {
    for (int i = 0; i < bcdlss::kTexCount; ++i) {
        if (s.km[i]) { s.km[i]->Release(); s.km[i] = nullptr; }
        if (s.tex[i]) { s.tex[i]->Release(); s.tex[i] = nullptr; }
    }
}

bool open_sets(const bcdlss::Shared* sh) {
    for (int e = 0; e < 2; ++e) {
        release_set(g_set[e]);
        for (int i = 0; i < bcdlss::kTexCount; ++i) {
            const uint64_t h = sh->handle[e][i];
            if (!h) continue;
            ID3D11Texture2D* t = nullptr;
            HRESULT hr = g_dev->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(h)), IID_PPV_ARGS(&t));
            if (FAILED(hr) || !t) {
                logf("open shared texture eye %d slot %d handle %llx FAILED 0x%08X", e, i, (unsigned long long)h, hr);
                return false;
            }
            g_set[e].tex[i] = t;
            t->QueryInterface(IID_PPV_ARGS(&g_set[e].km[i]));
        }
    }
    logf("texture sets opened (gen %u): render %ux%u -> out %ux%u", sh->gen, sh->renderW, sh->renderH, sh->outW, sh->outH);
    return true;
}

bool make_device(const bcdlss::Shared* sh) {
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return false;
    IDXGIAdapter1* chosen = nullptr;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* a = nullptr;
        if (f->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (d.AdapterLuid.LowPart == sh->luidLow && d.AdapterLuid.HighPart == sh->luidHigh) {
            chosen = a;
            logf("adapter: %ls", d.Description);
            break;
        }
        a->Release();
    }
    f->Release();
    if (!chosen) {
        logf("game adapter LUID not found; refusing a different GPU");
        return false;
    }
    const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const HRESULT hr = D3D11CreateDevice(chosen, chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                         fl, 2, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx);
    if (chosen) chosen->Release();
    if (FAILED(hr)) {
        logf("D3D11CreateDevice FAILED 0x%08X", hr);
        return false;
    }
    return true;
}

// One eye: copy (D0) or DLSS (D1). The game handed every slot over (key 1).
double g_tAcq = 0, g_tEval = 0;
long g_tN = 0;
bcdlss::GpuFinish g_finish;
bool g_finishLogged = false;
// One request's own numbers (the single legacy request, or an OVERLAP eye slot).
struct Req {
    float jx, jy;
    uint32_t reset;
    int32_t dlssMode;
    bool overlap; // the game waits for fenceOut instead of our finish-first wait
};
int serve(bcdlss::Shared* sh, int e, const Req& rq) {
    sh->evaluatedMode=0;
    if(rq.overlap) sh->req[e & 1].evaluatedMode=0;
    Set& s = g_set[e & 1];
    LARGE_INTEGER qf, ta, tb, tc;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&ta);
    IDXGIKeyedMutex* km[bcdlss::kTexCount] = {};
    int held[bcdlss::kTexCount] = {};
    int status = 0;
    for (int i = 0; i < bcdlss::kTexCount; ++i) {
        if (!s.tex[i]) continue;
        km[i] = s.km[i];
        if (!km[i] || km[i]->AcquireSync(1, 100) != S_OK) { status = -2; break; }
        held[i] = 1;
    }
    if (status == 0 && (!s.tex[bcdlss::kColor] || !s.tex[bcdlss::kOut])) status = -4;
    QueryPerformanceCounter(&tb);
    if (status == 0) {
        const bool dlss = rq.dlssMode > 0 && s.tex[bcdlss::kDepth] && s.tex[bcdlss::kMotion] && ngx_init();
        if (!dlss) {
            if (sh->renderW == sh->outW && sh->renderH == sh->outH) g_ctx->CopyResource(s.tex[bcdlss::kOut], s.tex[bcdlss::kColor]);
            else status = -5;
        } else {
            Feature& f = g_feat[e & 1];
            if (!f.h || f.rw != sh->renderW || f.rh != sh->renderH || f.ow != sh->outW || f.oh != sh->outH ||
                f.mode != rq.dlssMode || f.depthInv != sh->depthInverted || f.preset != sh->preset)
                make_feature(f, sh, rq.dlssMode);
            if (f.failed || !f.h) {
                status = -6;
            } else {
                NVSDK_NGX_D3D11_DLSS_Eval_Params ep{};
                ep.Feature.pInColor = s.tex[bcdlss::kColor];
                ep.Feature.pInOutput = s.tex[bcdlss::kOut];
                ep.pInDepth = s.tex[bcdlss::kDepth];
                ep.pInMotionVectors = s.tex[bcdlss::kMotion];
                ep.InJitterOffsetX = rq.jx;
                ep.InJitterOffsetY = rq.jy;
                ep.InRenderSubrectDimensions.Width = f.rw;
                ep.InRenderSubrectDimensions.Height = f.rh;
                ep.InReset = (f.reset || rq.reset) ? 1 : 0;
                f.reset = false;
                ep.InMVScaleX = sh->mvScaleX;
                ep.InMVScaleY = sh->mvScaleY;
                Tq* q = tq_begin();
                NVSDK_NGX_Result r = NGX_D3D11_EVALUATE_DLSS_EXT(g_ctx, f.h, f.p, &ep);
                for (int k = 0; k < sh->evalRepeat && k < 4 && NVSDK_NGX_SUCCEED(r); ++k)
                    r = NGX_D3D11_EVALUATE_DLSS_EXT(g_ctx, f.h, f.p, &ep);
                tq_end(q);
                if (NVSDK_NGX_FAILED(r)) {
                    status = -7;
                    if ((f.evals++ % 300) == 0) logf("eye %d evaluate FAILED 0x%08x", e, unsigned(r));
                } else if (++f.evals == 1) {
                    logf("eye %d: first DLSS evaluate", e);
                }
                if(rq.overlap && NVSDK_NGX_SUCCEED(r)) sh->req[e & 1].evaluatedMode=rq.dlssMode;
                if(!rq.overlap && NVSDK_NGX_SUCCEED(r)) sh->evaluatedMode=rq.dlssMode;
                if (g_tqN >= 50) {
                    sh->gpuMs = float(g_tqSum / g_tqN);
                    g_tqSum = 0;
                    g_tqN = 0;
                }
            }
        }
    }
    QueryPerformanceCounter(&tc);
    g_tAcq += double(tb.QuadPart - ta.QuadPart) * 1000.0 / double(qf.QuadPart);
    g_tEval += double(tc.QuadPart - tb.QuadPart) * 1000.0 / double(qf.QuadPart);
    if (++g_tN == 120) {
        logf("timing over 120 requests: waiting for the game's pictures %.3f ms, copy/DLSS call %.3f ms (CPU), DLSS on the card %.2f ms, status %d, sync %d (finish wait %.2f ms, %u timeouts)",
             g_tAcq / 120.0, g_tEval / 120.0, sh->gpuMs, status, sh->syncMode, sh->finishMs, sh->finishTimeouts);
        g_tAcq = g_tEval = 0;
        g_tN = 0;
    }
    for (int i = 0; i < bcdlss::kTexCount; ++i)
        if (held[i]) km[i]->ReleaseSync(0);
    // FINISHFIRST: answer only once the DLSS (and the release above) has run on the card, so the game's wait for
    // the output is already satisfied and its context never stalls on ours.
    if (sh->syncMode >= 1 && !rq.overlap) {
        LARGE_INTEGER td;
        QueryPerformanceCounter(&td);
        if (!g_finish.wait(g_dev, g_ctx, 1000)) {++sh->finishTimeouts;status=-11;sh->evaluatedMode=0;}
        LARGE_INTEGER te;
        QueryPerformanceCounter(&te);
        sh->finishMs = float(double(te.QuadPart - td.QuadPart) * 1000.0 / double(qf.QuadPart));
        if (!g_finishLogged) {
            g_finishLogged = true;
            logf("finish-first hand-off (sync %d) with a %s", sh->syncMode, g_finish.kind());
        }
    } else {
        sh->finishMs = 0.0f;
    }
    sh->ngxState = g_ngxTried ? (g_ngxOk ? 1 : -1) : 0;
    sh->driverPreset=g_driverPreset;
    return status;
}

} // namespace

int benchmark(){
 bcdlss::Shared sh{};IDXGIFactory1* factory{};if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 20;IDXGIAdapter1* adapter{};
 for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de){sh.luidLow=d.AdapterLuid.LowPart;sh.luidHigh=d.AdapterLuid.HighPart;adapter->Release();adapter=nullptr;break;}adapter->Release();adapter=nullptr;}factory->Release();if(!make_device(&sh)||!ngx_init())return 21;
 const bool cnn=wcsstr(GetCommandLineW(),L"--cnn")!=nullptr;sh.preset=cnn?5:0;
 for(int mode:{4,3,1,2,5}){
  sh.outW=2284;sh.outH=2448;const float ratio=mode==1?1.f:mode==2?.77f:mode==3?2.f/3.f:mode==4?.58f:.5f;
  sh.renderW=mode==4?1322:(UINT(sh.outW*ratio)&~1u);sh.renderH=mode==4?1478:(UINT(sh.outH*ratio)&~1u);
  std::vector<ID3D11Texture2D*> textures(4,nullptr);bool allocated=true;
  for(unsigned t=0;t<4;++t){D3D11_TEXTURE2D_DESC d{};d.Width=t==3?sh.outW:sh.renderW;d.Height=t==3?sh.outH:sh.renderH;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=t==1?DXGI_FORMAT_R32_FLOAT:t==2?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS;
   allocated=allocated&&SUCCEEDED(g_dev->CreateTexture2D(&d,nullptr,&textures[t]));if(textures[t]){ID3D11RenderTargetView* rt{};if(SUCCEEDED(g_dev->CreateRenderTargetView(textures[t],nullptr,&rt))){const float value[4]={t==0?.25f:t==1?.5f:0.f,t==0?.5f:0.f,t==0?.75f:0.f,1.f};g_ctx->ClearRenderTargetView(rt,value);rt->Release();}}
  }
  if(!allocated)return 22;
  Feature f;bool made=make_feature(f,&sh,mode);double gpuTotal{},cpuTotal{};
  ID3D11Query *start{},*end{},*disjoint{};D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP,0};g_dev->CreateQuery(&q,&start);g_dev->CreateQuery(&q,&end);q.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;g_dev->CreateQuery(&q,&disjoint);if(!start||!end||!disjoint)return 23;
  bool evaluated=made;unsigned samples{};
  LARGE_INTEGER freq{};QueryPerformanceFrequency(&freq);
  for(unsigned i=0;i<160&&evaluated;++i){NVSDK_NGX_D3D11_DLSS_Eval_Params ep{};ep.Feature.pInColor=textures[0];ep.pInDepth=textures[1];ep.pInMotionVectors=textures[2];ep.Feature.pInOutput=textures[3];ep.InRenderSubrectDimensions.Width=sh.renderW;ep.InRenderSubrectDimensions.Height=sh.renderH;ep.InMVScaleX=ep.InMVScaleY=1;ep.InReset=i==0;
   if(i==40){if(!g_finish.wait(g_dev,g_ctx,5000)){evaluated=false;break;}g_ctx->Begin(disjoint);g_ctx->End(start);}
   LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);auto r=NGX_D3D11_EVALUATE_DLSS_EXT(g_ctx,f.h,f.p,&ep);QueryPerformanceCounter(&b);evaluated=NVSDK_NGX_SUCCEED(r);
   if(evaluated&&i>=40){cpuTotal+=1000.*(b.QuadPart-a.QuadPart)/freq.QuadPart;++samples;}
  }
  if(evaluated){g_ctx->End(end);g_ctx->End(disjoint);evaluated=g_finish.wait(g_dev,g_ctx,5000);D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};UINT64 x{},y{};
   HRESULT rd=g_ctx->GetData(disjoint,&data,sizeof(data),0),ra=g_ctx->GetData(start,&x,sizeof(x),0),rb=g_ctx->GetData(end,&y,sizeof(y),0);
   const auto until=GetTickCount64()+1000;while(evaluated&&(rd==S_FALSE||ra==S_FALSE||rb==S_FALSE)&&GetTickCount64()<until){SwitchToThread();rd=g_ctx->GetData(disjoint,&data,sizeof(data),0);ra=g_ctx->GetData(start,&x,sizeof(x),0);rb=g_ctx->GetData(end,&y,sizeof(y),0);}
   logf("BENCH timestamps finish=%u hr=%08x/%08x/%08x disjoint=%u frequency=%llu begin=%llu end=%llu",evaluated,unsigned(rd),unsigned(ra),unsigned(rb),data.Disjoint,data.Frequency,x,y);
   evaluated=evaluated&&rd==S_OK&&ra==S_OK&&rb==S_OK&&!data.Disjoint&&data.Frequency&&y>=x;
   if(evaluated)gpuTotal=1000.*(y-x)/data.Frequency;
  }
  logf("BENCH requestedLibrary=%s driverPreset=%d mode=%d input=%ux%u output=%ux%u samples=%u evaluateGPU=%.3f ms/eye CPU=%.3f ms/eye valid=%u (isolated NGX, no game/handoff/headset)",cnn?"CNN3.7.20":"current310.9.1",g_driverPreset.load(),mode,sh.renderW,sh.renderH,sh.outW,sh.outH,samples,samples?gpuTotal/samples:0,samples?cpuTotal/samples:0,evaluated);
  if(f.h)NVSDK_NGX_D3D11_ReleaseFeature(f.h);if(f.p)NVSDK_NGX_D3D11_DestroyParameters(f.p);for(auto* t:textures)if(t)t->Release();start->Release();end->Release();disjoint->Release();
  if(made&&!evaluated)return 24;
 }
 return 0;
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (n && n < MAX_PATH - 64) {
        wcscat_s(path,wcsstr(GetCommandLineW(),L"--benchmark")?(wcsstr(GetCommandLineW(),L"--cnn")?L"\\Batman_Asylum\\dlss_benchmark_cnn.log":L"\\Batman_Asylum\\dlss_benchmark_current.log"):L"\\Batman_Asylum\\dlss_helper.log");
        g_log = _wfopen(path, L"w");
    }
    GetModuleFileNameW(nullptr, g_dir, MAX_PATH);
    if (wchar_t* sl = wcsrchr(g_dir, L'\\')) *sl = 0; // nvngx_dlss.dll sits next to the helper
    wcscpy_s(g_libraryDir,g_dir);if(wcsstr(GetCommandLineW(),L"--cnn"))wcscat_s(g_libraryDir,L"\\CNN");
    logf("aa_dlss64 starting (pid %lu), folder %ls", GetCurrentProcessId(), g_dir);
    logf("requested DLSS library folder %ls",g_libraryDir);
    if(wcsstr(GetCommandLineW(),L"--benchmark"))return benchmark();

    wchar_t session[64]{},mapName[128]{},reqName[128]{},doneName[128]{};
    if(const auto* arg=wcsstr(GetCommandLineW(),L"--session ")){
        if(swscanf_s(arg+10,L"%63ls",session,unsigned(_countof(session)))!=1)return 1;
        for(const auto* c=session;*c;++c)if((*c<L'0'||*c>L'9')&&*c!=L'_')return 1;
    }
    if(session[0]){swprintf_s(mapName,L"%s_%s",bcdlss::kMapName,session);swprintf_s(reqName,L"%s_%s",bcdlss::kReqName,session);swprintf_s(doneName,L"%s_%s",bcdlss::kDoneName,session);}
    else{wcscpy_s(mapName,bcdlss::kMapName);wcscpy_s(reqName,bcdlss::kReqName);wcscpy_s(doneName,bcdlss::kDoneName);}
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapName);
    if (!map) { logf("no shared block - exiting"); return 1; }
    auto* sh = static_cast<bcdlss::Shared*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(bcdlss::Shared)));
    HANDLE req = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, reqName);
    HANDLE done = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, doneName);
    if (!sh || !req || !done || sh->version != bcdlss::kVersion) { logf("shared block / events missing or old version - exiting"); return 1; }
    HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, sh->gamePid);
    if (!make_device(sh)) return 1;
    sh->helperPid = GetCurrentProcessId();
    sh->ngxState=ngx_init()?1:-1;
    sh->driverPreset=g_driverPreset;
    MemoryBarrier();
    sh->helperReady = 1;
    logf("ready");

    LARGE_INTEGER qf;
    QueryPerformanceFrequency(&qf);
    uint32_t served = 0, ovServed = 0;
    // OVERLAP: the game's two shared fences, opened once it has published them.
    ID3D11Fence* fenceIn = nullptr;
    ID3D11Fence* fenceOut = nullptr;
    ID3D11DeviceContext4* c4 = nullptr;
    bool fencesTried = false;
    HANDLE fenceEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    uint32_t handled[2] = {0, 0};
    auto open_fences = [&]() -> bool {
        if (fenceIn && fenceOut && c4) return true;
        if (fencesTried || !sh->fenceInHandle || !sh->fenceOutHandle) return false;
        fencesTried = true;
        ID3D11Device5* d5 = nullptr;
        HRESULT a = E_NOINTERFACE, b = E_NOINTERFACE;
        if (SUCCEEDED(g_dev->QueryInterface(IID_PPV_ARGS(&d5))) && d5) {
            a = d5->OpenSharedFence(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(sh->fenceInHandle)), IID_PPV_ARGS(&fenceIn));
            b = d5->OpenSharedFence(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(sh->fenceOutHandle)), IID_PPV_ARGS(&fenceOut));
            d5->Release();
        }
        g_ctx->QueryInterface(IID_PPV_ARGS(&c4));
        logf("overlap fences: in 0x%08X, out 0x%08X, context4 %s", unsigned(a), unsigned(b), c4 ? "yes" : "NO");
        return fenceIn && fenceOut && c4;
    };
    for (;;) {
        const DWORD w = WaitForSingleObject(req, 1000);
        if (game && WaitForSingleObject(game, 0) == WAIT_OBJECT_0) { logf("game ended - exiting"); break; }
        if (w != WAIT_OBJECT_0) continue;
        // OVERLAP eye slots, oldest serial first (left before right).
        for (;;) {
            int e = -1;
            for (int k = 0; k < 2; ++k) {
                const uint32_t s = sh->req[k].serial;
                if (s != handled[k] && (e < 0 || s < sh->req[e].serial)) e = k;
            }
            if (e < 0) break;
            const uint32_t serial = sh->req[e].serial;
            handled[e] = serial;
            MemoryBarrier(); // the slot's numbers were written before its serial
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            // Fail closed if shared fences cannot be opened. Never place a foreign-context wait on the GPU.
            const bool fences = open_fences();
            int status = fences ? 0 : -10;
            if (sh->gen != g_gen) {
                g_gen = sh->gen;
                if (!open_sets(sh)) status = -1;
            }
            if (status == 0) {
                // Wait on the CPU until input copy/release has run. A timeout rejects the request before serve.
                const ULONGLONG deadline = GetTickCount64() + 100;
                while (fences && fenceIn->GetCompletedValue() < serial) {
                    const ULONGLONG now = GetTickCount64();
                    if (now >= deadline || FAILED(fenceIn->SetEventOnCompletion(serial, fenceEv))) { ++sh->fenceWaitTimeouts; status=-12; break; }
                    WaitForSingleObject(fenceEv, DWORD(deadline - now));
                }
                const Req rq{sh->req[e].jitterX, sh->req[e].jitterY, sh->req[e].reset, sh->req[e].dlssMode, true};
                if(status==0)status = serve(sh, e, rq); // never submit a foreign-context GPU wait after a fence timeout
            }
            QueryPerformanceCounter(&t1);
            sh->req[e].status = status;
            sh->req[e].helperMs = static_cast<float>((t1.QuadPart - t0.QuadPart) * 1000.0 / qf.QuadPart);
            MemoryBarrier();
            sh->req[e].answered = serial; // before the fence: the game reads it once the fence has passed
            if (c4 && fenceOut) {
                c4->Signal(fenceOut, serial); // after our release, so the game's next acquire is already satisfied
                g_ctx->Flush();
            }
            SetEvent(done);
            if (++ovServed == 1 || (status && (ovServed % 300) == 1))
                logf("overlap request %u eye %d status %d (%.3f ms)", serial, e, status, sh->req[e].helperMs);
        }
        // The one-at-a-time request (sync 0..2).
        if (sh->serial == sh->answered) continue;
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        int status = 0;
        if (sh->gen != g_gen) {
            g_gen = sh->gen;
            if (!open_sets(sh)) status = -1;
        }
        if (status == 0) {
            const Req rq{sh->jitterX, sh->jitterY, sh->reset, sh->dlssMode, false};
            status = serve(sh, sh->eye, rq);
        }
        QueryPerformanceCounter(&t1);
        sh->status = status;
        sh->helperMs = static_cast<float>((t1.QuadPart - t0.QuadPart) * 1000.0 / qf.QuadPart);
        MemoryBarrier();
        sh->answered = sh->serial;
        SetEvent(done);
        if (++served == 1 || (status && (served % 300) == 1))
            logf("request %u eye %d status %d (%.3f ms)", sh->serial, sh->eye, status, sh->helperMs);
    }
    if (c4) c4->Release();
    if (fenceIn) fenceIn->Release();
    if (fenceOut) fenceOut->Release();
    for (Feature& f : g_feat)
        if (f.h) NVSDK_NGX_D3D11_ReleaseFeature(f.h);
    if (g_ngxOk) NVSDK_NGX_D3D11_Shutdown1(g_dev);
    release_set(g_set[0]);
    release_set(g_set[1]);
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
    return 0;
}
