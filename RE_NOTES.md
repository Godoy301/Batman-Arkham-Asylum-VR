# Asylum rendering evidence

Read ASYLUM_PLAN.md for the target hash and reference routes. Playbook RE-01 and
PORT-00/01/02, chapter 10 ownership/FPU routes and chapter 06 "Crash-dump
workflow", checkout b955f2244f7c5d5f3b3e5f403b1b6721a3362423.

## Draw route — static executable and one live viewport type

| Address / field | Evidence | Boundary |
|---|---|---|
| 0x00ccae50 | Uses command B Begin/End, viewport+0x20 client and client vtable+8 Draw; decompiled ordinary FViewport::Draw candidate | Game-thread caller and native stereo behavior not yet observed |
| 0x00ccae50 entry / 0x00ccb472 return | SEH prologue, no incoming stack argument inferred, bare `ret` at exit | Different ABI from Origins' viewport draw; do not reuse its extra stack argument |
| 0x006a9310 | `mov ecx,[esp+4]; call 0x00ccae50; ret 4` | Confirms ECX receiver dispatch for this call; wrapper's own one stack argument is not the callee ABI |
| viewport+0x20 client | Client virtual slot+8 receives viewport and local canvas | Need the live client's object/vtable and Draw entry before following CalcSceneView |
| command B 0x00c96cd0 | Loaded 16-byte signature matched; runs in JJ's A0 desktop fallback, returns original command size 8 | Queued render-thread stack cannot recover the enqueue-site caller |
| viewport vtable 0x0212c380 | LIVE command B logged RVA 0x01d2c380 | Object address was transient; later external read failed and yielded no field evidence |
| vtable+0x54 -> 0x00cc29c0 | Static contents of the observed live vtable, command executor's called slot | Viewport begin method, not scene constructor or game-thread Draw |
| 0x00cc2310 / 0x00cc2ab0 | Tiled screenshot / capture-readback roots | Do not select them as ordinary gameplay draw roots |

`ordinary-draw-call-leads.json` lists bounded direct relative-call matches and
padding-derived parent candidates. It includes the tiny receiver wrapper above
and calls from gameplay/editor-like functions; matches are leads, not live caller
proof. Ghidra's isolated focused database supplies control-flow evidence only.

Next: establish stable game Present/XR delivery with A0a, identify the live
viewport client and Draw dispatch, then trace its Asylum CalcSceneView and view
construction arguments. Render-thread command observation alone does not prove
that drawing twice is safe or gives one simulation step. Native implementation
must own both eye views/history, preserve one located XR pose per pair and gate
submission on both completed images. Projection and shader reconstruction
companions need Asylum-specific evidence. Never copy donor offsets/layouts.

## First launch crash and correction

A0 initialized VirtualDesktopXR, but its global CreateDevice detour recursed
after two system/9On12 factories. A matched local crash dump contains 8,699
returns into the detour (DLL+0x2f933), then faults in CRT stack probing
(DLL+0xb67d7). Original trampoline memory was not included in the dump, so its
exact internal branch target is unknown. A second A0 launch failed to hook the
factory and fell back desktop; JJ confirmed no headset or F10/controller panel.

A0a uses per-factory IDirect3D9 COM forwarding with original API arguments,
HRESULTs and independent ownership. Its fixture creates real GPU devices through
two wrapped factories and checks COM identity, managed textures, eight exact
BGRA-to-RGBA transfers, resource return and reset/resize. Three CTests pass; A0a
is installed and hash-verified. Game/headset result remains pending.
