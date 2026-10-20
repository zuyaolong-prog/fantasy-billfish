[CmdletBinding(PositionalBinding = $false)]
param()

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$patchRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $patchRoot

if($PSVersionTable.PSVersion.Major-ne5){
    $childOutput=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath 2>&1
    $childExit=$LASTEXITCODE
    $childOutput|Out-Host
    if($childExit-ne0){throw "Full Windows PowerShell 5.1 suite failed with exit code $childExit."}
    return
}

& (Join-Path $PSScriptRoot 'fixture-launcher.ps1') | Out-Host
& (Join-Path $PSScriptRoot 'transaction-security.ps1') | Out-Host
& (Join-Path $PSScriptRoot 'root-topology.ps1') | Out-Host
& (Join-Path $PSScriptRoot 'library-health.ps1') | Out-Host
& (Join-Path $PSScriptRoot 'portable-runtime.ps1') | Out-Host
& (Join-Path $PSScriptRoot 'plugin-ingress.ps1') | Out-Host

$savedFlags = $env:QTWEBENGINE_CHROMIUM_FLAGS
try {
    $env:QTWEBENGINE_CHROMIUM_FLAGS = '--disable-gpu-compositing-extra'
    $passThrough = @('-a=auto', '--unknown', '', 'space value', 'a"b', 'C:\trailing space\')
    $launchFixture=Join-Path $env:TEMP ('billfish-root-launch-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $launchFixture|Out-Null;[IO.File]::WriteAllText((Join-Path $launchFixture 'Billfish.core.exe'),'fixture core')
    $expectedLibraryRoot = Join-Path $launchFixture 'fixture-library'
    $dryRun = & (Join-Path $repositoryRoot 'start-billfish.ps1') -DryRun -SkipUiPreferences -TestFixtureRoot $launchFixture -TestSkipPersistentPatchHealth -TestProcessPaths 'C:\Fixture\OtherBillfish.exe' -LibraryRoot $expectedLibraryRoot @passThrough
    if ($dryRun.Outcome -ne 'WouldStart') { throw 'Unified launcher dry-run did not return WouldStart.' }
    if ($dryRun.LibraryRoot -ne [IO.Path]::GetFullPath($expectedLibraryRoot)) { throw "Unknown Billfish arguments were bound to LibraryRoot: $($dryRun.LibraryRoot)" }
    if (($dryRun.Arguments | ConvertTo-Json -Compress) -ne ($passThrough | ConvertTo-Json -Compress)) { throw "Launcher argument pass-through changed values: $($dryRun.Arguments | ConvertTo-Json -Compress)" }
    $flagTokens = @($env:QTWEBENGINE_CHROMIUM_FLAGS -split '\s+' | Where-Object { $_ })
    if (@($flagTokens | Where-Object { $_ -eq '--disable-gpu' }).Count -ne 1 -or @($flagTokens | Where-Object { $_ -eq '--disable-gpu-compositing' }).Count -ne 1) { throw 'Chromium flags were not token-deduplicated.' }
    $dryRun | Out-Host
}
finally { $env:QTWEBENGINE_CHROMIUM_FLAGS = $savedFlags;if($launchFixture-and(Test-Path $launchFixture)){Remove-Item $launchFixture -Recurse -Force} }

$runningFixture=Join-Path $env:TEMP ('billfish-core-running-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $runningFixture|Out-Null;[IO.File]::WriteAllText((Join-Path $runningFixture 'Billfish.core.exe'),'fixture core');$alreadyRunning = & (Join-Path $repositoryRoot 'start-billfish.ps1') -DryRun -SkipUiPreferences -TestFixtureRoot $runningFixture -TestSkipPersistentPatchHealth -TestProcessPaths (Join-Path $runningFixture 'Billfish.core.exe');Remove-Item $runningFixture -Recurse -Force
if ($alreadyRunning.Outcome -ne 'AlreadyRunning') { throw 'Full-path running-process check did not suppress launch.' }
$alreadyRunning | Out-Host

$ps51 = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'ps51-smoke.ps1') -RepositoryRoot $repositoryRoot
if ($LASTEXITCODE -ne 0) { throw "Windows PowerShell 5.1 smoke test failed: $($ps51 -join [Environment]::NewLine)" }
$ps51 | Out-Host
$ps51Fixture=Join-Path $env:TEMP ('billfish-ps51-core-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $ps51Fixture|Out-Null;[IO.File]::WriteAllText((Join-Path $ps51Fixture 'Billfish.core.exe'),'fixture core');$ps51Launch = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repositoryRoot 'start-billfish.ps1') -DryRun -SkipUiPreferences -TestFixtureRoot $ps51Fixture -TestSkipPersistentPatchHealth -TestProcessPaths 'C:\Fixture\OtherBillfish.exe' '-a=auto' '--unknown' 'space value';Remove-Item $ps51Fixture -Recurse -Force
if ($LASTEXITCODE -ne 0 -or ($ps51Launch -join [Environment]::NewLine) -notmatch 'WouldStart') { throw "Windows PowerShell 5.1 unified launcher dry-run failed: $($ps51Launch -join [Environment]::NewLine)" }
$av1Fixture=Join-Path $env:TEMP ('billfish-av1-core-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $av1Fixture|Out-Null;[IO.File]::WriteAllText((Join-Path $av1Fixture 'Billfish.core.exe'),'fixture core');$av1Forward = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repositoryRoot '_av1-internal-player-patch\start-billfish-av1.ps1') -DryRun -TestFixtureRoot $av1Fixture -TestSkipPersistentPatchHealth '-a=auto' '--unknown' 'space value';Remove-Item $av1Fixture -Recurse -Force
if ($LASTEXITCODE -ne 0) { throw "AV1 forwarding entry rejected Billfish arguments: $($av1Forward -join [Environment]::NewLine)" }
$av1ProcessTests=& python (Join-Path $repositoryRoot '_av1-analysis-patch\tests\test_process_preflight.py') 2>&1;$av1ProcessExit=$LASTEXITCODE;if($av1ProcessExit-ne0){throw "AV1 apply process preflight tests failed: $($av1ProcessTests -join [Environment]::NewLine)"};$av1ProcessTests|Out-Host

$wrapper = Join-Path $repositoryRoot 'Billfish.exe'
$boundaryRejected=$false;try{& (Join-Path $patchRoot 'build-wrapper.ps1') -StagingDirectory $repositoryRoot -DotNetPath 'must-not-run'|Out-Null}catch{$boundaryRejected=$_.Exception.Message-match'approved staging boundary'};if(-not$boundaryRejected){throw 'Build accepted BillfishRoot as StagingDirectory.'}
$traversalRejected=$false;try{& (Join-Path $patchRoot 'build-wrapper.ps1') -StagingDirectory (Join-Path $patchRoot 'staging\..\..\live') -DotNetPath 'must-not-run'|Out-Null}catch{$traversalRejected=$_.Exception.Message-match'approved staging boundary'};if(-not$traversalRejected){throw 'Build accepted a normalized staging traversal.'}
& (Join-Path $patchRoot 'build-wrapper.ps1')|Out-Null
$wrapperBefore = (Get-FileHash -LiteralPath $wrapper -Algorithm SHA256).Hash
$fakeDotNet = Join-Path $env:TEMP ('fake-dotnet-' + [guid]::NewGuid().ToString('N') + '.cmd')
[System.IO.File]::WriteAllText($fakeDotNet, "@exit /b 23`r`n", [System.Text.Encoding]::ASCII)
$buildFailed = $false
try {
    $fixtureBuildRoot=Join-Path $env:TEMP ('billfish-build-fixture-'+[guid]::NewGuid().ToString('N'));$fixtureBoundaryAccepted=$false;try{& (Join-Path $patchRoot 'build-wrapper.ps1') -DotNetPath $fakeDotNet -TestFixtureRoot $fixtureBuildRoot -StagingDirectory (Join-Path $fixtureBuildRoot 'staging')|Out-Null}catch{$fixtureBoundaryAccepted=$_.Exception.Message-match'exit code 23'};if(-not$fixtureBoundaryAccepted){throw 'Explicit temp fixture staging boundary was rejected.'}
    $badFixtureRejected=$false;try{& (Join-Path $patchRoot 'build-wrapper.ps1') -DotNetPath 'must-not-run' -TestFixtureRoot $env:SystemRoot -StagingDirectory (Join-Path $env:SystemRoot 'fixture-staging')|Out-Null}catch{$badFixtureRejected=$_.Exception.Message-match'child of the system temp'};if(-not$badFixtureRejected){throw 'Non-temp TestFixtureRoot was accepted.'}
    try { & (Join-Path $patchRoot 'build-wrapper.ps1') -DotNetPath $fakeDotNet | Out-Null }
    catch { $buildFailed = $_.Exception.Message -match 'exit code 23' }
}
finally { Remove-Item -LiteralPath $fakeDotNet -Force -ErrorAction SilentlyContinue }
if (-not $buildFailed) { throw 'Injected dotnet failure was not propagated.' }
if ((Get-FileHash -LiteralPath $wrapper -Algorithm SHA256).Hash -ne $wrapperBefore) { throw 'Failed build replaced the working wrapper.' }

$wrapperFixture = Join-Path $env:TEMP ('billfish-wrapper-failure-' + [guid]::NewGuid().ToString('N'))
try {
    $fixtureLauncher = $wrapperFixture
    New-Item -ItemType Directory -Path $fixtureLauncher -Force | Out-Null
    Copy-Item -LiteralPath $wrapper -Destination (Join-Path $fixtureLauncher 'Billfish.exe')
    [System.IO.File]::WriteAllText((Join-Path $wrapperFixture 'start-billfish.ps1'), "[Console]::Error.WriteLine('fixture preflight failure')`r`nexit 37`r`n", (New-Object System.Text.UTF8Encoding($false)))
    $dryRunFile=Join-Path $wrapperFixture 'wrapper-dry-run.txt';$savedDryRun=$env:BILLFISH_WRAPPER_DRY_RUN;$savedDryRunFile=$env:BILLFISH_WRAPPER_DRY_RUN_FILE
    try{$env:BILLFISH_WRAPPER_DRY_RUN='1';$env:BILLFISH_WRAPPER_DRY_RUN_FILE=$dryRunFile;$dryRunProcess=Start-Process -FilePath (Join-Path $fixtureLauncher 'Billfish.exe') -ArgumentList '--root-wrapper-smoke' -Wait -PassThru}
    finally{if($null-eq$savedDryRun){Remove-Item Env:BILLFISH_WRAPPER_DRY_RUN -ErrorAction SilentlyContinue}else{$env:BILLFISH_WRAPPER_DRY_RUN=$savedDryRun};if($null-eq$savedDryRunFile){Remove-Item Env:BILLFISH_WRAPPER_DRY_RUN_FILE -ErrorAction SilentlyContinue}else{$env:BILLFISH_WRAPPER_DRY_RUN_FILE=$savedDryRunFile}}
    $dryRunText=Get-Content -LiteralPath $dryRunFile -Raw;if($dryRunProcess.ExitCode-ne0-or$dryRunText-notmatch[regex]::Escape('WorkingDirectory="'+$wrapperFixture+'"')-or$dryRunText-notmatch[regex]::Escape((Join-Path $wrapperFixture 'start-billfish.ps1'))-or$dryRunText-notmatch'--root-wrapper-smoke'){throw 'Root wrapper dry-run did not resolve the root script and working directory.'}
    $errorFile = Join-Path $wrapperFixture 'wrapper-error.txt'
    $savedSuppressUi=$env:BILLFISH_WRAPPER_SUPPRESS_UI;$savedErrorFile=$env:BILLFISH_WRAPPER_ERROR_FILE
    try{
        $env:BILLFISH_WRAPPER_SUPPRESS_UI='1';$env:BILLFISH_WRAPPER_ERROR_FILE=$errorFile
        $process = Start-Process -FilePath (Join-Path $fixtureLauncher 'Billfish.exe') -Wait -PassThru
    }
    finally{
        if($null-eq$savedSuppressUi){Remove-Item Env:BILLFISH_WRAPPER_SUPPRESS_UI -ErrorAction SilentlyContinue}else{$env:BILLFISH_WRAPPER_SUPPRESS_UI=$savedSuppressUi}
        if($null-eq$savedErrorFile){Remove-Item Env:BILLFISH_WRAPPER_ERROR_FILE -ErrorAction SilentlyContinue}else{$env:BILLFISH_WRAPPER_ERROR_FILE=$savedErrorFile}
    }
    if ($process.ExitCode -ne 37) { throw "Wrapper did not propagate PowerShell exit code 37; got $($process.ExitCode)." }
    if (-not (Test-Path -LiteralPath $errorFile) -or (Get-Content -LiteralPath $errorFile -Raw) -notmatch 'fixture preflight failure') { throw 'Wrapper failure was not made visible through the diagnostic file.' }
}
finally { if (Test-Path -LiteralPath $wrapperFixture) { Remove-Item -LiteralPath $wrapperFixture -Recurse -Force } }

$global:LASTEXITCODE = 0
[pscustomobject]@{ Outcome = 'Passed'; Ps51 = $true; ArgumentPassThrough = $true; Av1ApplyPreflight = $true; StagingBoundary = $true; BuildFailure = $true; WrapperRootDryRun = $true; WrapperFailure = $true }
