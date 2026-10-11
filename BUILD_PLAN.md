# Asylum build and grouped tests

## TEST PLAN A0 — isolated clone and real graphics bridge

Looked up: ASYLUM_PLAN.md reference sections, playbook b955f224.

| # | switch / action | proof | judge |
|---|---|---|---|
| 1 | source clone + fingerprint tool | isolated donor/target identities | source digest receipt, Asylum hash, no donor offsets linked |
| 2 | GPU bridge fixture (no game) | 9On12-to-11On12 transfer | exact color readback, repeated return, resize, managed texture compatibility |
| 3 | ordinary Steam launch | vanilla behavior | proxy forwards only; VR factory replacement requires -bavr |
| 4 | dedicated VR launcher | real game bridge and Origins-style panel | game-device ownership, actual source dimensions, XR/session log, capture |
| 5 | menu -> campaign -> pause/map/detective -> menu -> quit | first live baseline plus transition/reset evidence | one run, all captures/status together; camera hooks require Asylum guards |

Native camera, AFW and DLSS become testable only after their Asylum render seams
and depth are established. Do not present a cinema bridge as native stereo.
No game launch by the agent; JJ drives the installed build's first run.

### A0 RESULTS

2026-10-11: Release Win32 build installed on E:, 14 manifest-owned files
independently hash-verified. Runtime DLL SHA256
`D3F817FAF3AB01D035A3FD368382CA4EC36AC7FD7AA20694802B30C497A8520E`.
All three CTests passed on the final source: foreign-host/proxy guard, actual
D3D9 GPU transport, and ANSI/UTF-16 graphics-profile scope/byte-exact rollback.
The GPU fixture exercises eight BGRA-to-RGBA color transfers, managed textures,
fenced ownership return and device reset/resize. The frozen x64 helper also
builds successfully, but is not installed or active: Asylum depth, motion vectors
and per-eye input remain unimplemented.

Receipts: `re/evidence/A0-ctest.txt`, `A0-build-checks.json`,
`installed-build.json`, `A0-before-install.json`, `donor-source.json`.
Protected Asylum executable/configuration and Origins runtime/settings bytes
are unchanged after installation. Installer backups are under `re/backups/`.
Grade at initial deployment: BUILD/OFFLINE + DEPLOYED. First live outcomes and
the superseding A0a build are recorded below.
Native stereo, AFW, DLSS, game HUD/shaders and transitions remain pending.

### A0 source progress before first live run

Frozen Origins O1H core, dependencies, input, licenses and x64 helper source;
only Asylum's adapter/graphics bridge and guarded Asylum command observers link.
GPU bridge uses plain 9On12, adopts the game GPU, unwraps its completed image,
copies on an ordered 11On12 queue, flushes, and returns ownership with a checked
fence. XR image release explicitly flushes 11On12 commands. FPU behavior flags
are passed through unchanged. Actual GPU checks already passed exact color
transfers, managed textures and resize; foreign-host/normal-launch guards and
ANSI/UTF-16 graphics-profile scope/rollback checks also passed. The installed
build converts the game surface into RGBA8 before feeding the core, and passes
runtime-recommended output dimensions independently of the desktop window size.

STATIC/Ghidra: completed focused decompilation of Asylum command executors and
three candidate roots (`ghidra-render-focused.txt`). Root 0x00cc2310 is tiled
capture, and 0x00cc2ab0 is another capture/readback route; neither establishes
the gameplay draw seam. 0x00ccae50 is the ordinary draw candidate: it uses the
second Begin/End command family, viewport+0x20 client and its draw slot +8.
No camera or stereo mutation is enabled from this inference. The two tiny
Asylum command executors have full 16-byte loaded guards and preserve their
original return/one-call behavior while recording counts/stacks/viewport data.
The first live run must distinguish the ordinary caller from capture paths.
Broad Ghidra analysis timed out at 120 s (149 s reported total) but saved its
partial database; focused decompilation completed independently. This was a
separate workspace database, not the user's open Ghidra session.

References remain ASYLUM_PLAN.md's cited routes/checkout. Before first launch,
capture transport/session status, command A/B counts, and menu/campaign/pause/
map/detective/menu/quit in one run. No per-eye camera, AFW, DLSS or optical shader
claim follows from a successful flat game-frame bridge.

## TEST PLAN A0a — first-launch graphics-factory recursion

Looked up: playbook `06-debugging-methodology.md` "Crash-dump workflow"
(instruction/register/stack evidence), existing chapter 10 9On12 ownership
route, checkout b955f224. No Asylum-specific dual-factory precedent found.

LIVE 2026-10-11 14:31: user launched A0. Target guard and VirtualDesktopXR
instance initialization succeeded. Two plain9On12 factories were created;
no game-device/Present log followed. WER reports Asylum DLL+0xb67d7 access
violation. Local crash dump resolves this to CRT stack probing, with 8,699
instances of DLL+0x2f933 on the captured stack. That instruction immediately
follows the `originalCreate` call in our CreateDevice detour. This establishes
recursive factory-hook dispatch, before any image transport or native camera.

| # | action | proof | judge |
|---|---|---|---|
| 1 | replace global factory CreateDevice detour with per-factory COM forwarding wrapper | each factory owns its original dispatch, independent of nested/shared system implementations | no global factory trampoline; unchanged arguments/HRESULT/object lifetime |
| 2 | actual GPU fixture creates two wrapped plain9On12 factories/devices | covers the missed startup shape | both devices succeed, QI identity/refcounts correct, existing exact transfer/managed/reset checks pass |
| 3 | final build/deploy and verify rollback receipt | concrete corrected build | three CTests plus installed hashes, game config restored after failed launch |
| 4 | JJ repeats grouped route on A0a | real game reaches owned device and XR frame, command callers gathered | LIVE logs and headset image; if no device, inspect returned HRESULT/caps before further stereo changes |

A0a RESULTS: implemented per-factory COM forwarding; no global CreateDevice
hook/trampoline remains. The GPU fixture creates devices through two wrapped
9On12 factories, checks COM identity, then runs all previous transport checks.
Final Release build passes all three CTests. A0a installed, 14 hashes verified,
protected bytes unchanged during this deployment. DLL SHA256
`0A5506CE8C17BFC358A993B8C0E1ECF84FE051405694A6E3440EB124CECCF261`.
Receipts: `A0a-ctest.txt`, `A0a-build-checks.json`, `A0a-before-install.json`,
current `installed-build.json`; failure proof `A0-factory-crash.json` and both
`A0-*-launch.log` files. JJ confirmed A0's second run displayed desktop only,
no headset or panel, and closed it normally before A0a installation.
A0a real-game/headset result pending; native stereo/AFW/DLSS remain pending.

The initial A0 protected receipt is a historical snapshot: subsequent user
game runs and an externally updated Origins DLL mean it no longer describes
all current bytes. A0a's protected receipt captures the current pre-deployment
baseline and was verified entry-by-entry after installation. The Asylum
installer does not target Origins files. Active graphics journals are restored
before replacement, and game-modified baseline files are not overwritten from
an older unrelated installation snapshot.

Live command B runs on the render thread; its stack contains no enqueue-site
caller. Do not treat an empty exe stack as proof against 0x00ccae50. Its viewport
vtable RVA is 0x01d2c380, whose static slot +0x54 is 0x00cc29c0. This establishes
the command's viewport type/begin method, not the game-thread draw/camera ABI.
