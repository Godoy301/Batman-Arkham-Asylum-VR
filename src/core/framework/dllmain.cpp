#include "framework.h"

#include "core/util/log.h"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
namespace aa { std::string HashFile(const std::wstring& path); }
namespace {
std::atomic<DWORD> identity{0};
constexpr char kSteamGotyExeSha256[] = "4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028";
constexpr char kJokerDlcPatchedExeSha256[] = "ed65ce3c52baca7adaea00cb0e5def3d8a78bd05f7ceacd68a68462e92d8dab2";
}

extern "C" __declspec(dllexport) DWORD WINAPI AA_Identity() { return identity.load(); }

namespace {

DWORD WINAPI InitThread(LPVOID) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    auto* name = wcsrchr(path, L'\\');
    if (_wcsicmp(name ? name + 1 : path, L"ShippingPC-BmGame.exe") != 0) {
        OutputDebugStringA("[Batman Asylum VR] Unsupported host executable name; VR initialization skipped.\\n");
        identity.store(2);
        return 0;
    }

    const std::string hash = aa::HashFile(path);
    const bool stockBuild = hash == kSteamGotyExeSha256;
    const bool knownJokerPatch = hash == kJokerDlcPatchedExeSha256;
    if (!stockBuild && !knownJokerPatch) {
        char message[256]{};
        sprintf_s(message,
                  "[Batman Asylum VR] Unsupported host executable SHA-256: %s; VR initialization skipped.\\n",
                  hash.empty() ? "<hash failed>" : hash.c_str());
        OutputDebugStringA(message);
        identity.store(2);
        return 0;
    }

    identity.store(1);
    bvr::framework::init();
    BVR_LOG("[aa identity] accepted host executable: %s",
            stockBuild ? "official Steam GOTY" : "known Joker DLC-patched Steam GOTY");
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // All real work happens off the loader lock.
        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        // Breadcrumb that separates an ORDERLY shutdown from a hard kill. A log
        // that just stops mid-heartbeat with no line at all (session 23's
        // external report) means neither ran - i.e. TerminateProcess or a
        // fail-fast, not an exception our filter could ever have seen.
        BVR_LOG("shutdown: DLL_PROCESS_DETACH (orderly process exit)");
    }
    return TRUE;
}
