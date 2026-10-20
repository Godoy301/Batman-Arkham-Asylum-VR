// FIRSTPERSON for Asylum: Batman City's first-person mode (batman_city scene.cpp fp_update / fp_head_follow / fp_apply),
// re-found in Asylum by code shape (RE_NOTES "FIRSTPERSON", STATIC 2026-10-20 on the retail exe; same module as Origins'
// first_person.cpp except the offsets and the head-bone read):
//   APawn::GetPawnViewLocation 0x867C30 (City 0x7A6970, same instructions): Location +0x54, EyeHeight +0x284.
//   APawn::GetViewRotation 0x87B650 (City 0x7BA650, same instructions): Controller +0x1B4 (Rotation +0x60).
//   The game's own head-bone read 0x8A1178: FName ctor 0x5D5620 (L"Bip01_Head", 1, 1; ret 0xC) on pawn +0x348 (= Pawn.Mesh;
//   EyeHeight -> Mesh is City's 0xC4), then USkeletalMeshComponent::MatchRefBone 0x90E8E0 (thiscall FName; ret 8; -1 =
//   none; SkeletalMesh +0x1B0) and GetBoneMatrix 0x91D2A0 (thiscall FMatrix* out, index; ret 8): world position = row 3.
//   Asylum has no GetBoneLocation of City's shape.
// The player -> controller link (City +0x30) and the controller -> pawn field are found LIVE by the pawn's back-pointer.
#include "first_person.h"
#include "scene.h"
#include "core/ui/overlay.h"
#include "core/util/log.h"
#include <windows.h>
#include <imgui.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bvr::aa::first_person {
namespace {
using W = uint32_t;
constexpr W kPlayerScanBegin = 0x20, kPlayerScanEnd = 0x80;   // ULocalPlayer -> PlayerController (City +0x30)
constexpr W kPCScanBegin = 0x100, kPCScanEnd = 0x700;         // PlayerController -> Pawn
constexpr W kPawnController = 0x1B4, kPawnLocation = 0x54, kPawnEyeHeight = 0x284, kPawnMesh = 0x348, kMeshSkeleton = 0x1B0;
constexpr W kFNameCtor = 0x5D5620, kMatchRefBone = 0x90E8E0, kGetBoneMatrix = 0x91D2A0;
constexpr float kHeadCapUU = 140.0f; // ~1.5 m: anything further is not his head (City)
using FNameCtorFn = void*(__thiscall*)(void* name, const wchar_t* text, int findType, int unused);
using MatchRefBoneFn = int(__thiscall*)(void* mesh, W nameIndex, W nameNumber);
using GetBoneMatrixFn = float*(__thiscall*)(void* mesh, float* out, int bone);

uintptr_t base = 0, imageEnd = 0;
wchar_t settings[MAX_PATH] = {};
std::atomic<bool> enabled{false}, followHead{true};
std::atomic<float> forwardCm{25.0f}, upCm{0.0f}, headSmoothS{0.08f};
std::atomic<unsigned> frames{0}, headFrames{0}, headMisses{0};
W playerOff = 0, pawnOff = 0;               // found live
float eye[3] = {};
bool eyeValid = false;                      // this pair's eye point (game thread)
float headOff[3] = {};
bool headOffValid = false;
LARGE_INTEGER headLastT = {};
W headName[2] = {};
bool headNameReady = false;
const char* gateWhy = "";

bool rd(uintptr_t p, void* out, size_t n) {
    __try { memcpy(out, reinterpret_cast<const void*>(p), n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t word(uintptr_t p) { W v = 0; return rd(p, &v, 4) ? v : 0; }
bool object(uintptr_t p) { // a C++ object of the game (vtable in its image)
    if (p < 0x10000 || (p & 3)) return false;
    const uintptr_t vt = word(p);
    return vt >= base && vt < imageEnd;
}
bool finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
void gate(const char* why) {
    if (!why) why = "";
    if (why == gateWhy) return;
    gateWhy = why;
    BVR_LOG("[aa] first person %s%s", *why ? "paused: " : "", *why ? why : "active again");
}
uintptr_t pawn_of(uintptr_t pc, W off) {
    const uintptr_t q = word(pc + off);
    if (q == pc || !object(q) || word(q + kPawnController) != pc) return 0;
    float eh = 0;
    return rd(q + kPawnEyeHeight, &eh, 4) && std::isfinite(eh) && eh > -50.0f && eh < 300.0f ? q : 0;
}
uintptr_t find_pawn(uintptr_t player) {
    if (playerOff && pawnOff) {
        const uintptr_t pc = word(player + playerOff);
        if (object(pc)) if (const uintptr_t pawn = pawn_of(pc, pawnOff)) return pawn;
    }
    static ULONGLONG s_lastScan = 0; // no pawn (menus, some cutscenes): look again twice a second
    const ULONGLONG now = GetTickCount64();
    if (now - s_lastScan < 500) return 0;
    s_lastScan = now;
    for (W lo = kPlayerScanBegin; lo < kPlayerScanEnd; lo += 4) {
        const uintptr_t pc = word(player + lo);
        if (!object(pc)) continue;
        for (W po = kPCScanBegin; po < kPCScanEnd; po += 4)
            if (const uintptr_t pawn = pawn_of(pc, po)) {
                if (lo != playerOff || po != pawnOff)
                    BVR_LOG("[aa] first person: player +0x%X = controller %08X, its +0x%X = Batman (pawn %08X)", lo,
                            unsigned(pc), po, unsigned(pawn));
                playerOff = lo; pawnOff = po;
                return pawn;
            }
    }
    return 0;
}
bool head_name() {
    if (headNameReady) return true;
    __try { reinterpret_cast<FNameCtorFn>(base + kFNameCtor - 0x400000)(headName, L"Bip01_Head", 1, 1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    headNameReady = true;
    BVR_LOG("[aa] first person head: name Bip01_Head = %u/%u", headName[0], headName[1]);
    return true;
}
bool bone(uintptr_t mesh, float* out) { // as the game: the bone's index, then its world matrix
    __try {
        void* m = reinterpret_cast<void*>(mesh);
        const int index = reinterpret_cast<MatchRefBoneFn>(base + kMatchRefBone - 0x400000)(m, headName[0], headName[1]);
        if (index < 0) return true; // out stays zero = no such bone
        float matrix[16] = {};
        reinterpret_cast<GetBoneMatrixFn>(base + kGetBoneMatrix - 0x400000)(m, matrix, index);
        out[0] = matrix[12]; out[1] = matrix[13]; out[2] = matrix[14];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
// HEADCAM (City): the eye point follows his head bone, smoothed (real time, so both eyes of a pair share it) and capped.
void follow_head(uintptr_t pawn) {
    if (!followHead.load(std::memory_order_relaxed)) { headOffValid = false; return; }
    auto miss = [](const char* why) {
        headOffValid = false;
        if (headMisses.fetch_add(1, std::memory_order_relaxed) < 6) BVR_LOG("[aa] first person head: %s - steady eye point", why);
    };
    const uintptr_t mesh = word(pawn + kPawnMesh);
    if (!object(mesh)) return miss("no mesh at pawn +0x348");
    if (!object(word(mesh + kMeshSkeleton))) return miss("mesh has no skeleton");
    if (!head_name()) return miss("Bip01_Head name failed");
    float head[3] = {};
    if (!bone(mesh, head)) return miss("bone read faulted");
    if (!finite3(head) || (head[0] == 0.0f && head[1] == 0.0f && head[2] == 0.0f)) return miss("no Bip01_Head bone");
    const float d[3] = {head[0] - eye[0], head[1] - eye[1], head[2] - eye[2]};
    if (fabsf(d[0]) > kHeadCapUU || fabsf(d[1]) > kHeadCapUU || fabsf(d[2]) > kHeadCapUU) {
        char b[96];
        snprintf(b, sizeof(b), "head %.0f, %.0f, %.0f UU from the eye point (cap %.0f)", d[0], d[1], d[2], kHeadCapUU);
        return miss(b);
    }
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    const float dt = headLastT.QuadPart ? float(double(now.QuadPart - headLastT.QuadPart) / double(f.QuadPart)) : 0.0f;
    headLastT = now;
    const float tau = headSmoothS.load(std::memory_order_relaxed);
    const float a = !headOffValid || tau <= 0.001f ? 1.0f : 1.0f - expf(-(dt > 0.1f ? 0.1f : dt) / tau);
    for (int i = 0; i < 3; ++i) {
        headOff[i] += (d[i] - headOff[i]) * a;
        eye[i] += headOff[i];
    }
    headOffValid = true;
    if (headFrames.fetch_add(1, std::memory_order_relaxed) == 0)
        BVR_LOG("[aa] first person head: mesh %08X, Bip01_Head - eye point = %.1f, %.1f, %.1f UU", unsigned(mesh), d[0], d[1], d[2]);
}
void save() {
    if (!settings[0]) return;
    wchar_t v[32];
    WritePrivateProfileStringW(L"AsylumVR", L"FirstPerson", enabled.load() ? L"1" : L"0", settings);
    swprintf_s(v, L"%.1f", forwardCm.load()); WritePrivateProfileStringW(L"AsylumVR", L"FirstPersonForwardCm", v, settings);
    swprintf_s(v, L"%.1f", upCm.load()); WritePrivateProfileStringW(L"AsylumVR", L"FirstPersonUpCm", v, settings);
    WritePrivateProfileStringW(L"AsylumVR", L"FirstPersonHead", followHead.load() ? L"1" : L"0", settings);
    swprintf_s(v, L"%.3f", headSmoothS.load()); WritePrivateProfileStringW(L"AsylumVR", L"FirstPersonHeadSmooth", v, settings);
}
float get(const wchar_t* key, float fallback, float lo, float hi) {
    wchar_t t[32] = {};
    GetPrivateProfileStringW(L"AsylumVR", key, L"", t, 32, settings);
    if (!t[0]) return fallback;
    const float v = float(wcstod(t, nullptr));
    return std::isfinite(v) && v >= lo && v <= hi ? v : fallback;
}
} // namespace

void install(uintptr_t imageBase, const wchar_t* settingsPath) {
    base = imageBase;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(imageBase + dos->e_lfanew);
    imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;
    wcscpy_s(settings, settingsPath);
    enabled.store(GetPrivateProfileIntW(L"AsylumVR", L"FirstPerson", 0, settings) != 0);
    forwardCm.store(get(L"FirstPersonForwardCm", 25.0f, -20.0f, 80.0f));
    upCm.store(get(L"FirstPersonUpCm", 0.0f, -40.0f, 40.0f));
    followHead.store(GetPrivateProfileIntW(L"AsylumVR", L"FirstPersonHead", 1, settings) != 0);
    headSmoothS.store(get(L"FirstPersonHeadSmooth", 0.08f, 0.0f, 0.5f));
    overlay::set_chord_double_tap(&toggle);
    BVR_LOG("[aa] first person %s (both sticks clicked twice quickly toggles it)", enabled.load() ? "ON" : "off");
}

void update(void* localPlayer, const char* blocked) {
    eyeValid = false;
    if (!enabled.load(std::memory_order_relaxed) || !localPlayer) return;
    // FPGATE (City, JJ 2026-10-11: "must always be disabled when going into main menu ... or any other loading screen and
    // menus"): the setting stays on, the game's own camera is used while a menu is up.
    if (blocked) { gate(blocked); return; }
    const uintptr_t pawn = find_pawn(reinterpret_cast<uintptr_t>(localPlayer));
    if (!pawn) { gate("no Batman to follow (menu or cutscene)"); return; }
    float loc[3] = {}, eh = 0;
    if (!rd(pawn + kPawnLocation, loc, 12) || !rd(pawn + kPawnEyeHeight, &eh, 4) || !finite3(loc) || !std::isfinite(eh)) return;
    eye[0] = loc[0]; eye[1] = loc[1]; eye[2] = loc[2] + eh; // APawn::GetPawnViewLocation
    follow_head(pawn);
    eyeValid = true;
    static bool s_logged = false;
    if (!s_logged) { s_logged = true; BVR_LOG("[aa] first person: eye (%.1f, %.1f, %.1f) = location + eye height %.1f", eye[0], eye[1], eye[2], eh); }
}

// The view matrix: row vectors, v_view = (v - origin) * R, translation row = -origin * R; columns of R: right, up, forward.
void apply(float view[4][4], float unitsPerMetre) {
    if (!eyeValid) return;
    float o[3];
    for (int i = 0; i < 3; ++i) o[i] = -(view[3][0] * view[i][0] + view[3][1] * view[i][1] + view[3][2] * view[i][2]);
    const float dx = eye[0] - o[0], dy = eye[1] - o[1], dz = eye[2] - o[2];
    // FPGATE (City): the main menu has a stand-in pawn far from the menu scene's camera; third person sits ~2-4 m behind him.
    if (!(sqrtf(dx * dx + dy * dy + dz * dz) < 12.0f * unitsPerMetre)) { gate("game camera far from Batman"); return; }
    gate(nullptr);
    float fx = view[0][2], fy = view[1][2];
    const float len = sqrtf(fx * fx + fy * fy);
    if (len > 1e-3f) { fx /= len; fy /= len; } else { fx = fy = 0.0f; } // straight down / up: no forward push
    const float cm = unitsPerMetre * 0.01f, fwd = forwardCm.load(std::memory_order_relaxed) * cm;
    const float n[3] = {eye[0] + fx * fwd, eye[1] + fy * fwd, eye[2] + upCm.load(std::memory_order_relaxed) * cm};
    for (int j = 0; j < 3; ++j) view[3][j] = -(n[0] * view[0][j] + n[1] * view[1][j] + n[2] * view[2][j]);
    frames.fetch_add(1, std::memory_order_relaxed);
}

bool on() { return enabled.load(); }
void set_on(bool v) {
    if (enabled.exchange(v) != v) { BVR_LOG("[aa] first person %s", v ? "ON" : "off"); save(); }
}
void toggle() { set_on(!enabled.load()); } // both sticks clicked twice quickly (overlay thread)

void draw_ui() { // at the top of the panel: it changes the whole view (City)
    bool fp = enabled.load();
    if (ImGui::Checkbox("FIRST PERSON  (or click both sticks twice quickly)", &fp)) set_on(fp);
    if (!fp) return;
    float f = forwardCm.load(), u = upCm.load();
    if (overlay::SliderStep("   camera in front of his face (cm)", &f, -20.0f, 80.0f, "%.0f")) { forwardCm.store(f); save(); }
    if (overlay::SliderStep("   camera up / down (cm)", &u, -40.0f, 40.0f, "%.0f")) { upCm.store(u); save(); }
    bool hf = followHead.load();
    if (ImGui::Checkbox("   camera follows his head (gliding, animations)", &hf)) { followHead.store(hf); save(); }
    if (hf) {
        float sm = headSmoothS.load();
        if (overlay::SliderStep("   head smoothing (s) - higher = steadier, lags more", &sm, 0.0f, 0.5f, "%.2f")) { headSmoothS.store(sm); save(); }
    }
    if (scene::test_options())
        ImGui::TextDisabled("      frames %u, head frames %u, steady eye point used %u%s%s", frames.load(), headFrames.load(),
                            headMisses.load(), *gateWhy ? ", paused: " : "", gateWhy);
}

bool command(const char* a) {
    if (!strcmp(a, "on")) set_on(true);
    else if (!strcmp(a, "off")) set_on(false);
    else if (!strncmp(a, "fwd ", 4)) { forwardCm.store(float(atof(a + 4))); save(); }
    else if (!strncmp(a, "up ", 3)) { upCm.store(float(atof(a + 3))); save(); }
    else if (!strncmp(a, "head ", 5)) { followHead.store(atoi(a + 5) != 0); save(); }
    else if (!strncmp(a, "smooth ", 7)) { headSmoothS.store(float(atof(a + 7))); save(); }
    else if (strcmp(a, "status")) return false;
    BVR_LOG("[aa] first person %s: forward %.0f cm, up %.0f cm, head %d (smoothing %.2f s) | frames %u, head frames %u, "
            "steady eye point %u, player +0x%X, pawn field +0x%X%s%s", enabled.load() ? "ON" : "off", forwardCm.load(),
            upCm.load(), followHead.load() ? 1 : 0, headSmoothS.load(), frames.load(), headFrames.load(), headMisses.load(),
            playerOff, pawnOff, *gateWhy ? ", paused: " : "", gateWhy);
    return true;
}

} // namespace bvr::aa::first_person
