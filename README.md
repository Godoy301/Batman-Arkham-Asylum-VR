<p align="center">
  <img src="https://cdn.cloudflare.steamstatic.com/steam/apps/35140/capsule_616x353.jpg" width="616" alt="Batman: Arkham Asylum Game of the Year Edition">
</p>

> **Work in progress - not ready to play yet, and there is no release.** Batman: Arkham Asylum Game of the Year Edition in PC VR, made the same way as
> [Batman City VR](https://github.com/junglejuz/Batman-Arkham-City-VR) and
> [Batman: Arkham Knight VR](https://github.com/junglejuz/Batman-Arkham-Knight-VR): the game draws both eyes itself
> (native stereo), with per-eye DLSS and OpenXR for Virtual Desktop and SteamVR. The notes below are the developers'
> working notes. The picture above is the game's own Steam store artwork, shown from Steam (Batman: Arkham Asylum Game of the Year Edition © Warner Bros.
> Entertainment, Batman and all characters © DC). This is an unofficial fan mod and will need your own copy of the game.

# Batman Asylum VR — development build 0.1.0-A0a

Asylum's isolated port starts from the Origins O1H core accepted by JJ as the
behavior reference. Target: Steam Asylum GOTY on E:, x86 ShippingPC-BmGame.exe.
The retail target imports D3D9, so an Origins DX11 DLL cannot be dropped in as
the completed conversion. Read ASYLUM_PLAN.md and BUILD_PLAN.md for the route.

Implemented: dedicated D3D9/XInput proxies, exact-build guard, plain 9On12
game-device adoption, GPU-only 11On12 game-frame transfer into the copied
OpenXR/UI core, fenced resource return/reset, separate logs/settings,
Origins-style F10/both-stick controller panel and capture controls, headset eye
resolution query, Virtual Desktop/default-runtime and SteamVR launchers,
manifest-owned install/uninstall and temporary near-square graphics profiles
with byte-exact restoration. Asylum-specific guarded command observers gather
the real render-thread/caller evidence needed for native stereo. GPU tests use
the actual bridge; no game is launched by the agent.

**This first build is a game-frame/cinema bridge, not the finished native VR
conversion.** Asylum camera/constructor/view-state seams, same-frame native
pairs, HUD separation, shader/DoF behavior, full-view transition triggers,
gamepad title skip, depth/AFW and active per-eye DLSS remain to implement and
validate. The frozen x64 helper builds successfully but is not installed or
advertised as an active feature yet. No Origins engine offsets, shader identities
or private captured bat asset are linked into this build.

Development launch after installation:
`E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries/Batman Asylum VR.bat`.
JJ drives the first grouped run: title/main menu, campaign, pause/map/detective,
return to menu and quit. F10 or both sticks held 0.6 s opens the panel; capture
saves evidence under `%LOCALAPPDATA%/Batman_Asylum`. Normal Steam launches remain
flat. Origins' installation/settings are preserved.

Build: CMake Visual Studio 2022 Win32, then Release. CTest covers foreign-host/
proxy forwarding, real 9On12 GPU transfer/reset and graphics-profile rollback.
All three checks pass; A0a is installed and its 14 owned files are hash-verified.
Current source/build/install/live/headset status is recorded in BUILD_PLAN.md
and `re/evidence/installed-build.json`. A0's first runs exposed graphics-factory
recursion/desktop fallback; A0a replaces that hook with independent factory
wrappers. Its real-game/headset result is pending.
