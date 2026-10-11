# Arkham Asylum VR conversion

2026-10-11. Requested by JJ: replicate Origins' current treatment and conventions.

Looked up: playbook start-new-port PORT-00/01/02, reverse-engineered-route RE-01,
graphics-apis chapter 10 #d3d9-xr-interop-routes, #who-owns-9on12 and
#fpu-preserve-contagion; cross-project-index D3D9 bridge precedents; framework
guide 16 milestones 1/2; VR_ESSENTIALS sections 2/3/4/6/9/11, VR_HUD_GUIDE 6/8
and PACKAGING_GUIDE 4. Checkout b955f2244f7c5d5f3b3e5f403b1b6721a3362423.
Microsoft SDK d3d9on12.h/d3d11on12.h and CreateWrappedResource documentation:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11on12/nf-d3d11on12-id3d11on12device-createwrappedresource

## Scope and authority

Closed retail UE3 executable: RE-owned. Single-player GOTY campaign first, third
person and original gamepad actions. Quest 3/Virtual Desktop is the primary
headset; SteamVR must have its own launcher. Match Origins' both-stick 0.6 s
panel, F10, saved settings, captures, recentre and UI conventions. Native engine
same-frame stereo defaults on after its Asylum seams are established; no AER.
AFW and DLSS are optional routes, with per-eye temporal histories and vectors.
Physical hand combat and tracked gadget interaction follow the existing mods'
scope rather than becoming new requirements for the first conversion.

## Target and baseline

E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries/ShippingPC-BmGame.exe
SHA256 4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028.
STATIC: x86, image base 0x00400000, normal .text/.rdata/.data PE sections;
imports Direct3DCreate9, XInput ordinals 2/3 and Bink movie exports. This is
static API evidence, not an observed active device. No existing d3d9/XInput
injector found in Binaries. The generated BmEngine.ini is ANSI, not Origins'
UTF-16. It currently records 1024x768, windowed, DoF/motion blur on and smoothing
on. Do not blindly copy Origins' launcher/XML edits.

## Implementation order

1. Freeze a source-only copy of Origins O1H's universal core and dependencies,
   licenses, input/panel, XR and DLSS helper; isolated Asylum logs/settings/builds.
   No donor executable offsets or shader hashes are used.
2. Build a real D3D9 game-frame bridge: intercept the game factory, use plain
   Direct3DCreate9On12 (not Ex), adopt its D3D12 device, preserve game creation
   flags/FPU behavior. Unwrap the actual completed game surface and use one
   ordered queue with D3D11On12 to feed the frozen XR/UI core, no CPU image path.
   GPU fixture must prove color/channel/alpha and resource return/reset ownership.
3. Identify Asylum's viewport draw, CalcSceneView/FSceneView constructor, allocator
   and render-thread eye labels. Prove live callers before doubling. Feed both
   eyes one simulation tick and located pose, near-square render extents and
   full asymmetric runtime angles. Maintain independent eye state and enforce
   atomic pair submission; update per-eye scene captures/reflections.
4. Capture Asylum's own Scaleform/HUD lane with dedicated stencil; preserve
   menu content, full-view fades/bats and gamepad title skip through its Bink
   movie lifecycle. Copy the solved layer handoff/black-opening behavior, not
   Origins' Wwise event or movie-player offsets.
5. Adapt depth, camera motion vectors and per-eye DLSS/AFW to D3D9 transport;
   preserve stock AA until actual NGX success. Remove optical DoF/motion blur
   without accidentally removing the whole UE3 grading/AA/map post chain.
6. Run one grouped menu/gameplay/map/detective/reflection/title/quit/soak test;
   then validate SteamVR and package a manifest-owned reversible install.

## Evidence boundaries

STATIC imports and source copies do not prove a working VR renderer. GPU bridge
checks do not prove Asylum camera integration or headset quality. User reports
Origins in a very good state; preserve that installation and use it as the
accepted behavior reference. Record each Asylum gate in BUILD_PLAN.md.
