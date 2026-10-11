param([string]$GameDir='E:/Games/Steam/steamapps/common/Batman Arkham Asylum GOTY')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$bin=[IO.Path]::GetFullPath((Join-Path $GameDir 'Binaries'))
if(Get-Process 'ShippingPC-BmGame' -ErrorAction SilentlyContinue) {throw 'Close Asylum before installation.'}
if((Get-FileHash -LiteralPath (Join-Path $bin 'ShippingPC-BmGame.exe')).Hash -ne '4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028') {throw 'Unsupported Asylum executable.'}
if(Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Batman_Asylum/profile-active.json')) {throw 'An Asylum graphics profile is still active.'}
$manifestPath=Join-Path $bin 'asylumvr_manifest.json'
$previous=$null
if(Test-Path -LiteralPath $manifestPath) {$previous=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json}
$sources=[ordered]@{
 'd3d9.dll'='build/Release/d3d9.dll'
 'xinput1_3.dll'='build/Release/xinput1_3.dll'
 'batman_asylum.dll'='build/Release/batman_asylum.dll'
 'aa_xr_config32.exe'='build/Release/aa_xr_config32.exe'
 'AsylumVR/Launch-VR.ps1'='tools/Launch-VR.ps1'
 'AsylumVR/Graphics-Profile.ps1'='tools/Graphics-Profile.ps1'
 'AsylumVR/Uninstall-VR.ps1'='tools/Uninstall-VR.ps1'
 'AsylumVR/LICENSE-bioshock-vr.txt'='LICENSE-bioshock-vr.txt'
 'AsylumVR/LICENSE-luma-framework.md'='LICENSE-luma-framework.md'
 'AsylumVR/LICENSE-MinHook.txt'='third_party/minhook/LICENSE.txt'
 'AsylumVR/LICENSE-ImGui.txt'='third_party/imgui/LICENSE.txt'
 'AsylumVR/LICENSE-OpenXR.txt'='third_party/OpenXR-SDK/LICENSE'
 'Batman Asylum VR.bat'='tools/Batman Asylum VR.bat'
 'Batman Asylum VR (SteamVR).bat'='tools/Batman Asylum VR (SteamVR).bat'
}
foreach($name in $sources.Keys) {
 if(!(Test-Path -LiteralPath (Join-Path $project $sources[$name]))) {throw "Missing source: $name"}
 $dest=Join-Path $bin $name
 if(Test-Path -LiteralPath $dest) {
  $owned=$previous.files | Where-Object name -EQ $name
  if(!$owned -or (Get-FileHash -LiteralPath $dest).Hash -ne $owned.sha256) {throw "Existing file is not our unchanged file: $dest"}
 }
}
$backup=Join-Path $project ('re/backups/install-'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
. (Join-Path $PSScriptRoot 'Graphics-Profile.ps1')
$baseline=@();$i=0
foreach($path in (Get-AsylumConfigPaths $GameDir)) {
 $dest=Join-Path $backup ("config-$i-"+[IO.Path]::GetFileName($path));$i++
 Copy-Item -LiteralPath $path -Destination $dest
 $baseline+=@{path=$path;backup=$dest;sha256=(Get-FileHash -LiteralPath $path).Hash}
}
$baseline | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'graphics.json') -Encoding UTF8
if($previous) {
 foreach($record in $previous.files) {
  $dest=Join-Path $backup $record.name;New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $bin $record.name) -Destination $dest
 }
 Copy-Item -LiteralPath $manifestPath -Destination $backup
}
$records=@()
foreach($name in $sources.Keys) {
 $src=Join-Path $project $sources[$name];$dest=Join-Path $bin $name
 New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force | Out-Null
 Copy-Item -LiteralPath $src -Destination $dest
 $hash=(Get-FileHash -LiteralPath $dest).Hash
 if($hash -ne (Get-FileHash -LiteralPath $src).Hash) {throw "Install hash mismatch: $name"}
 $records+=@{name=$name;sha256=$hash;bytes=(Get-Item -LiteralPath $dest).Length}
}
foreach($record in $baseline) {if((Get-FileHash -LiteralPath $record.path).Hash -ne $record.sha256) {throw 'Installation changed game settings.'}}
@{version='0.1.0-A0a';game=$bin;installed=(Get-Date -Format o);backup=$backup;files=$records;capabilities='Game-frame graphics bridge + panel/input/XR core; native camera, AFW, DLSS not yet active'} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $project 're/evidence/installed-build.json') -Force
Write-Host "Asylum A0a installed and hash-verified: $($records.Count) owned files."
