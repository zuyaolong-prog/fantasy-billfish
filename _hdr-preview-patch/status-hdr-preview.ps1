[CmdletBinding()]
param()

Set-StrictMode -Version 3.0

$patchDir = $PSScriptRoot
$mpvExe = Join-Path $patchDir 'tools\mpv\mpv.exe'
$mpvConf = Join-Path $patchDir 'portable-config\mpv.conf'
$menuKeys = @('.mp4', '.webm', '.mkv', '.mov') | ForEach-Object {
    "HKCU:\Software\Classes\SystemFileAssociations\$_\shell\BillfishHdrSdrPreview"
}

[pscustomobject]@{
    PatchDir = $patchDir
    MpvExe = $mpvExe
    MpvExeExists = Test-Path -LiteralPath $mpvExe -PathType Leaf
    MpvConf = $mpvConf
    MpvConfExists = Test-Path -LiteralPath $mpvConf -PathType Leaf
    ContextMenuInstalledFor = @($menuKeys | Where-Object { Test-Path -LiteralPath $_ }).Count
}
