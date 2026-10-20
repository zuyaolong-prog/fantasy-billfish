[CmdletBinding()]
param()
Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$patchRoot = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $patchRoot
. (Join-Path $patchRoot 'PluginIngress.Core.ps1')
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('billfish-plugin-ingress-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
try {
    $package = Join-Path $fixture 'space plugin.eagleplugin'
    [IO.File]::WriteAllText($package, 'fixture package')
    [IO.File]::WriteAllText((Join-Path $fixture 'Billfish.core.exe'), 'fixture core')
    foreach ($inputArgs in @(@($package), @('--install-plugin', $package))) {
        $request = Resolve-BillfishPluginIngress -Arguments $inputArgs
        if ($request.SourcePath -ne $package -or @($request.Arguments).Count -ne 0) { throw 'Package ingress leaked package arguments to vendor core.' }
    }
    $vendor = @('-a=auto', '--unknown', '', 'space value', 'a"b')
    $request = Resolve-BillfishPluginIngress -Arguments $vendor
    if ($request.SourcePath -or ($request.Arguments | ConvertTo-Json -Compress) -cne ($vendor | ConvertTo-Json -Compress)) { throw 'Ordinary vendor arguments changed.' }
    foreach ($invalid in @(@('--install-plugin'), @('--install-plugin', $package, '--unknown'), @($package, '--unknown'), @('--install-plugin', 'https://example.com/p.eagleplugin'), @('--install-plugin', (Join-Path $fixture 'missing.eagleplugin')))) {
        $failed = $false
        try { Resolve-BillfishPluginIngress -Arguments $invalid | Out-Null } catch { $failed = $true }
        if (-not $failed) { throw ('Invalid package command was accepted: ' + ($invalid -join ' ')) }
    }
    $launch = Join-Path $root 'start-billfish.ps1'
    $cold = & $launch -DryRun -SkipUiPreferences -TestFixtureRoot $fixture -TestSkipPersistentPatchHealth -TestProcessPaths 'C:\Fixture\OtherBillfish.exe' -InstallPlugin $package
    if ($cold.Outcome -ne 'WouldStart' -or $cold.PluginPackage -ne $package -or @($cold.Arguments).Count -ne 0) { throw 'Cold-start package was lost or passed to vendor core.' }
    $warm = & $launch -DryRun -SkipUiPreferences -TestFixtureRoot $fixture -TestSkipPersistentPatchHealth -TestProcessPaths (Join-Path $fixture 'Billfish.core.exe') -InstallPlugin $package
    if ($warm.Outcome -ne 'AlreadyRunning' -or $warm.PluginPackage -ne $package) { throw 'Running-host package was dropped.' }
    [pscustomobject]@{ Outcome = 'Passed'; PackageArguments = $true; ColdStart = $true; AlreadyRunning = $true; NoGuiStarted = $true }
}
finally { if ([IO.Directory]::Exists($fixture)) { [IO.Directory]::Delete($fixture, $true) } }
