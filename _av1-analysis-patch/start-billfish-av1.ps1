[CmdletBinding()]
param(
    [switch]$SkipScan,
    [AllowEmptyString()][string]$LibraryRoot
)

Set-StrictMode -Version 3.0

$patchDir = $PSScriptRoot
$root = Split-Path -Parent $patchDir
$billfishExe = Join-Path $root 'Billfish.exe'
$fixScript = Join-Path $patchDir 'fix-av1-analysis.py'

if (-not (Test-Path -LiteralPath $billfishExe -PathType Leaf)) {
    throw "Billfish.exe was not found at $billfishExe"
}

if (-not (Test-Path -LiteralPath $fixScript -PathType Leaf)) {
    throw "fix-av1-analysis.py was not found at $fixScript"
}

$rootPrefix=[IO.Path]::GetFullPath($root).TrimEnd('\')+'\';$fixed=@([IO.Path]::GetFullPath((Join-Path $root 'Billfish.exe')),[IO.Path]::GetFullPath((Join-Path $root 'Billfish.core.exe')));$running=@()
foreach($process in @(Get-Process -Name Billfish,Billfish.core,PluginWorker,PluginWorker64 -ErrorAction SilentlyContinue)){
    try{$processPath=[string]$process.Path;if([string]::IsNullOrWhiteSpace($processPath)){throw 'Executable path is unavailable.'};$full=[IO.Path]::GetFullPath($processPath)}catch{$running+=,"$($process.ProcessName) PID $($process.Id) (path unavailable)";continue}
    $isFixed=@($fixed|Where-Object{$_.Equals($full,[StringComparison]::OrdinalIgnoreCase)}).Count-gt0;$isWorker=$full.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase)-and[IO.Path]::GetFileNameWithoutExtension($full)-in@('PluginWorker','PluginWorker64');if($isFixed-or$isWorker){$running+=,$full}
}
if ($running.Count-eq0 -and -not $SkipScan) {
    . (Join-Path $root '_launcher-patch\Launcher.Core.ps1')
    $resolution = Resolve-BillfishLibraryRoot -LibraryRoot $LibraryRoot -AppDataRoot $env:APPDATA
    if (-not $resolution.RootAvailable) { throw $resolution.Diagnostic }
    python $fixScript --library-root $resolution.LibraryRoot --scan-av1 --apply
    $scanExitCode = $LASTEXITCODE
    if ($scanExitCode -ne 0) { throw "AV1 analysis failed with exit code $scanExitCode" }
}

Start-Process -FilePath $billfishExe -WorkingDirectory $root -WindowStyle Hidden
