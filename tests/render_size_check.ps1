$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/Render-Size.ps1')
foreach($case in @(@(2284,2448,2560,1392),@(2284,2448,1920,1040),@(1280,1372,2560,1392),@(1024,1024,2560,1392))) {
    $r=Get-AsylumWindowRenderSize $case[0] $case[1] $case[2] $case[3]
    if($r.width -gt $case[2]-64 -or $r.height -gt $case[3]-64) {throw 'Window would be clamped.'}
    if([Math]::Abs($r.width/[double]$r.height-$case[0]/[double]$case[1]) -gt .002) {throw 'Eye aspect ratio changed.'}
}
$r=Get-AsylumWindowRenderSize 2284 2448 2560 1392
if($r.width -ne 1238 -or $r.height -ne 1328) {throw 'Live A1a oversized-window case is not corrected.'}
$r=Get-AsylumWindowRenderSize 1024 1024 2560 1392
if($r.width -ne 1024 -or $r.height -ne 1024) {throw 'Already fitting render changed.'}
$rejected=$false
try {Get-AsylumWindowRenderSize 2284 2448 640 480 | Out-Null} catch {$rejected=$true}
if(!$rejected) {throw 'Unusable desktop was accepted.'}
$render=Get-AsylumVRRenderSize 2284 2448 1.0
if($render.width -ne 2284 -or $render.height -ne 2448) {throw 'Game render is still desktop-capped.'}
foreach($scale in @(.6,1.0,1.2,1.8)) {
 $render=Get-AsylumVRRenderSize 2284 2448 $scale
 if([Math]::Abs($render.width/[double]$render.height-2284.0/2448) -gt .002) {throw 'Scaled game aspect changed.'}
}
Write-Output 'PASS: desktop mirror fitted independently; game render2284x2448 default,0.6x-1.8x preserve near-square eye aspect.'
$quality=Get-AsylumDLSSRenderSize 2284 2448 1.0 3 .8
if($quality.width -ne 1902 -or $quality.height -ne 2040 -or $quality.outputWidth -ne 2284 -or $quality.outputHeight -ne 2448) {throw 'Quality still renders full-sized input or supersamples output accidentally.'}
foreach($mode in @(0,1,2,3,4,5)) {
 $input=Get-AsylumDLSSRenderSize 2284 2448 1.2 $mode .8
 if($input.outputWidth -ne 2740 -or $input.outputHeight -ne 2936) {throw 'DLSS mode changed requested output size.'}
 if([Math]::Abs($input.width/[double]$input.height-2284.0/2448) -gt .002) {throw 'DLSS mode distorted eye aspect.'}
}
$performance=Get-AsylumDLSSRenderSize 2284 2448 1.0 5 1.0
if($performance.width -ne 1142 -or $performance.height -ne 1224) {throw 'Direct-lens Performance ratio incorrect.'}
Write-Output 'PASS: mode-sized scene input, compensated compatibility crop, fixed desired output; scale applies to output, aspect preserved.'
