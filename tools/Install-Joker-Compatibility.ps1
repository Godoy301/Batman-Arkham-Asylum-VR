[CmdletBinding()]
param([switch]$Elevated)

$ErrorActionPreference = 'Stop'
$installer = Join-Path $PSScriptRoot 'Apply-JokerCompatBuild.ps1'

if (!(Test-Path -LiteralPath $installer -PathType Leaf)) {
    Write-Error "Apply-JokerCompatBuild.ps1 was not found beside this installer. Extract the entire ZIP first."
    if ($Elevated) { Read-Host 'Press Enter to close' }
    exit 1
}

# Request elevation because Steam may be installed under Program Files.
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)
if (!$isAdmin) {
    try {
        $powerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $argumentLine = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Elevated' -f $PSCommandPath
        $child = Start-Process -FilePath $powerShell -ArgumentList $argumentLine -Verb RunAs -Wait -PassThru
        exit $child.ExitCode
    }
    catch {
        Write-Host 'Administrator permission was not granted; no files were changed.' -ForegroundColor Yellow
        exit 1
    }
}

Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Application]::EnableVisualStyles()

try {
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select the Binaries folder containing ShippingPC-BmGame.exe for Batman: Arkham Asylum GOTY.'
    $dialog.ShowNewFolderButton = $false

    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        Write-Host 'Cancelled. No files were changed.' -ForegroundColor Yellow
        if ($Elevated) { Read-Host 'Press Enter to close' }
        exit 0
    }

    $gameBinaries = [IO.Path]::GetFullPath($dialog.SelectedPath)
    $exe = Join-Path $gameBinaries 'ShippingPC-BmGame.exe'
    $manifest = Join-Path $gameBinaries 'asylumvr_manifest.json'
    $launchScript = Join-Path $gameBinaries 'AsylumVR\Launch-VR.ps1'

    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw 'The selected folder does not contain ShippingPC-BmGame.exe. Select the game Binaries folder, not the game root folder.'
    }
    if (!(Test-Path -LiteralPath $manifest -PathType Leaf) -or !(Test-Path -LiteralPath $launchScript -PathType Leaf)) {
        throw 'Batman Asylum VR is not installed in the selected folder. Install the official VR mod first, then run this compatibility installer.'
    }

    $newline = [Environment]::NewLine
    $confirmText = 'Install the Joker DLC compatibility update into:' + $newline + $newline + $gameBinaries + $newline + $newline + 'The installer will back up the files it replaces and will not modify ShippingPC-BmGame.exe.'
    $answer = [System.Windows.Forms.MessageBox]::Show(
        $confirmText,
        'Confirm Batman Asylum VR compatibility update',
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Question
    )
    if ($answer -ne [System.Windows.Forms.DialogResult]::Yes) {
        Write-Host 'Cancelled. No files were changed.' -ForegroundColor Yellow
        if ($Elevated) { Read-Host 'Press Enter to close' }
        exit 0
    }

    & $installer -GameBinaries $gameBinaries

    [System.Windows.Forms.MessageBox]::Show(
        ('Compatibility update installed successfully.' + $newline + $newline + 'Start your PC VR software, then launch Batman Asylum VR using its shortcut.'),
        'Batman Asylum VR - installation complete',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information
    ) | Out-Null
    Write-Host 'Installation completed successfully.' -ForegroundColor Green
    if ($Elevated) { Read-Host 'Press Enter to close' }
    exit 0
}
catch {
    $message = $_.Exception.Message
    Write-Host "INSTALLATION FAILED: $message" -ForegroundColor Red
    [System.Windows.Forms.MessageBox]::Show(
        ('The compatibility update could not be installed:' + $newline + $newline + $message + $newline + $newline + 'Check the console for details.'),
        'Batman Asylum VR - installation failed',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
    if ($Elevated) { Read-Host 'Press Enter to close' }
    exit 1
}
