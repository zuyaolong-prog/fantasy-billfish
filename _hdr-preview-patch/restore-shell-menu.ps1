[CmdletBinding(SupportsShouldProcess = $true)]
param()

Set-StrictMode -Version 3.0

$extensions = @('.mp4', '.webm', '.mkv', '.mov')
foreach ($extension in $extensions) {
    $baseKey = "HKCU:\Software\Classes\SystemFileAssociations\$extension\shell\BillfishHdrSdrPreview"
    if (Test-Path -LiteralPath $baseKey) {
        if ($PSCmdlet.ShouldProcess($baseKey, 'remove user-level HDR SDR preview context menu')) {
            Remove-Item -LiteralPath $baseKey -Recurse -Force
        }
    }
}

Write-Host 'Removed user-level context menu: Billfish HDR SDR Preview'
