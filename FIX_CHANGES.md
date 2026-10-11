# External file changes

2026-10-11: development build 0.1.0-A0a installed in
`E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY/Binaries`.

Added 14 manifest-owned files: `d3d9.dll`, `xinput1_3.dll`,
`batman_asylum.dll`, `aa_xr_config32.exe`, the default/SteamVR launchers,
three scripts under `AsylumVR/`, and five dependency license notices.
`asylumvr_manifest.json` records their hashes, sizes and backup location.
The current manifest is copied to `re/evidence/installed-build.json`; all 14
installed file hashes were independently verified after the final deployment.

No executable, game package, movie or save was edited. Asylum's install and
player graphics INIs remain byte-identical to the pre-install receipt.
Dedicated VR launch temporarily applies headset-sized resolution, disables DoF,
motion blur, smoothing and VSync, and restores original configuration bytes
after exit. An interrupted run leaves a restoration journal for the next launch
or uninstaller. This configuration policy does not yet prove that every optical
post-process is disabled inside the running engine.

Backups of baseline configuration and replaced owned development files are in
`re/backups/install-*`; their exact location is recorded in each manifest.
Uninstall validates all owned hashes/paths before removing files. It refuses to
overwrite or remove unknown injectors or user-modified owned files.

Asylum installation does not write Origins files. Origins is the accepted
behavior reference. `A0a-before-install.json` records the current protected
pre-deployment bytes; each entry was checked after installing A0a. The older
`A0-before-install.json` is historical, and subsequent game runs/external
Origins updates must not be confused with the Asylum install changing files.

A0a replaces the initial A0 DLL to fix CreateDevice recursion/desktop fallback
seen during JJ's first launches. No game was launched or killed by the agent.
