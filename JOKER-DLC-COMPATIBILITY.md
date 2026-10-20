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

## Apply the experimental test payload

This payload updates an existing installation; it is not a complete standalone mod package. Install the official Batman Asylum VR mod first, and make sure `asylumvr_manifest.json` is intact. The existing x64 DLSS helper is reused.

1. Close Batman: Arkham Asylum.
2. Extract the CI artifact ZIP to a folder.
3. Open PowerShell in that folder and run the command below, adjusting the path to your game's `Binaries` directory:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\\Apply-JokerCompatBuild.ps1 -GameBinaries "F:\\SteamLibrary\\steamapps\\common\\Batman Arkham Asylum GOTY\\Binaries"
   ```

4. Start your PC VR software and launch the game using the existing **Batman Asylum VR** shortcut.

The helper verifies every file listed in the installed mod manifest before replacing anything. It backs up the four core binaries, the VR launcher script, and the manifest to `%LOCALAPPDATA%\\Batman_Asylum\\joker-compat-backup\\<timestamp>`, then refreshes the manifest hashes for the files it replaces. It does not modify `ShippingPC-BmGame.exe`.

To roll back, close the game and copy the corresponding backed-up files and `asylumvr_manifest.json` from the reported backup directory back to the game folder.

## Validation status

This is a development compatibility branch, not a tested release. The Win32 build and deterministic tests must pass. The generated DLL must then be tested with both stock and Joker-patched executables on a PC with a connected headset before creating a public release.
