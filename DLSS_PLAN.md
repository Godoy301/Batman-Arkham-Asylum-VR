## A1q installed: optimize native rendering with DLSS off

HEADSET A1p: JJ says ground/shadows fine, no FPS gain; DLSS off about75FPS
versus40-50 withDLSS in the opening gameplay section. Desktop4K about230FPS;
City/Origins faster. The base VR deficit remains a priority independently ofDLSS.
LIVE evidence preserved in re/evidence/A1p-dlss-ab BEFORE fixtures. Previous log
is menu DLSSon thenoff; original gameplay DLSSon timing log is unavailable, so
these are not matched gameplay timing captures. Current Off log source2836x3040
per eye (17,242,880 pixels/pair), output2284x2448 per eye; desktop4K8,294,400.
This is2.079x source pixels, plus two scene passes and the D3D9->9On12->11On12
interop path. No inference that pixel count alone explains the observed FPS.
Off scene CPU about4.3-5.35ms/eye, GPU elapsed about4.95-6.08ms/eye; elapsed
includes queue effects/interception, not pure GPU busy time. Unused depth copy
still cost .45-.68ms CPU/eye. A1p nativePSO creations only395/17765requests;
no FPS gain despite much better reuse. Long OOM stability remains unaccepted.

Looked up: playbook docs/19-d3d12-and-performance.md #dlss-in-vr, performance
ledgers and per-section bridge-cost table, checkout
b955f2244f7c5d5f3b3e5f403b1b6721a3362423; VR_ESSENTIALS sections3/4/9 and
framework04-graphics-api-interception (owned-device boundaries). City
DLSS_PLAN.md D1b/D2: modern model eval can exceed rendering savings; early
clarity fix removed inputFXAA and adjusted mip bias. Origins dlss_bridge.cpp
CAMMV/MIPBIAS method inspected; target semantics still require Asylum proof.
NVIDIA official NvApiDriverSettings.h SR mode3=snippet-controlled, preset13=M;
NVAPI GetNGXOverrideState is READ ONLY. SDK declaration/query IDs from
references/luma-framework-2026-10 @5d7ee5470d8f09e27dfe529a386af46d8c6f8e33.

SOURCE A1q: HUD scene-depth selection/DS queries/transport run only for a depth
consumer: DLSS, AFW, or an explicitly armed still capture. Off/AFWoff retains
normal game depth writes and HUD extraction while skipping unused copies/fences/
flushes. Still captures arm the next scene and wait up to1.5s for fresh depth;
titles/flat captures do not wait, absence of depth cannot stall forever. Rolling
stereo history is unchanged. Texture-detail hook still forwards every engine
texture bind but skips descriptions/COM probes/redundant sampler writes when
its correction is inactive. Active correction inspects bound textures at scene
start (handles enabling while already bound); original sampler reads, reversible
mild bias and HUD exclusion retained. Temporal jitter period follows actual
output/input ratio instead of fixed32; one phase for both eyes, independent
histories and existing reset/relative-camera motion rules retained. No resolution,
lighting, game quality, backend or frame-pacing reduction.

DLSS investigation: official signedCNN3.7.20 candidate downloaded from NVIDIA
v3.7.20 @a11125971f4d5f19ce17ca8cbe6f703eaa16de76; provenance/license in
third_party/dlss_cnn. Initial benchmark labels/timings are REJECTED as model
comparison: verbose NGX proves provider replaced the requestedDLL with cached
310.9.0 (160_E658700.bin) and forced global preset13. Mode3 in driver log is
snippet-controlled, NOT forcedQuality. No claimed CNN speedup, no CNN deployment.
Current310.9.1 package retained; provider-selected path recorded. Read-only own-PID
NVAPI query after feature creation confirms status0/preset13/flags23f or63f;
override displayed in overlay and capture status. Global/app driver settings are
unchanged. Benchmark --cnn is a local research flag, not a shipped model choice.
UltraQuality native NGX feature request was rejected in the research benchmark;
that pre-existing option is not accepted as a supported runtime mode yet.

OFFLINE: final9 checks pass; A1q-ctest-soak.txt includes4097 completed DLSS pairs,
8192 Present soak, two separate eye histories/colour, rollback/mode-off/reset,
depth/AFW/HUD/lens/fullsource fixtures and previous260 static/dynamic pixel checks.
New actual scene write/clear/rebind fixture makes ZERO depth transfers without
consumers, then explicit still request preserves .25/.75 physical scene depth
beforeclear in both eyes; DLSS/AFW requiredepth verified.512 real stock texture
binds make ZERO metadata probes, then active mild bias/readback/restore still
passes. Two test-only setup mistakes corrected before deployment: unused scene
wrote colour into the fixture's blue source, and its default-pool DS was retained
acrossReset; now colour writes suppressed and DS released. Budget fixture gets a
fresh sampled pair after the preliminary scene. NVAPI helper-only follow-up GPU
check passes4.45s; A1q-ctest-helper-final.txt/A1q-helper-final.log.
These are offline correctness proofs, not FPS/blur/OOM/headset acceptance.

