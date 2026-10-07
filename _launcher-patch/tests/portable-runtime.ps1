[CmdletBinding(PositionalBinding = $false)]
param()

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('Billfish portable 中文 ' + [guid]::NewGuid().ToString('N'))
$runtimeRoot = Join-Path $fixture 'relocated app'
$appDataRoot = Join-Path $fixture 'new account'
function Assert-Portable([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

try {
    $baseline = Get-Content -LiteralPath (Join-Path $repositoryRoot '_launcher-patch/portable-runtime.json') -Raw | ConvertFrom-Json
    $runtimeFiles = @($baseline.files | ForEach-Object { $_.path }) + @('qt.conf', 'start-billfish.ps1', '_launcher-patch/Launcher.Core.ps1', '_launcher-patch/portable-runtime.json')
    foreach ($relativePath in $runtimeFiles) {
        $target = Join-Path $runtimeRoot $relativePath
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
        [IO.File]::Copy((Join-Path $repositoryRoot $relativePath), $target)
    }
    $status = & (Join-Path $repositoryRoot '_launcher-patch/status-unified-billfish.ps1') -BillfishRoot $runtimeRoot -AppDataRoot $appDataRoot
    Assert-Portable ($status.Healthy -and $status.Mode -eq 'PortableRuntime' -and -not $status.EntryPoints.Checked) 'A relocated runtime without installation history or user data is not healthy.'
    $plan = & (Join-Path $runtimeRoot 'start-billfish.ps1') -DryRun -TestAppDataRoot $appDataRoot -TestProcessPaths 'C:\Fixture\OtherBillfish.exe'
    Assert-Portable ($plan.Outcome -eq 'WouldStart' -and $plan.Executable -eq (Join-Path $runtimeRoot 'Billfish.core.exe') -and [string]::IsNullOrEmpty($plan.LibraryRoot)) 'Fresh account cannot start without a preexisting library.'
    Assert-Portable (-not [IO.Directory]::Exists($appDataRoot)) 'Dry-run wrote account configuration.'

    # Exercise the real startup preference path without launching the vendor UI.
    function Start-Process { param($FilePath, $WorkingDirectory, $ArgumentList) }
    $started = & (Join-Path $runtimeRoot 'start-billfish.ps1') -TestAppDataRoot $appDataRoot -TestProcessPaths 'C:\Fixture\OtherBillfish.exe'
    Assert-Portable ($started.Outcome -eq 'Started') 'First startup failed before process creation.'
    $config = [IO.File]::ReadAllText((Join-Path $appDataRoot 'Billfish/config/user_config.ini'))
    Assert-Portable ($config.Contains('EnableHardwareAcceleration=0') -and $config.Contains('EnableHardwareAccelerationV2=0') -and $config.Contains('EnableHardwareRender=0')) 'First startup did not initialize the HDR decode preferences.'
    [IO.File]::WriteAllText((Join-Path $appDataRoot 'Billfish/config/recent_lib.ini'), "[LoadDefaultLibType]`r`nLastLoadLibPath=Z:\\missing-library\\`r`n")
    $plan = & (Join-Path $runtimeRoot 'start-billfish.ps1') -DryRun -TestAppDataRoot $appDataRoot -TestProcessPaths 'C:\Fixture\OtherBillfish.exe'
    Assert-Portable ([string]::IsNullOrEmpty($plan.LibraryRoot)) 'A stale recent library was injected into the relocated runtime.'

    [IO.File]::AppendAllText((Join-Path $runtimeRoot 'plugins/media_runtime/libMediaSDK64.dll'), 'tampered')
    $blocked = $false
    try { & (Join-Path $runtimeRoot 'start-billfish.ps1') -DryRun -TestAppDataRoot $appDataRoot -TestProcessPaths 'C:\Fixture\OtherBillfish.exe' | Out-Null }
    catch { $blocked = $_.Exception.Message -match 'Persistent patch health check failed' }
    Assert-Portable $blocked 'Runtime payload drift no longer blocks startup.'
    [pscustomobject]@{ Outcome = 'Passed'; Relocation = $true; FreshAccount = $true; MissingLibrary = $true; RuntimeDriftBlocked = $true }
}
finally {
    Remove-Item Function:\Start-Process -ErrorAction SilentlyContinue
    if ([IO.Directory]::Exists($fixture)) { [IO.Directory]::Delete($fixture, $true) }
}
