[CmdletBinding()]
param(
    [switch]$DryRun,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Path
)

Set-StrictMode -Version 3.0

$patchDir = $PSScriptRoot
$mpvExe = Join-Path $patchDir 'tools\mpv\mpv.exe'
$mpvConf = Join-Path $patchDir 'portable-config\mpv.conf'

if (-not (Test-Path -LiteralPath $mpvExe -PathType Leaf)) {
    throw "mpv.exe was not found at $mpvExe"
}

if (-not (Test-Path -LiteralPath $mpvConf -PathType Leaf)) {
    throw "mpv.conf was not found at $mpvConf"
}

if ($Path.Count -eq 0) {
    throw 'Pass one or more media file paths, or drag media files onto play-hdr-sdr.cmd.'
}

$resolvedPaths = New-Object System.Collections.Generic.List[string]
foreach ($item in $Path) {
    if ([string]::IsNullOrWhiteSpace($item)) {
        continue
    }

    $literal = $item.Trim('"')
    if (-not (Test-Path -LiteralPath $literal -PathType Leaf)) {
        throw "Media file was not found: $literal"
    }

    $resolvedPaths.Add([System.IO.Path]::GetFullPath($literal))
}

if ($resolvedPaths.Count -eq 0) {
    throw 'No valid media files were provided.'
}

$arguments = New-Object System.Collections.Generic.List[string]
$arguments.Add("--config-dir=$($patchDir)")
$arguments.Add("--include=$mpvConf")
$arguments.Add('--force-window=yes')
foreach ($mediaPath in $resolvedPaths) {
    $arguments.Add($mediaPath)
}

if ($DryRun) {
    [pscustomobject]@{
        MpvExe = $mpvExe
        WorkingDirectory = $patchDir
        Arguments = $arguments.ToArray()
    } | Format-List
    return
}

Start-Process -FilePath $mpvExe -ArgumentList $arguments -WorkingDirectory $patchDir
