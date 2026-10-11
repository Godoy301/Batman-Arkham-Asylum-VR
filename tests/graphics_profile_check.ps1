$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/Graphics-Profile.ps1')
$dir=Join-Path ([IO.Path]::GetTempPath()) ('asylum-profile-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir | Out-Null
foreach($encoding in @([Text.Encoding]::GetEncoding(28591),[Text.Encoding]::Unicode)) {
 $path=Join-Path $dir 'engine.ini';$backup=Join-Path $dir 'engine.backup'
 $text="[Engine.GameEngine]`r`nbSmoothFrameRate=TRUE`r`n[SystemSettings]`r`nResX=1680`r`nResY=1050`r`nFullscreen=True`r`nUseVsync=True`r`nMotionBlur=True`r`nDepthOfField=True`r`nUnrelated=keep`r`n[SystemSettingsEditor]`r`nResX=960`r`nResY=720`r`n"
 $bytes=$encoding.GetPreamble()+$encoding.GetBytes($text)
 [IO.File]::WriteAllBytes($path,$bytes);[IO.File]::WriteAllBytes($backup,$bytes)
 $hash=(Get-FileHash -LiteralPath $path).Hash
 Set-AsylumVRProfile $path 2284 2448
 $out=$encoding.GetString([IO.File]::ReadAllBytes($path))
 foreach($expected in @('ResX=2284','ResY=2448','MotionBlur=False','DepthOfField=False','bSmoothFrameRate=False','Unrelated=keep',"[SystemSettingsEditor]`r`nResX=960`r`nResY=720")) {
  if(!$out.Contains($expected)) {throw "Profile corruption: $expected"}
 }
 Restore-AsylumProfile @{files=@(@{path=$path;backup=$backup;sha256=$hash})}
 if((Get-FileHash -LiteralPath $path).Hash -ne $hash) {throw 'Byte-exact restoration failed.'}
}
Remove-Item -LiteralPath (Join-Path $dir 'engine.ini'),(Join-Path $dir 'engine.backup')
Remove-Item -LiteralPath $dir
Write-Output 'PASS: ANSI/UTF-16 profiles, runtime near-square dimensions, editor section preserved, byte-exact rollback.'