INSTALLED:18 owned destination hashes verified and five CURRENT protected
config/executable hashes unchanged; aa_rendering.ini byte-exact unchanged,
DLSSMode0 preserved, no active game/helper/profilejournal. Backup, hashes and
validation in re/evidence/A1q-build-checks.json. Added NVIDIA NVAPI MIT notice.
No autonomous game launch/kill, City/Origins edits or upstream reference edits.

NEXT SINGLE JJ run: same opening gameplay/native with savedDLSSoff for30seconds,
note FPS then exit normally. Ordinary logs/captures automatic; no comparison
button or crash wait required. Check depth transport falls tozero between the
on-demand captures and whether scene/pair time falls. If stillunder90, investigate
remaining base scene/translation/colour submission cost rather than declaring
DLSS-off acceptable. Separately prepare a controlled Asylum-only DLSS model
choice that actually survives driver override; don't change global driver policy.
Remaining: baseFPS, DLSS cost/mid-distance clarity, long stability and retail AFW
flicker/ground artifacts/head jitter. A1p ground/shadows accepted byJJ only.
Earlier checkpoints below are historical.

## A1p installed: exact dynamic depth bias

LIVE A1o:665/679 sampled nearest misses change integer DepthBias;668 raster,
16 shader, zero root, one slope and no clamp differences.22835 requests/1085
hits/21750 creations; freeVA1576->882MiB. Samples are heuristic nearest-key
comparisons, not universal proof, but repeatedly isolate the varying bias.
A1o log/profile/INI preserved in re/evidence/A1o-run before GPU fixtures.
A1o reuse/FPS/memory remain unaccepted. No retail A1p result yet.

Looked up: Microsoft DirectX-Specs VulkanOn12 dynamic depth bias/API/DDI0099:
https://github.com/microsoft/DirectX-Specs/blob/master/d3d/VulkanOn12.md
INT bias already casts toFLOAT; dynamic flag enables RSSetDepthBias, and
SetPipelineState must restore its declared bias. SDK26100 OPTIONS16 and
GraphicsCommandList9 support gated on actual adapter; real 4070Ti test supports
it. Playbook ch19 identity/lifetime/budget and ch06 proof discipline, checkout
b955f2244f7c5d5f3b3e5f403b1b6721a3362423; existing framework04 interop.
TranslationLayer PipelineState.cpp RoundTripPSOs/GetCachedBlob inspected (saved
A1o-run/PipelineState.cpp): preserve original cached-blob descriptor compatibility.

SOURCE A1p: new dynamic_pipeline.cpp/h. Supported owned-device graphics PSOs
use shared native PSO with dynamic-bias flag and zero initial bias/clamp/slope.
Each original request returns a compact COM variant retaining its EXACT integer
bias and FLOAT clamp/slope, and a shared owned descriptor/native PSO template.
SetPipelineState, CreateCommandList initial state, Reset and ClearState unwrap
native PSO and then set exact original dynamic values. SDK-defined vtables,
real current-device probe/interfaces/hooks checked; no engine offsets reused.
Immutable root/shaders/blend/depth/input/formats remain in exact cache key.
No bias quantization or lighting/quality changes. Standard PSO COM identities,
GetDevice and per-variant private metadata retained. GetCachedBlob lazily creates
an original static counterpart and returns a compatible original blob; requests
carrying cached blobs remain stock. Unsupported hardware/APIs, existing dynamic
PSOs, unsupported IID/cached blobs/nonfinite values or creation failures retain
original paths. Compact variants and shared templates log live counts; any
remaining translation-layer/software pipeline growth is not eliminated by proof
of native PSO reuse. Weak template registry bounded512, native cache512 entries/
16MiB keys; caller-held object lifetime not artificially capped. Drained reset
clears mod cache/template lookup references, not caller ownership.

OFFLINE final9 checks pass, A1p-ctest-soak.txt. Real GPU4096 distinct integer
bias variants hold caller refs while sharing ONE native PSO,4095 reuse hits.
260 pixel comparisons vs original static factory: D24/D32, LESS/GREATER depth,
13 bias/clamp/slope cases including INT_MIN/MAX and +/-16777217, each across
Set/Create/Reset/Clear/rebind. Distinct original images ensure meaningful test;
full16x16 RGBA equality. Standard COM/private metadata distinct between variants,
original static cached blob reload and caller-held render after cache clear pass.
Existing real D3D9 interop/depth/HUD/AFW/DLSS checks also run with dynamic path
active and pass; extended4097 DLSS pairs/8192 Presents. This is GPU fixture proof,
not headset FPS/crash/shadow acceptance. A1p-build-checks marks retail pending.
Installed17 owned hashes verified; five CURRENT protected hashes unchanged,
no active profile journal/game/helper. No autonomous game launch/kill, no other
Batman mods, upstream reference, executable or driver policy modified.

