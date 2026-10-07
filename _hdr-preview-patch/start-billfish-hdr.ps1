[CmdletBinding(PositionalBinding = $false)]
param(
    [AllowEmptyString()][string]$LibraryRoot,
    [switch]$DryRun,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$BillfishArgs
)

$root=Split-Path -Parent $PSScriptRoot
$unified=Join-Path $root 'start-billfish.ps1'
if(-not(Test-Path -LiteralPath $unified -PathType Leaf)){throw "Unified Billfish launcher not found: $unified"}
& $unified -LibraryRoot $LibraryRoot -DryRun:$DryRun @BillfishArgs
exit $LASTEXITCODE
