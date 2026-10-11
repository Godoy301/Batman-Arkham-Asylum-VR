$ErrorActionPreference='Stop'
$bin=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
if(Get-Process 'ShippingPC-BmGame' -ErrorAction SilentlyContinue) {throw 'Close Asylum before uninstalling.'}
$manifestPath=Join-Path $bin 'asylumvr_manifest.json'
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach($record in $manifest.files) {
 $p=[IO.Path]::GetFullPath((Join-Path $bin $record.name))
 if(!$p.StartsWith($bin+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Unsafe manifest path.'}
 if((Get-FileHash -LiteralPath $p).Hash -ne $record.sha256) {throw "Changed installed file retained: $p"}
}
$journal=Join-Path $env:LOCALAPPDATA 'Batman_Asylum/profile-active.json'
if(Test-Path -LiteralPath $journal) {
 . (Join-Path $PSScriptRoot 'Graphics-Profile.ps1')
 Restore-AsylumProfile (Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json)
 Remove-Item -LiteralPath $journal
}
foreach($record in $manifest.files) {Remove-Item -LiteralPath (Join-Path $bin $record.name)}
Remove-Item -LiteralPath $manifestPath
Write-Host 'Asylum VR files removed; saves and original game files retained.'
