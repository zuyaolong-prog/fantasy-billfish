[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$patchRoot = [System.IO.Path]::GetFullPath((Join-Path -Path $PSScriptRoot -ChildPath '..\..'))
$files = @(Get-ChildItem -LiteralPath $patchRoot -Recurse -Filter '*.ps1' -File | Sort-Object -Property FullName)
if ($files.Count -eq 0) {
    throw "No PowerShell files found under $patchRoot"
}

$failures = New-Object 'System.Collections.Generic.List[string]'
foreach ($file in $files) {
    $tokens = $null
    $parseErrors = $null
    [void] [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref] $tokens, [ref] $parseErrors)
    foreach ($parseError in @($parseErrors)) {
        $failures.Add(('{0}:{1}:{2}: {3}' -f $file.FullName, $parseError.Extent.StartLineNumber, $parseError.Extent.StartColumnNumber, $parseError.Message))
    }
}

foreach ($supportsShouldProcessScript in @(
    'prepare-video-trim-patch.ps1',
    'install-video-trim-patch.ps1',
    'restore-video-trim-patch.ps1',
    'start-billfish-video-trim.ps1'
)) {
    $scriptPath = Join-Path -Path $patchRoot -ChildPath $supportsShouldProcessScript
    $scriptText = Get-Content -LiteralPath $scriptPath -Raw -ErrorAction Stop
    if ($scriptText -notmatch '(?s)\[CmdletBinding\([^\)]*SupportsShouldProcess\s*=\s*\$true') {
        $failures.Add("$scriptPath does not declare SupportsShouldProcess=true.")
    }
}

if ($failures.Count -gt 0) {
    throw ("PowerShell parse validation failed:`n" + ([string[]] $failures -join [Environment]::NewLine))
}

[pscustomobject]@{
    test = 'PowerShellParse'
    outcome = 'Passed'
    filesParsed = $files.Count
    powerShellVersion = $PSVersionTable.PSVersion.ToString()
}