NEXT SINGLE JJ launch: same beginning scene/native for~1minute, note FPS and
whether ground/shadows look correct in both eyes; normal exit. Automatic logs
show native creation vs variants, exact binds, live handle/template counts,
static/blob fallback count and memory trend. No comparison/crash wait needed.
Read evidence before declaring success or choosing next cause. Retail stability,
frame rate, AFW flicker/ground/jitter remain pending.

## A1o installed after A1n retail failure

LIVE A1n:38560 graphics requests,788 hits (~2.04%),37772 successful creations;
cache stayed512 entries/~3.9MiB keys but driver/engine process VA still depleted
1613.7->585.1MiB, private1721->2640MiB. Native pairs~50-55/s in gameplay.
Saved all logs/profile/INI in re/evidence/A1n-run before fixtures. A1n exact reuse
is NOT an accepted retail fix; don't claim 90FPS or cured OOM. Cache growth itself
bounded; low reuse means active/irrelevant differences or working-set churn need
measurement, not blind cache-size/bias quantization.

Looked up again: playbook ch06 measurement and ch19 full identity/lifetimes,
cross-project-index/framework routes (no direct PSO cure), checkout
b955f2244f7c5d5f3b3e5f403b1b6721a3362423. Microsoft D3D12_BLEND_DESC explicitly
ignores RT1..7 when IndependentBlendEnable=false:
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_blend_desc

SOURCE A1o only omits documented ignored blend slots from KEY for valid blend
values. Malformed/future values retain full original key; independent blending
retains all eight slots. All shaders/raster/depth/input/RTV formats still exact.
Original descriptor/API arguments unchanged. Proposed omission of inactive RTV
formats was REJECTED by real GPU validation: setting unused RTV7 format can fail
original API. Keep all eight formats and regression-check exact failure HRESULT.
No depth-bias quantization, disabled lighting, reduced resolution or cache-size
increase. Pipeline reuse retains512 entries/16MiB key limit.

Adds sampled (one/32 misses) nearest key-group fingerprints: root+device,
shader bytes, stream output, blend, raster, depth/stencil, input and target/flags;
depthBias/slope/clamp difference counters. Candidate selection is diagnostic,
not proof of a causal field or cache equivalence. Full bytes still determine
actual reuse. Heap trace/private/free/driver usage and creation counters remain.

OFFLINE nine final checks pass (A1o-ctest-soak.txt). Real GPU verifies moved
bytecode1024-request reuse; documented ignored blend fields reuse, active independent
blend stays separate; direct original and cached paths render identical red pixels;
red/green shaders separate. Invalid sample count, unused format and malformed blend
requests preserve original HRESULT. Nearest sampling sees known bias variations.
Eviction/clear/caller-held GPU render survives; previous full depth/HUD/AFW/DLSS/
profile checks and4097 DLSS pairs/8192 Presents pass. Synthetic test is not retail
FPS/pressure acceptance. Installed17 owned and five CURRENT protected hashes
verified; no active profile journal or game/helper. Audit A1o-build-checks.json.

Next SINGLE JJ run: same native opening gameplay scene30seconds, observe FPS,
normal exit. Automatic field-group/bias counts and memory trend should distinguish
real state churn from ignored blend misses. No comparison/crash wait. Read evidence
before any new tweak. User drives launches; no City/Origins/upstream edits. Retail
OOM/FPS, AFW artifacting/ground/jitter remain open.

## A1n installed: exact graphics pipeline reuse

LIVE A1m confirms21338 graphics pipeline creations by14:40:19 (~65s), zero
creation failures;16 large reservation traces carry pipelineActivity1 and
full call chain: NVIDIA ->D3D12Core ->owned graphics hook ->d3d9on12 worker.
FreeVA falls1593->903MiB after startup. Actual trace saved A1m-run. This proves
pipeline creation contributes to address-space growth. It does NOT prove all
creations are duplicates, all growth is PSOs, or creationCPU alone explains
40-50FPS (cumulative graphics creationCPU~1033ms, async worker costs not measured).

Looked up: playbook ch19 canonical identity/full descriptions/lifetime budgets,
commit b955f2244f7c5d5f3b3e5f403b1b6721a3362423; Microsoft D3D12 graphics
pipeline descriptor and Managing Graphics Pipeline State (immutable state reuse):
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_graphics_pipeline_state_desc
https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12
Upstream9on12 PipelineStateKey/GetPipelineState/Trim remains a lead; current
Windows cache behavior is established only by LIVE intercepted API calls.

