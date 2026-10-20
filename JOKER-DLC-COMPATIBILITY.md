# Joker DLC Patcher compatibility branch

This experimental branch targets one known modified executable for Batman: Arkham Asylum GOTY (Steam).

## Accepted executable SHA-256 values

- Stock Steam GOTY: `4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028`
- Known Joker DLC Patcher variant supplied for this investigation: `ed65ce3c52baca7adaea00cb0e5def3d8a78bd05f7ceacd68a68462e92d8dab2`

No other modified executables are accepted. The installer, launcher, and native DLL use the same explicit allowlist.

## Root cause addressed

The install/launch PowerShell checks were only the first gate. `src/core/framework/dllmain.cpp` independently hashed the host executable and skipped `bvr::framework::init()` unless the stock hash matched. In that path the DLL set identity to 2; `AA_CreateD3D9` then returned the system Direct3D factory, allowing the game to continue in flat mode without initializing the VR framework.

This branch accepts the one known Joker-patched hash and logs which supported executable was accepted after framework initialization. A diagnostic was also added to the stock-D3D fallback path so it is easier to tell whether bridge readiness or host identity failed.

## Scope and safety

The two differences in the supplied executables are at file offsets `0x012153C6` and `0x012153CB`, in the Joker DLC flag routine. This change does not patch the game executable. Unknown host executable hashes remain rejected, and the launcher's checks for altered mod files remain enabled.

## Easy installation

For the release ZIP, double-click `Install-Joker-Compatibility.bat`, approve the administrator prompt if Windows asks, then select the game's **Binaries** folder (the folder containing `ShippingPC-BmGame.exe`). Confirm the path; the helper validates the existing mod manifest and executable hash, backs up the replaced files, installs the compatibility binaries, and updates the manifest. It does not modify the game's executable.

For manual installation, run `Apply-JokerCompatBuild.ps1 -GameBinaries "<path to Binaries>"` from the extracted package folder. Keep the BAT, PowerShell scripts, and payload files together.

Backups are written under `%LOCALAPPDATA%\Batman_Asylum\joker-compat-backup\<timestamp>`.

## Validation status

- Windows Win32 build: passed in GitHub Actions.
- Selected deterministic tests: passed in GitHub Actions.
- PowerShell parser checks: passed in GitHub Actions.
- Real headset test: the maintainer confirmed that the supplied test payload successfully launches the Joker DLC-patched executable in VR.

This is now a working compatibility build for the exact Joker-patched executable hash listed above, based on the reported hardware test. It remains intentionally limited to that specific patched hash; other patches or game builds are not covered. A public release should include the tested payload and these limitations in its release notes.
