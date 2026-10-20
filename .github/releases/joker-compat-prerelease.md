# Batman Asylum VR — Joker DLC Patcher Compatibility

This is a **pre-release compatibility overlay**, not a standalone copy of the entire mod. Install the official Batman Asylum VR mod first, then apply this ZIP using `Apply-JokerCompatBuild.ps1`.

## Verified target

- Steam GOTY original SHA-256: `4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028`
- Joker DLC Patcher variant SHA-256: `ed65ce3c52baca7adaea00cb0e5def3d8a78bd05f7ceacd68a68462e92d8dab2`

Only these exact executable hashes are accepted. The patch does not modify `ShippingPC-BmGame.exe`.

## What the compatibility update does

The VR DLL had its own SHA-256 gate that rejected the Joker-patched executable and skipped framework initialization. The game could then open in flat mode even though the VR DLL was loaded. This build permits the single known Joker-patched hash as well as the stock Steam GOTY hash, and adds diagnostics for the Direct3D fallback path.

## Validation

- Win32 build passed in GitHub Actions.
- Selected deterministic tests passed.
- PowerShell syntax checks passed.
- The maintainer confirmed that this compatibility package starts the Joker-patched game in VR on their PC.

Compatibility is verified for the exact hash above; other mods, patches, or game builds are not covered.

## Easy installation

1. Keep the official Batman Asylum VR mod installed.
2. Close the game, download this ZIP, and extract **all** files to a regular folder.
3. Double-click `Install-Joker-Compatibility.bat`.
4. Approve the Windows administrator prompt if shown.
5. In the folder picker, select the game's **Binaries** folder — the folder containing `ShippingPC-BmGame.exe`. The installer checks that the VR mod is installed there before continuing.
6. Confirm the selected path. The helper backs up the files, applies the compatibility binaries, and refreshes the mod manifest. It does not modify `ShippingPC-BmGame.exe`.
7. Start your PC VR software, then launch the game using the existing **Batman Asylum VR** shortcut.

If you prefer, `Apply-JokerCompatBuild.ps1` can still be run manually with `-GameBinaries "<path to Binaries>"`. Do not move only the BAT file out of the ZIP; it needs the companion scripts and binaries beside it.

The backup is stored under `%LOCALAPPDATA%\Batman_Asylum\joker-compat-backup\<timestamp>`. The installer refuses to proceed if the executable hash is not one of the two supported hashes or if a file in the current mod manifest is missing or changed.

## Source

- Fork: https://github.com/Godoy301/Batman-Arkham-Asylum-VR
- Branch: https://github.com/Godoy301/Batman-Arkham-Asylum-VR/tree/compat/joker-dlc-patcher
- Pull request: https://github.com/Godoy301/Batman-Arkham-Asylum-VR/pull/1