SOURCE: new pipeline_cache.cpp/h. Exact content key serializes every descriptor
field (no padding), actual shader bytes, semantic strings, stream declarations/
strides; canonical device/root identity with retained root reference. FNV hash
is only lookup; full byte equality required.512-entry LRU/16MiB key storage,
512KiB maximum key. Driver PSO memory is opaque and not included in key limit.
Unsupported IID/null/oversize/unsupported cached-blob requests use original API;
failed creations are not cached. TLS recursion guard, serialized cache mutation,
COM references retained; caller-held objects survive eviction. Drained reset
clears cache references. Shader/raster/bias/lighting state unchanged; no lossy
bias quantization. Passive counters log requests/hits/real creations/bypass/
evictions/live/key bytes. Stream PSOs remain original (zero in A1m).

OFFLINE final nine checks pass, A1n-ctest-soak.txt. Real GPU:1024 requests from
moved shader byte buffers reuse same COM PSO; different red/green shader gets
separate PSO. Invalid request preserves original failure HRESULT.520 bias-state
variants force eviction within limits; clearing removes entries; caller-held
red/green PSOs then render exact9-point pixels using D3D12 command lists/readback.
All prior stereo/depth/HUD/AFW/DLSS/rollback checks pass including4097 DLSS pairs
and8192 physical Presents. This synthetic reuse is not a measured retail duplicate
rate or proof of FPS/crash fix. A1n-build-checks explicitly marks retail pending.
Installed17 owned files; five current protected files unchanged, no active profile
journal/live game/helper. Backup/runtime hash in A1n-build-checks.json.

Next SINGLE JJ launch: same opening gameplay scene in native, observe FPS for
about a minute then normal exit. No transfer comparison required. Automatic
reuse and memory counters establish whether actual repeated creations are
eliminated and whether remaining pressure comes from distinct states. Observe
image correctness along with speed. No autonomous launch/kill, no City/Origins
changes; ground/AFW/jitter and retail performance/stability acceptance remain open.

## A1m installed: deeper allocation provenance and pipeline counters

A1l user run preserved in re/evidence/A1l-run/, including normal exit. It
reproduced VA depletion WITHOUT transfer comparison: after initialization
freeVA1613.9MiB/largest1536.1 ->702.1/largest610.4MiB. GPU usage remained well
below budget. Recurring15.8MiB heap reservations terminate in NVIDIA/D3D12 at
stack12; those frames do not establish which rendering operation requested
them. A1l's stack limit was too shallow. Forty-to-fifty gameplay FPS and original
retail OOM remain unresolved; no new headset performance acceptance claimed.

Looked up again: playbook ch06 measurement/environment and ch19 lifetime/budget
references, checkout b955f2244f7c5d5f3b3e5f403b1b6721a3362423. Microsoft
D3D9On12 src/9on12PipelineStateCache.cpp GetPipelineState/Trim describes pipeline
creation and cache trimming. Saved upstream-main source in A1l-run/d3d9on12-source;
this is a lead, not evidence that the installed Windows build leaks PSOs.
https://github.com/microsoft/D3D9On12/blob/main/src/9on12PipelineStateCache.cpp

SOURCE A1m extends reservation stacks12->48. Adds passive CreateGraphicsPipelineState
and Device2 CreatePipelineState counters and CPU durations; unchanged descriptors,
arguments/results and cache policy. Thread-local pipeline activity is saved with
reservation records; asynchronous driver allocations may lack that marker.
Counters cover process calls through hooked implementations; nested calls may
count twice. Allocation ring remains bounded128; no allocator logging/symbol work.

OFFLINE final9 checks passed: actual allocation trace/release; observed11 graphics
pipeline creations/zero failures while existing colour/depth/HUD/DLSS/reset checks
pass;8192 Presents and4097 DLSS pairs remain stable in the tiny synthetic fixture.
Evidence A1m-ctest-soak.txt/A1m-build-checks.json. Installed17 owned hashes verified;
five restored-baseline protected hashes unchanged by installation. Current A1l
profile journal remained after exit; archived journal/current bytes into
A1m-profile-recovery, validated all four backup hashes, restored their exact bytes,
then removed only the journal. No later desktop settings were present in current
configs: they still carried1644x1764 VR profile. No game/helper process running.

Next single JJ launch: ordinary gameplay at the same opening scene for30seconds,
then normal exit, no comparison/crash wait. Deeper allocations plus pipeline count
will decide whether growth is pipeline compilation/cache or another driver/engine
path. No autonomous launch/kill or changes to City/Origins. A1m is diagnostic,
not a claimed OOM/FPS fix.

