Set-StrictMode -Version 3.0

function Resolve-BillfishPluginIngress {
    param([string]$ExplicitPath, [AllowEmptyCollection()][string[]]$Arguments = @())
    $source = $ExplicitPath
    $remaining = @($Arguments)
    if ($remaining -contains '--install-plugin') {
        if ($source -or $remaining.Count -ne 2 -or $remaining[0] -ne '--install-plugin') {
            throw 'Usage: Billfish.exe --install-plugin <local .eagleplugin or .zip file>'
        }
        $source = $remaining[1]
        $remaining = @()
    }
    elseif (-not $source -and $remaining.Count -eq 1 -and $remaining[0].EndsWith('.eagleplugin', [StringComparison]::OrdinalIgnoreCase)) {
        $source = $remaining[0]
        $remaining = @()
    }
    elseif (@($remaining | Where-Object { $_ -and $_.EndsWith('.eagleplugin', [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) {
        throw 'Open one .eagleplugin package at a time, without additional Billfish arguments.'
    }
    if ($source) {
        if ($remaining.Count -ne 0 -or $source -match '^[a-z][a-z0-9+.-]*://' -or $source.StartsWith('\\')) {
            throw 'Plugin installation requires one local package file, without additional Billfish arguments.'
        }
        $source = [IO.Path]::GetFullPath($source)
        if ([IO.Path]::GetExtension($source) -notin @('.eagleplugin', '.zip') -or -not [IO.File]::Exists($source)) {
            throw "Plugin package not found or unsupported. Select a local .eagleplugin or .zip file: $source"
        }
    }
    [pscustomobject]@{ SourcePath = $source; Arguments = $remaining }
}

function Invoke-BillfishPluginIngress {
    param([Parameter(Mandatory = $true)][string]$BillfishRoot, [Parameter(Mandatory = $true)][string]$SourcePath)
    $python = $env:BILLFISH_PYTHON
    $bundledPython = Join-Path $BillfishRoot 'tools\python\python.exe'
    if (-not $python -and [IO.File]::Exists($bundledPython)) { $python = $bundledPython }
    if (-not $python) {
        $command = Get-Command python.exe -ErrorAction SilentlyContinue
        if (-not $command) { $command = Get-Command python -ErrorAction SilentlyContinue }
        if ($command) { $python = $command.Source }
    }
    if (-not $python) { throw 'Python is unavailable. The plugin package was not installed.' }
    $helper = Join-Path $PSScriptRoot 'deliver-plugin-package.py'
    $output = & $python -I $helper --install-root $BillfishRoot --source-path $SourcePath 2>&1
    $deliveryExit = $LASTEXITCODE
    if ($deliveryExit -ne 0) { throw "Plugin installation did not complete successfully.`n$($output -join [Environment]::NewLine)" }
    $output
}
