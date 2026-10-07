[CmdletBinding(SupportsShouldProcess = $true)]
param()

Set-StrictMode -Version 3.0

$patchDir = $PSScriptRoot
$launcher = Join-Path $patchDir 'play-hdr-sdr.cmd'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "Launcher was not found at $launcher"
}

$extensions = @('.mp4', '.webm', '.mkv', '.mov')
foreach ($extension in $extensions) {
    $baseKey = "HKCU:\Software\Classes\SystemFileAssociations\$extension\shell\BillfishHdrSdrPreview"
    $commandKey = Join-Path $baseKey 'command'
    if ($PSCmdlet.ShouldProcess($baseKey, 'install user-level HDR SDR preview context menu')) {
        New-Item -Path $commandKey -Force | Out-Null
        New-ItemProperty -Path $baseKey -Name 'MUIVerb' -Value 'Billfish HDR SDR Preview' -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $baseKey -Name 'Icon' -Value (Join-Path $patchDir 'tools\mpv\mpv.exe') -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $commandKey -Name '(default)' -Value "`"$launcher`" `"%1`"" -PropertyType String -Force | Out-Null
    }
}

Write-Host 'Installed user-level context menu: Billfish HDR SDR Preview'