## A1l installed: allocation-source evidence; crash/FPS still open

JJ confirms A1k remains40-50FPS; third DLSS transfer comparison coincided with
E_OUTOFMEMORY. Preserved complete run before fixtures: re/evidence/A1k-crash/.
LIVE: first comparison02:22:56; VA largest free already fell2045->672MiB
before it. At02:24:14 game Present returned8007000e; free158.1MiB,
largest21.4MiB, private2473.4MiB, reserved1077.2MiB. Local GPU~1794MiB
against11226MiB budget. Wrapper count3 throughout; helper texture set gen1,
two feature creations only. Address-space depletion precedes comparisons;
this is not evidence of exhausted VRAM or proof that comparison switching leaks.
Test1 safe50.25/overlap55.38/safe67.99; test2 safe42.42/overlap49.50/safe43.25;
test3 safe42.25 then crash during overlap. Scene stability uncontrolled, so
these do not justify a clean speedup claim. Frame rate remains unresolved.

Looked up again: playbook ch06 measurement/environment, ch19 lifetimes/budgets,
FAIL-PACK-005 (target already LAA), commit b955f2244f7c5d5f3b3e5f403b1b6721a3362423.
Microsoft D3D9On12 issues72/90 are similar reports only, not applicable fixes:
https://github.com/microsoft/D3D9On12/issues/72
https://github.com/microsoft/D3D9On12/issues/90

SOURCE A1l adds bounded NtAllocateVirtualMemory current-process MEM_RESERVE
tracing for >=1MiB reservations,128 records/12 stack PCs, thread-local recursion
guard, no heap/log/symbol calls inside detour. Module offsets emitted at heartbeat.
Dropped-record counts and address-reuse caveat explicit. This covers that API,
not every mapping/allocator; absence of stacks does not exonerate a component.
No quality/performance setting changes and NO claimed retail fix.

OFFLINE:9 checks pass including real2MiB reserve/stack/release test; optional
AA_DLSS_SOAK fixture completed8192 D3D9 Presents/transfers (freeVA1479.2MiB
through all samples) and4097 paired NGX completions (private memory245.6->247.4
then223.2MiB), correct eye colours/ownership/timeouts. Tiny synthetic images,
no actual engine/XR runtime; does not prove full-resolution game stability.
Saved A1l-ctest-soak.txt and A1l-build-checks.json. Installed17 owned files,
five current protected hashes unchanged. Source IDs and manifest A1l.

NEXT single JJ launch: ~30s ordinary same-scene gameplay, normal exit. Do not
repeat comparison or wait for crash. Read reservation call stacks and budget
trend before any further speculative fix. No autonomous launch/kill, no City/
Origins edits. Ground/HUD/AFW/head-pose and performance acceptance remain open.

## A1k installed 02:19 Sydney, 2026-10-12

HEADSET/user evidence: A1j did not improve gameplay (~50 FPS, blank wall90),
while desktop4K in the same section averages230. Crash screenshot reports
E_OUTOFMEMORY at D3D9Viewport.cpp:202. Neither the remaining frame-rate gap nor
the retail crash is resolved/proven fixed by this build. Target already LAA.
LIVE logs preserved in re/evidence/A1j-crash-performance: sceneCPU~5ms/eye,
GPU elapsed~4.5-5ms/eye, DLSS helper~2.5-3ms/eye; classifier cost is small.

SOURCE fixes: removed duplicate Native radio control in AFW details and gave
those controls their own ID scope; unique Floating screen selector ID. Bounded
8-entry canonical-resource/full-description wrapped-texture cache replaces
per-capture wrapper creation. Acquire/release/fences/ReturnUnderlyingResource
remain per capture; Reset releases cached wrappers and flushes deferred cleanup.
AFW now performs physical desktop Present every rendered frame, not every other
frame. Added process private/free/largest-free/reserved VA and video-memory budget
telemetry, including failed Present HRESULT. This does not identify the original
failed allocation or prove that wrapper churn caused it.

OFFLINE: all9 checks pass (re/evidence/A1k-ctest.txt), including actual production
ImGui gamepad navigation from first to last controls and back in Native/AFW,
real GPU transport/reset/depth/HUD/DLSS checks. 4096 transfers create zero new
wrappers after warmup. Private memory238.5 ->269.7 ->270.1MiB across two2048
batches; VA free1509.9 ->1479.3 ->1479.3MiB. Synthetic trend only, not retail
memory stability or headset performance acceptance. Build audit:
re/evidence/A1k-build-checks.json;17 owned and5 current protected hashes verified.

