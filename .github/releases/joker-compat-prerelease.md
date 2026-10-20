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

## Apply the package

1. Keep the official VR mod installed and make sure `asylumvr_manifest.json` is intact.
2. Close the game and extract the ZIP.
3. In PowerShell, from the extracted folder, run:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Apply-JokerCompatBuild.ps1 -GameBinaries "F:\SteamLibrary\steamapps\common\Batman Arkham Asylum GOTY\Binaries"
   ```

   Change the path if the game is installed in a different Steam library.
4. Launch the **Batman Asylum VR** shortcut after starting your PC VR software.

The script backs up the replaced DLLs, launcher script, and manifest under `%LOCALAPPDATA%\Batman_Asylum\joker-compat-backup\<timestamp>`. It refuses to update the game if an existing manifest-listed mod file is missing or altered.

## Source

- Fork: https://github.com/Godoy301/Batman-Arkham-Asylum-VR
- Branch: https://github.com/Godoy301/Batman-Arkham-Asylum-VR/tree/compat/joker-dlc-patcher
- Pull request: https://github.com/Godoy301/Batman-Arkham-Asylum-VR/pull/1
