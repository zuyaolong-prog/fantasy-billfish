[CmdletBinding(PositionalBinding = $false, SupportsShouldProcess = $true)]
param(
    [string]$BillfishRoot,
    [switch]$DryRun,
    [switch]$Wait,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$ArgumentList
)

if([string]::IsNullOrWhiteSpace($BillfishRoot)){$BillfishRoot=Split-Path -Parent $PSScriptRoot}
$unified=Join-Path ([IO.Path]::GetFullPath($BillfishRoot)) 'start-billfish.ps1'
if($Wait){throw 'The unified root launcher does not expose a Wait mode.'}
if(-not(Test-Path -LiteralPath $unified -PathType Leaf)){throw "Unified Billfish launcher not found: $unified"}
if($WhatIfPreference){[pscustomobject]@{Outcome='WouldStart';Executable=Join-Path ([IO.Path]::GetFullPath($BillfishRoot)) 'Billfish.core.exe'};return}
& $unified -DryRun:$DryRun @ArgumentList
exit $LASTEXITCODE