References reused: playbook b955f2244f7c5d5f3b3e5f403b1b6721a3362423,
FAIL-PACK-005/ch06 #laa-address-space, ch19 canonical identity/full resource
identity and budget ledgers; framework04 interop/reset, framework12 section6,
VR_HUD_GUIDE and PACKAGING_GUIDE. Microsoft wrapped-resource lifetime guidance:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-11-on-12
Beren5556 repo is BioShock1/2 Remastered downloads/docs, not Infinite; source
private. Public performance notes suggest overlapping DLSS, guarded depth reuse,
and headset submission before mirror. Asylum already overlaps native DLSS;
these are leads, not transferable offsets or evidence of a speed fix here.
https://github.com/Beren5556/BioShock-VR-DLSS-Downloads/releases/tag/v0.2.17-en
Saved public documents: re/evidence/beren5556-v0.2.17/.

Next SINGLE JJ launch: use the usual VR launcher, visit the same opening scene,
observe native FPS, enable AFW and navigate from first to last panel options and
back, then return to native. Memory/scene budgets log automatically. Save stereo
history if artifacts recur; exit normally. No autonomous game launch or kill.

# Asylum per-eye DLSS

## A1j current - gameplay scene overhead at preserved quality

Installed00:47 SydneyOct12;8 checks PASS,17 owned/five CURRENT protected hashes
verified. HEADSET A1i menu now reaches90, early gameplay remains<60. LIVE
native overlap1/sync3/no timeouts; Balanced1322x1478->2284x2448 unchanged.
Physical Present~.3ms/pair, helperGPU~2.7ms/eye; these do not explain the whole
17-19ms game frame. A1j caches LIVE graphics state until setters/StateBlock
Apply/eye/reset invalidate it, preserving depth/HUD after DLSS and AFW.
Real9On12 regression confirms512->1 binding refreshes per256 unchanged draws,
correct restored live state, preserved geometry and all NGX/fallback/history
checks. Classifier CPU falls, total tiny-fixture draw submission is noisy;
retail FPS gain not established. New nonblocking per-eye scene CPU/GPU elapsed
excludes final colour/XR/DLSS transfer; not pure GPU busy time.
FRAME RATE Compare scene tracking speed runs8s OFF/ON/OFF at same saved mode/
resolution, automatic FPS/captures and setting restoration. One grouped JJ
launch at same gameplay spot determines gain and next bottleneck. A1i menu
cap improvement confirmed; A1j gameplay target pending. See RE_NOTES A1j.

Historical A1i checkpoint follows.

## A1i current - isolate remaining cap/desktop scheduler

A1h LIVE confirms native overlap sync3, no fence/completion timeouts;
menu61-62pairs/s at same Balanced input/output, gameplay~50-60 HEADSET.
A1i installed00:22 SydneyOct12 covers previously omitted DefaultEngine smooth
TRUE/max62 in reversible graphics profile, plus generated files. SmoothingOFF,
max300 fallback; owned D3D9 presentationIMMEDIATE. DLSS overlap implementation,
mode and resolution unchanged.8 checks PASS,17 owned/five protected verified.
Startup config snapshots/named CPU timings now log remaining budget; next
same-scene launch establishes actual cap removal/FPS gain. Read RE_NOTES A1i.

Historical A1h checkpoint follows.

## A1h - overlap native DLSS with RIGHT rendering

Installed00:07 Sydney2026-10-12, eight checks pass. JJ confirms A1g image
quality improved; live baseline33-37pairs/s at Balanced1322x1478 ->2284x2448.
Native overlap defaults ON with safe fallback for unsupported shared fences,
no held acquired image and AFW. Client queues LEFT, engine draws RIGHT while
helper runs NGX, then one bounded pair wait; final acquired LEFT updated before
RIGHT release/capture. Both eye replies must validate, stock pair rollback
preserved, final HUD and recorder snapshot ordered after NGX. Timeout rejects
before GPU work. Source/method lookup and evidence limits in RE_NOTES A1h.

GPU:17 actual overlap pairs with distinct red/blue eye outputs, valid histories,
zero fence timeouts; missing RIGHT, early LEFT finish, reset/mode-off, no held
XR image and helper timeout fail-closed regressions pass. Recorder decodes
final deferred LEFT green rather than initial stock red. All8 tests pass.
Actual headset/retail FPS gain remains pending. Saved Balanced/resolution kept.
Overlay 'Compare DLSS transfer speed' runs8s safe/overlap/safe at identical
mode/resolution and logs completed pair FPS before automatic still/history
capture. Stay still after30s warm-up. Restores all settings. See BUILD_PLAN.

Historical A1g checkpoint follows.

## A1g - correct physical depth and motion registration

