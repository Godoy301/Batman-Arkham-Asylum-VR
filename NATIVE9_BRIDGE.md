# A1s: native Direct3D9Ex to Direct3D11

INSTALLED A1s at21:53 Sydney, 2026-10-12;19 owned files/five protected and saved
INI independently verified. Receipt `re/evidence/A1s-build-checks.json`.

2026-10-12. JJ explicitly requested trying a direct route without DX12 rendering
translation. Installed baseline before this change: A1q, native stereo / DLSS off.
A1r was built and checked, never installed; its isolated copy removal saved only
about 0.15ms/pair and did not explain the base scene performance deficit.

## References consulted

- Playbook `docs/10-graphics-apis.md`, `#d3d9-xr-interop-routes` and
  `#who-owns-9on12`, checkout `b955f2244f7c5d5f3b3e5f403b1b6721a3362423`:
  Ex can create shared textures, classic9 cannot; Ex rejects MANAGED resources;
  blind pool remapping drops compatibility; shared Ex textures lack keyed mutexes.
- Playbook ch06 completion-versus-enqueue and ch19 blocking-wait-floor: prove
  ownership/completion and measured costs, do not infer speed from game age.
- Microsoft [DX9Ex](https://learn.microsoft.com/en-us/windows/win32/direct3d9/dx9lh)
  and [D3DPOOL](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpool).
- Arkham-fixer `source/d3d9/buffer_detection.cpp`: INTZ replacement is a lead,
  precision/stencil need actual GPU checks. No shader hashes or offsets copied.
- DxWrapper `d3d9/IDirect3DDevice9Ex.cpp`, commit
  `d77702d55f1725eeae15a6466091db4b998d89b4`: managed-to-dynamic pool remap and
  explicit reset warning. Inspected in `re/dxwrapper-reference`; not incorporated.
- Framework `04-graphics-api-interception.md`: owned-device interception.
  Existing target camera/draw ABI retained; no City/Origins files modified.

## Implementation (SOURCE / BUILT)

The usual launchers choose native9Ex. `AA_GRAPHICS_BRIDGE=9on12` selects the
previous route at startup, exposed by `Batman Asylum VR (Previous Bridge).bat`.
Both retain the native engine stereo seam, FOV, pacing, controls, HUD, captures
and x64 DLSS helper. The fallback is this build's retained9On12 route; the exact
installed A1q files are also backed up by the installer.

`managed9.cpp` exposes classic Device9 over the private native Ex device. Managed
2D/cube/volume textures use SYSTEMMEM shadows and DEFAULT GPU twins; locks, dirty
updates, child/container identities, PreLoad, autogen and logical pool/usage are
preserved. VB/IB CPU contents are retained and uploaded on binding/dirty draws.
Only bound dirty resources are synchronized. State blocks retain logical objects
and sampler state, including explicitly recorded sampler changes. ResetEx keeps
allocations while the facade restores initial render state and managed content.
LOD uses sampler mip limits, enabling point mip selection when an explicit LOD
would otherwise be ignored with mip filtering disabled. Mechanical COM forwarding
is generated from Windows SDK10.0.26100.0 by `tools/generate_device9_forward.py`.

`native9_bridge.cpp` selects the same adapter by native9 LUID and creates an
ordinary D3D11 device. Two eye slots and a HUD slot import cached native9Ex shared
BGRA textures. A native9 EVENT query completes the producer; a D3D11 EVENT query
marks actual reads and must complete before9 reuses a slot. Bounded3s failure
limits are guards, not frame pacing targets. No9On12/11On12/D3D12 rendering device
or queue is created on this route. The DLL still contains the legacy backend;
an imported DX12 library is not evidence that the native rendering route uses it.

Single-sample D24S8/D24X8 depth surfaces get texture-backed INTZ replacements with
their original logical descriptor. Private stencil rendering remains intact in
the actual GPU checks. Depth freezes at the existing pre-clear/rebind boundary.
A native SM3 pass packs24bit rawZ into shared RGB8, then native DX11 decodes it
into R32Float. This avoids half-float far-Z rounding. The positive1 endpoint is
clamped before packing. Other depth formats / multisampling retain the original
surface and fail depth transport closed instead of pretending valid data.

DLSS protocol is ABI8 and each launch gets PID/tick-suffixed mapping/event names
passed to the helper. A terminated old harness retained the former fixed name
(error183), exposing a fixture null-mutex dereference. The fixture now rejects
initialization failure before readback; explicit native/DLSS cleanup prevents
driver teardown under the loader lock. No NVIDIA profile changes made.

Build directory: `build-native9` (Win32 Release), helper `build64` (x64 Release).
Existing legacy build artifacts/evidence remain separate.

## GPU results (OFFLINE, actual4070Ti)

Receipt: `re/evidence/A1s-ctest-soak.txt`. All11 checks passed.

- Classic9 managed allocation succeeds, shared create AND opening an existing Ex
  texture fail INVALIDCALL. Both Ex creation routes reject MANAGED and share
  successfully; plain Ex creation alone is insufficient compatibility.
- Native resource check: real textured/indexed draws, updates while bound, child
  identity, Create/Begin/End state blocks, explicit LOD, partial-copy boundaries,
  cube/volume shader pixels, reset and CPU contents. Both2836x3040 shared eye
  images checked at every pixel. Independent harness loads no9On12 module.
- Assembled DLL: production HUD hooks preserve clean world/private stencil alpha,
  both eye lenses and BGRA/RGBA conversion; actual geometry depth .25/.75 survives
  a later clear1/rebind. Far-Z/clear0/clear1 checked within2/16777215.
- AFW native-DX11 GPU checks: both directions, asymmetric lenses, forward/inverted
  depth, alternating histories and sky regression. This is not a headset flicker fix.
- Actual x86-to-x64 DLAA/Quality and4097 stereo pairs on nativeDX11, distinct eye
  pixels/histories, no fence timeouts; missing-right / cancellation rollback and
  reset/resize. Small synthetic DLSS textures are not a retail speed measurement.
- Retained9On12 regression,8192 physical Presents, pipeline cache/dynamic bias,
  native ABI, launch profile rollback and overlay navigation also pass.

## One-launch headset plan (JJ launches / navigates)

1. Usual VR shortcut: verify title/menu, stereo fusion, overlay and graphics line
   `Native DX9Ex -> DX11`. Settings initially remain Native / DLSS off / AFW off.
2. Same opening-gameplay spot as the A1q comparison:30-60s normal play, include
   blank-wall and ordinary-scene views. Passive logs record completed-pair FPS,
   scene chunks, transfer waits and memory at unchanged resolution/lighting.
3. F2 or overlay rolling stereo capture if ground/lighting/HUD looks wrong.
   Optionally use existing timed stock/DLSS comparison in this same launch;
   AFW is optional last because its retail flicker remains unaccepted.
4. Quit normally. Inspect observed native route, completed pairs, timings,
   captures and profile rollback before attributing any gain to the backend.

LIVE / HEADSET performance and complete retail resource coverage remain pending.
Do not claim a90FPS fix from these synthetic checks. Engine defaults such as
GetDevice on unwrapped DEFAULT resources, unusual volume child identity/formats,
and full retail reset/lifetime paths remain target-validation boundaries.
