param([switch]$SteamVR)
$ErrorActionPreference='Stop'
$bin=Split-Path $PSScriptRoot -Parent
$game=Split-Path $bin -Parent
$exe=Join-Path $bin 'ShippingPC-BmGame.exe'
$data=Join-Path $env:LOCALAPPDATA 'Batman_Asylum'
$journal=Join-Path $data 'profile-active.json'
$manifest=Get-Content -LiteralPath (Join-Path $bin 'asylumvr_manifest.json') -Raw | ConvertFrom-Json
foreach($record in $manifest.files) {
 if((Get-FileHash -LiteralPath (Join-Path $bin $record.name)).Hash -ne $record.sha256) {throw "Installed file changed: $($record.name)"}
}
if((Get-FileHash -LiteralPath $exe).Hash -ne '4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028') {throw 'Unsupported Asylum executable.'}
$games=@(Get-Process -ErrorAction SilentlyContinue | Where-Object ProcessName -Match '^(ShippingPC-BmGame|BatmanOrigins|BatmanAC|BatmanAK|ACUnity|sekiro|DarkSoulsIII|armoredcore6|starwarsbattlefrontii|BioShockInfinite|BioshockHD)$')
if($games.Count) {throw "Close the running game first: $($games.ProcessName -join ', ')"}
New-Item -ItemType Directory -Path $data -Force | Out-Null
. (Join-Path $PSScriptRoot 'Graphics-Profile.ps1')
if(Test-Path -LiteralPath $journal) {Restore-AsylumProfile (Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json);Remove-Item -LiteralPath $journal}
if($SteamVR) {
 $steam=(Get-ItemProperty 'HKCU:/SOFTWARE/Valve/Steam' -ErrorAction SilentlyContinue).SteamPath
 $candidates=@((Join-Path (Split-Path $game -Parent) 'SteamVR/steamxr_win32.json'))
 if($steam) {$candidates+=(Join-Path $steam 'steamapps/common/SteamVR/steamxr_win32.json')}
 $runtime=$candidates | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
 if(!$runtime) {throw 'SteamVR 32-bit OpenXR runtime not found.'}
 $env:XR_RUNTIME_JSON=$runtime
}
$recommend=& (Join-Path $bin 'aa_xr_config32.exe')
if($LASTEXITCODE -ne 0) {throw 'Headset unavailable. Connect Virtual Desktop and enable VR first.'}
$size=$recommend | ConvertFrom-Json
$w=[int]$size.width;$h=[int]$size.height
if($w -lt 512 -or $h -lt 512 -or $w -gt 8192 -or $h -gt 8192) {throw 'Headset render recommendation is out of bounds.'}
$backup=Join-Path $data ('profiles/'+(Get-Date -Format yyyyMMdd_HHmmss_fff))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$files=@();$i=0
foreach($path in (Get-AsylumConfigPaths $game)) {
 if(!(Test-Path -LiteralPath $path)) {throw "Game configuration absent: $path"}
 $copy=Join-Path $backup ("$i-"+[IO.Path]::GetFileName($path));$i++
 [IO.File]::WriteAllBytes($copy,[IO.File]::ReadAllBytes($path))
 $files+=@{path=$path;backup=$copy;sha256=(Get-FileHash -LiteralPath $path).Hash}
}
$profile=@{files=$files;runtime=$size.runtime;render=@($w,$h);build=$manifest.version}
$profile | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $journal -Encoding UTF8
try {
 foreach($path in $files.path) {Set-AsylumVRProfile $path $w $h}
 Write-Host "Asylum $($manifest.version): game-frame bridge, $($size.runtime), ${w}x${h}. F10 or both sticks held opens the panel."
 Write-Host 'Native camera integration is still in development in this first build.'
 $process=Start-Process -FilePath $exe -WorkingDirectory $bin -ArgumentList @('-bavr','-windowed',"ResX=$w","ResY=$h","-AAOutputX=$w","-AAOutputY=$h") -PassThru
 $process.WaitForExit()
 while(Get-Process 'ShippingPC-BmGame' -ErrorAction SilentlyContinue) {Start-Sleep -Seconds 1}
} finally {
 if(!(Get-Process 'ShippingPC-BmGame' -ErrorAction SilentlyContinue)) {Restore-AsylumProfile $profile;Remove-Item -LiteralPath $journal;Write-Host 'Original graphics settings restored.'}
}