Installed23:34 Sydney; eight checks pass. Highest priority: distant shimmer/
low detail. See RE_NOTES A1g lookup (FAIL-PERF-017, chapter19#dlss-in-vr,
playbook b955f224) and actual user-capture audit. Five depth previews were
uniform red255. New code freezes geometry depth BEFORE clear/rebind, retains
raw-DS projection rather than shader-unpack inversion, and calculates relative
camera motion in double precision. GPU geometry .25/.75 survives clear1;
large-world physical-forward motion matches an independent analytical value.
Raw depth capture and pose-age logging let the next launch verify actual
retail inputs. Headset detail/ground/AFW/jitter are still unconfirmed.

Historical A1f checkpoint follows.

## A1f current ? genuine mode-sized input and fixed output

Looked up: RE_NOTES/BUILD_PLAN A1f record City D1/D2, Origins O1C and playbook
PERF-006/007/chapter19 at b955f224. A1e retail NGX was real but reconstructed
1838x2050 to2756x3074 before resampling to2284x2448, with a full-sized game
source. A1f lowers engine input by mode, compensates the measured centred crop,
and targets the chosen output resolution (default XR recommendation), using
existing independent NGX features/shared phase/HUD-after/atomic rollback.
Mild scene texture bias preserves original sampler reads, excluding HUD/post.
Cleared-depth camera motion now uses the far direction while NGX sees raw depth.
Quality/DLAA and AFW real GPU histories, sky motion and rollback checks pass;
17 installed files verified. No full object velocities or NGX optimum-query
claim. FPS/black-artifact/headset acceptance needs grouped next launch.

Historical A1e follows.

## A1e current - increased game resolution and AFW

Looked up: BUILD_PLAN/RE_NOTES A1e cite FAIL-PERF-020 and playbook19 composed
reprojection/per-eye history at b955f224, framework06/10 and City's AFW method.
A1d LIVE now proves retail Quality:7034 evaluations, zero depth/fallback/timeouts;
blur report corresponds to996x1112 temporal input, not a fake DLSS copy.

A1e launcher defaults to1.00x actual runtime recommendation, with saved0.6-1.8x
scale. Logical game client/render size is independent of the fitted real desktop
window. Lighting-compatible input still crops the centred enclosure; actual
sizes are shown. DLSS outputs the selected ratio up to8192 before XR resampling,
allowing sharper full-resolution/supersampled input instead of desktop capping.
This remains a provisional ratio, not an NGX optimum query claim.

AFW draws one eye and warps the other from the same frame's depth; NGX features
retain separate histories across alternating single-eye generations. Native
keeps atomic stock-pair fallback. The HUD is added after BOTH DLSS and AFW.
All eight GPU/static checks pass, including actual2284x2448 source in a smaller
window, DLAA/Quality paired evaluations and four AFW single-eye evaluations.
Retail high-resolution scene/depth coverage, AFW quality and performance remain
pending the grouped BUILD_PLAN A1e run. Camera-only motion can smear animation;
transparent/reflected content can disagree in an AFW-synthesized eye.

Historical A1d implementation/limits below describe the preceding installed
build, including its now-superseded desktop source and XR output caps.

## A1d current implementation - supersedes the foundation below

Looked up: RE_NOTES A1d records playbook19 DLSS, playbook14 depth/shadow
ownership at b955f2244f7c5d5f3b3e5f403b1b6721a3362423; framework10; City
camera-motion/shared-helper source; AKN legacy-lighting source; this target's
matrix/depth-convention decompile and Microsoft interop resource rules.

IMPLEMENTED: D3D9 scene-sized depth is captured before UI/private stencil,
transported through fenced9On12/11On12 and matched to native camera eye and
generation. Separate keyed D3D11 input/depth/motion/output textures hand pixels
to Asylum's x64 NGX helper on the matching adapter. Installer owns the helper
and nvngx_dlss.dll310.9.1. Both eyes have independent NGX features/history;
current/previous matrices reconstruct camera motion from real depth. Both eyes
share one32-phase Halton sample. Modes/recenter/cuts/resize/untracked Present
and failed pairs reset histories. A RIGHT failure restores BOTH stock eyes.
HUD is composited afterward. Actual success/evaluatedMode is mandatory;
Quality cannot silently become Balanced. Default Quality request and mode
choices persist in %LOCALAPPDATA%/Batman_Asylum/aa_rendering.ini. The panel
shows actual evaluations and input/output sizes, retaining stock on failure.

OFFLINE GPU: real D24S8 .25/.75 depth; actual x86/x64 NGX DLAA64->64 and
Quality64->96 evaluations BOTH eyes; depth readback .750000, camera motion
-10.0391 X versus -10.0400 expected, zero Y despite declared jitter; missing
RIGHT depth rejection/LEFT rollback. All eight checks pass. See A1d-ctest.txt
and A1d-dlss-gpu.log. These are real GPU tests, not retail/headset acceptance.

