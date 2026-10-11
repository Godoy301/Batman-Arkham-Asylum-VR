# Batman Asylum VR

Read ../VR_ESSENTIALS.md and ../SESSION_START.md, then README.md and ASYLUM_PLAN.md.
Read RE_NOTES.md before choosing native camera/draw seams.
Target: Steam Asylum GOTY, E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY,
Binaries/ShippingPC-BmGame.exe, x86, SHA256
4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028.

Origins O1H is the frozen donor for controls, XR, captures, pacing and graphics
conventions. Asylum's imported graphics interface is D3D9, unlike Origins' DX11
path. Never compile or call Origins/City camera, allocator, movie or Wwise
offsets in this target. Discover and guard Asylum's own seams.
Native same-frame stereo is the intended default; AFW optional, DLSS per-eye,
VDXR primary and a separate SteamVR launcher. A flat bridge is infrastructure,
not completion of the conversion. Keep source/static/offline/live/headset distinct.
JJ launches/navigates the games. No autonomous game launch or kill.
Read BUILD_PLAN.md before a launch; combine checks into one launch. Record external
file changes in FIX_CHANGES.md. Private animation is not a public asset.

Current checkpoint: A0a Release installed on E:, three offline/GPU checks pass;
A0 live launch recursed in CreateDevice or fell back desktop; A0a uses independent
COM factory wrappers and adds two-factory regression coverage. A0a game/headset
result pending. Native camera, AFW, DLSS and game HUD fixes are pending.
Ordinary draw candidate 0x00ccae50 is static-only. 0x00cc2310 and 0x00cc2ab0 are
capture/readback routes; do not use them as gameplay draw roots. Guarded observers
at Asylum 0x00c96bb0/0x00c96cd0 record live callers without doubling draws.
