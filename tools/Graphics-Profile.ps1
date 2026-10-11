function Get-AsylumConfigPaths([string]$GameDir) {
    $paths=@((Join-Path $GameDir 'BmGame/Config/BmEngine.ini'))
    $docs=[Environment]::GetFolderPath('MyDocuments')
    foreach($relative in @('Square Enix/Batman Arkham Asylum GOTY','Eidos/Batman Arkham Asylum')) {
        foreach($name in @('BmEngine.ini','UserEngine.ini')) {
            $p=Join-Path $docs ($relative+'/BmGame/Config/'+$name)
            if(Test-Path -LiteralPath $p) { $paths+=$p }
        }
    }
    return $paths
}
function Set-AsylumVRProfile([string]$Path,[int]$Width,[int]$Height) {
    $bytes=[IO.File]::ReadAllBytes($Path)
    # Preserve bytes/BOM/line endings. Retail Asylum configs here are ANSI;
    # Latin-1 is a reversible byte mapping for changes to ASCII keys/values.
    $encoding=[Text.Encoding]::GetEncoding(28591)
    if($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) {$encoding=[Text.Encoding]::Unicode}
    elseif($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) {$encoding=[Text.Encoding]::UTF8}
    $text=$encoding.GetString($bytes)
    $blocks=[regex]::Matches($text,'(?ms)(^\[SystemSettings\]\r?\n)(.*?)(?=^\[|\z)')
    if($blocks.Count -ne 1) {throw "SystemSettings section ambiguous: $Path"}
    $block=$blocks[0];$body=$block.Groups[2].Value
    $newline=if($body.Contains("`r`n")){"`r`n"}else{"`n"}
    foreach($option in @(@('Fullscreen','False'),@('UseVsync','False'),@('ResX',"$Width"),@('ResY',"$Height"),@('MotionBlur','False'),@('DepthOfField','False'))) {
        $key=$option[0];$value=$option[1];$pattern='(?mi)^'+$key+'=[^\r\n]*'
        if([regex]::Matches($body,$pattern).Count -gt 1) {throw "Duplicate $key in $Path"}
        if([regex]::IsMatch($body,$pattern)) {$body=[regex]::Replace($body,$pattern,{param($m) "$key=$value"})}
        else {$body+="$key=$value$newline"}
    }
    $text=$text.Substring(0,$block.Index)+$block.Groups[1].Value+$body+$text.Substring($block.Index+$block.Length)
    $text=[regex]::Replace($text,'(?mi)^bSmoothFrameRate=[^\r\n]*','bSmoothFrameRate=False')
    [IO.File]::WriteAllBytes($Path,$encoding.GetBytes($text))
}
function Restore-AsylumProfile($Profile) {
    foreach($record in $Profile.files) {
        if((Get-FileHash -LiteralPath $record.backup).Hash -ne $record.sha256) {throw "Profile backup changed: $($record.backup)"}
        [IO.File]::WriteAllBytes($record.path,[IO.File]::ReadAllBytes($record.backup))
        if((Get-FileHash -LiteralPath $record.path).Hash -ne $record.sha256) {throw "Profile restore failed: $($record.path)"}
    }
}
