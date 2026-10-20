[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$GameBinaries
)

$ErrorActionPreference = 'Stop'
$GameBinaries = [IO.Path]::GetFullPath($GameBinaries)
$exe = Join-Path $GameBinaries 'ShippingPC-BmGame.exe'
$manifestPath = Join-Path $GameBinaries 'asylumvr_manifest.json'
$stockHash = '4dac1f5e2ac6710b7378fdce74601f616f4753e3756cb5fda63c7519cc2eb028'
$jokerHash = 'ed65ce3c52baca7adaea00cb0e5def3d8a78bd05f7ceacd68a68462e92d8dab2'

if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw "ShippingPC-BmGame.exe was not found in: $GameBinaries" }
if (Get-Process -Name 'ShippingPC-BmGame' -ErrorAction SilentlyContinue) { throw 'Close Batman: Arkham Asylum before applying the compatibility build.' }
$exeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
if ($exeHash -notin @($stockHash, $jokerHash)) { throw "Unsupported game executable hash: $exeHash. Nothing was changed." }
if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "The installed mod manifest was not found: $manifestPath. Install the official VR mod first." }

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (!$manifest.files) { throw 'The mod manifest is missing its files list. Nothing was changed.' }

# Validate every installed mod file before replacing anything, so this helper does not conceal unrelated changes.
foreach ($record in $manifest.files) {
    $path = Join-Path $GameBinaries ($record.name -replace '/', '\')
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Installed mod file is missing: $($record.name). Nothing was changed." }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $record.sha256) { throw "Installed mod file was modified before this update: $($record.name). Nothing was changed." }
}

$payload = [ordered]@{
    'd3d9.dll' = 'd3d9.dll'
    'xinput1_3.dll' = 'xinput1_3.dll'
    'batman_asylum.dll' = 'batman_asylum.dll'
    'aa_xr_config32.exe' = 'aa_xr_config32.exe'
    'AsylumVR/Launch-VR.ps1' = 'Launch-VR.ps1'
}
foreach ($sourceName in $payload.Values) {
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $sourceName) -PathType Leaf)) {
        throw "Payload file is missing: $sourceName. Nothing was changed."
    }
}
foreach ($targetName in $payload.Keys) {
    if (!($manifest.files | Where-Object { $_.name -eq $targetName })) {
        throw "Manifest has no record for $targetName. Nothing was changed."
    }
}

$backupRoot = Join-Path $env:LOCALAPPDATA ('Batman_Asylum/joker-compat-backup/' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backupRoot 'asylumvr_manifest.json')
foreach ($targetName in $payload.Keys) {
    $destination = Join-Path $GameBinaries ($targetName -replace '/', '\')
    $backupPath = Join-Path $backupRoot ($targetName -replace '/', '\')
    New-Item -ItemType Directory -Path (Split-Path $backupPath -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $destination -Destination $backupPath
}

try {
    foreach ($targetName in $payload.Keys) {
        $source = Join-Path $PSScriptRoot $payload[$targetName]
        $destination = Join-Path $GameBinaries ($targetName -replace '/', '\')
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    foreach ($record in $manifest.files) {
        if ($payload.Contains($record.name)) {
            $path = Join-Path $GameBinaries ($record.name -replace '/', '\')
            $record.sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
            $record.bytes = (Get-Item -LiteralPath $path).Length
        }
    }
    $json = $manifest | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText($manifestPath, $json, (New-Object System.Text.UTF8Encoding($false)))
}
catch {
    Write-Warning "Update failed. Restoring the backed-up files from $backupRoot"
    foreach ($targetName in $payload.Keys) {
        $destination = Join-Path $GameBinaries ($targetName -replace '/', '\')
        $backupPath = Join-Path $backupRoot ($targetName -replace '/', '\')
        if (Test-Path -LiteralPath $backupPath) { Copy-Item -LiteralPath $backupPath -Destination $destination -Force }
    }
    Copy-Item -LiteralPath (Join-Path $backupRoot 'asylumvr_manifest.json') -Destination $manifestPath -Force
    throw
}

Write-Host ''
Write-Host 'Joker compatibility build installed.' -ForegroundColor Green
Write-Host "Executable hash accepted: $exeHash"
Write-Host "Backup saved to: $backupRoot"
Write-Host 'Start your PC VR software, then use the Batman Asylum VR shortcut.'
Write-Host 'This is an experimental test build; restore the backups if VR does not start correctly.'