LIMITS: camera motion only, so animated objects can smear. Source/client
1238x1328 remains desktop-fit; centred enclosure cropping further reduces
temporal input. Output uses provisional mode ratios (Quality about1.5x input),
capped to XR size before final XR resampling. This is not full2284x2448 DLSS
Quality or an NGX optimal-size claim. Stock AA remains. A higher-detail route
needs proven offscreen source/client separation plus NGX optimum queries;
never stretch an axis or restore A1a's clipped window.

NEXT: one grouped BUILD_PLAN A1d launch verifies corrected tower shading,
actual retail depth coverage/convention, both-eye evaluations, wires/edges,
animation, disocclusions, detective/reflection modes and sustained pacing.
Fix inputs from those captures before object-motion/resolution/performance work.
No game was launched or killed by the agent.

## Historical A1c foundation and original acceptance plan

Looked up: VR_ESSENTIALS section9; playbook docs/19-d3d12-and-performance.md
#dlss-in-vr, checkout b955f2244f7c5d5f3b3e5f403b1b6721a3362423; framework
guide10 temporal histories; Batman City DLSS_PLAN.md D0/D1 handoff and AKN
DLSS_PLAN.md. These establish ordering/history requirements, not Asylum depth
offsets, shader IDs, matrix conventions or usable retail inputs.

## Current concrete foundation

A1c implements D3D9 scene/UI separation, a fenced GPU-only 9On12 -> 11On12
HUD transfer, and composition after the scene blit in bridge::compose_eye.
The real GPU fixture covers those production paths. This provides the correct
seam for replacing the scene blit with per-eye DLSS while adding the crisp HUD
afterwards. Same-frame native stereo already supplies one pose generation and
independent right-eye engine history; JJ confirms scene stereo on A1b.

helper64/aa_dlss64.cpp builds as a separate x64 program because the game is x86.
It has two NGX features, isolated Asylum IPC names, copy mode, evaluation
status and fenced/finished handoff inherited from City/Origins. It is currently
neither installed nor active. No DLSS quality label is exposed as if it worked.

## Implementation and acceptance order

1. Accept A1c in one JJ launch: controller logo cancellation, upright eye-level
   movie and fusable title/menu/gameplay text; preserve hud_sheet + composed
   captures. Verify the actual retail scene/UI boundary, not just counters.
2. Find and tag each native scene's D3D9 depth surface before UI/private stencil.
   Transport compatible depth through owned 9On12/D3D12 resources into readable
   D3D11 depth using the existing queue/fence discipline. A GPU fixture must
   distinguish near/far values and verify reset/return ownership; runtime
   evidence must establish the target depth convention and real scene coverage.
   Never pass UI stencil or a zero-filled substitute as scene depth.
3. Publish exact current/previous per-eye view/projection matrices with the
   render ticket. Generate camera motion from scene depth initially; explicitly
   document missing object animation motion. Measure direction, units and
   disocclusion against a static scene and camera motion. Add actual game object
   motion if a usable lane is found. Reject unrelated views/generations.
4. Apply the same Halton phase to both native projections, retaining independent
   per-eye temporal histories. Declare actual pixel jitter, motion scale,
   inverted depth and exposure consistently to NGX; do not disable stock AA
   until successful evaluations are accepted. Reset both histories after cuts,
   recenter, menu/game changes, resize and invalidated pairs.
5. Add the x86 client and first prove D0 shared-texture copy for both real eyes.
   Keep resource handoff serials/generation, GPU completion and adapter identity
   explicit. Then D1 replaces the HUD-free scene blit with actual NGX output;
   bridge::compose_eye adds the existing transparent HUD at XR output size.
   Evaluation failure returns to stock rendering/AA and reports the reason.
6. Expose Quality/Balanced/Performance/DLAA only with actual supported dimensions
   and success status, persistent controls and captures. Record source/output
   size, mode, evaluatedMode, reset/jitter, GPU time and both-eye serials. Validate
   wires/edges, animated characters, detective mode, reflective surfaces,
   disocclusions, menu/HUD sharpness and a long run in one grouped headset test.

## Resolution constraint requiring an explicit choice

The current working source is1238x1328, uniformly blitted to2284x2448 XR.
This is a desktop-fit source, not a DLSS Quality resolution. Quality at a
nominal two-thirds ratio would need approximately1523x1632, exceeding the
current1392-high desktop work area. Use NGX's returned optimum dimensions,
not a hardcoded quality label. A larger source needs a separately proven
offscreen/client-size route; otherwise cap the DLSS output to a supported
source size and report the actual dimensions. Do not reintroduce A1a's clipped
Windows client or stretch one axis to advertise higher resolution.

Active DLSS remains a separate milestone after A1c headset acceptance; neither
the existing helper nor the HUD GPU check proves retail DLSS image quality.
