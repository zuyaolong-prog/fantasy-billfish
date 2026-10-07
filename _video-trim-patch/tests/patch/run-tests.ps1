[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$parseTest = Join-Path -Path $PSScriptRoot -ChildPath 'parse-powershell.ps1'
$fixtureTest = Join-Path -Path $PSScriptRoot -ChildPath 'fixture-install-restore.ps1'

$results = @()
$results += & $parseTest
$results += & $fixtureTest
$normalizedResults = foreach ($result in $results) {
    $filesParsed = $null
    $assertions = $null
    if ($null -ne $result.PSObject.Properties['filesParsed']) {
        $filesParsed = $result.filesParsed
    }

    if ($null -ne $result.PSObject.Properties['assertions']) {
        $assertions = $result.assertions
    }

    [pscustomobject]@{
        test = $result.test
        outcome = $result.outcome
        filesParsed = $filesParsed
        assertions = $assertions
        powerShellVersion = $result.powerShellVersion
    }
}

$normalizedResults | Format-Table -AutoSize
