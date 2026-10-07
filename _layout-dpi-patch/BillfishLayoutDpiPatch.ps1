Set-StrictMode -Version 3.0

function Get-BillfishLayoutPatchPaths {
    $toolDir = $PSScriptRoot
    $installRoot = [System.IO.Path]::GetFullPath((Join-Path $toolDir '..'))

    [pscustomobject]@{
        ToolDir = $toolDir
        InstallRoot = $installRoot
        BillfishExe = Join-Path $installRoot 'Billfish.core.exe'
        QtConf = Join-Path $installRoot 'qt.conf'
        BackupDir = Join-Path $toolDir 'backups'
        QtConfBackup = Join-Path $toolDir 'backups/qt.conf.before-layout-dpi-patch'
    }
}

function Read-TextNoBom {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
}

function Write-TextNoBom {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Get-QtConfWindowsArguments {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $lines = $Content -split "`r?`n"
    $platformsIndex = -1

    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match '^\s*\[Platforms\]\s*$') {
            $platformsIndex = $index
            break
        }
    }

    if ($platformsIndex -lt 0) {
        return $null
    }

    for ($index = $platformsIndex + 1; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($line -match '^\s*\[[^\]]+\]\s*$') {
            return $null
        }
        if ($line -match '^\s*WindowsArguments\s*=\s*(.*)$') {
            return $Matches[1].Trim()
        }
    }

    $null
}

function ConvertTo-DpiUnawareQtConf {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($Content -split "`r?`n")) {
        if ($line.Length -gt 0 -or $lines.Count -gt 0) {
            $lines.Add($line)
        }
    }

    while ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -eq '') {
        $lines.RemoveAt($lines.Count - 1)
    }

    $platformsIndex = -1
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match '^\s*\[Platforms\]\s*$') {
            $platformsIndex = $index
            break
        }
    }

    if ($platformsIndex -lt 0) {
        if ($lines.Count -gt 0) {
            $lines.Add('')
        }
        $lines.Add('[Platforms]')
        $lines.Add('WindowsArguments = fontengine=freetype dpiawareness=0')
        return ($lines -join [Environment]::NewLine) + [Environment]::NewLine
    }

    $sectionEnd = $lines.Count
    for ($index = $platformsIndex + 1; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match '^\s*\[[^\]]+\]\s*$') {
            $sectionEnd = $index
            break
        }
    }

    $argumentsIndex = -1
    $arguments = ''
    for ($index = $platformsIndex + 1; $index -lt $sectionEnd; $index++) {
        if ($lines[$index] -match '^\s*WindowsArguments\s*=\s*(.*)$') {
            $argumentsIndex = $index
            $arguments = $Matches[1].Trim()
            break
        }
    }

    $tokens = @()
    if ($arguments.Length -gt 0) {
        $tokens = $arguments -split '\s+' | Where-Object { $_ -and ($_ -notmatch '^dpiawareness=') }
    }
    $tokens = @($tokens) + 'dpiawareness=0'
    $newArgumentsLine = 'WindowsArguments = ' + ($tokens -join ' ')

    if ($argumentsIndex -ge 0) {
        $lines[$argumentsIndex] = $newArgumentsLine
    }
    else {
        $lines.Insert($platformsIndex + 1, $newArgumentsLine)
    }

    ($lines -join [Environment]::NewLine) + [Environment]::NewLine
}

function Test-BillfishLayoutPatchStatus {
    [CmdletBinding()]
    param()

    $paths = Get-BillfishLayoutPatchPaths
    $qtConfExists = Test-Path -LiteralPath $paths.QtConf -PathType Leaf
    $backupExists = Test-Path -LiteralPath $paths.QtConfBackup -PathType Leaf
    $billfishExists = Test-Path -LiteralPath $paths.BillfishExe -PathType Leaf
    $arguments = $null

    if ($qtConfExists) {
        $arguments = Get-QtConfWindowsArguments -Content (Read-TextNoBom -Path $paths.QtConf)
    }

    $hasFreeType = $arguments -ne $null -and (($arguments -split '\s+') -contains 'fontengine=freetype')
    $hasDpiUnaware = $arguments -ne $null -and (($arguments -split '\s+') -contains 'dpiawareness=0')
    $isRunning = @(Get-Process -Name 'Billfish','Billfish.core','PluginWorker','PluginWorker64' -ErrorAction SilentlyContinue).Count -gt 0

    [pscustomobject]@{
        InstallRoot = $paths.InstallRoot
        QtConf = $paths.QtConf
        QtConfExists = $qtConfExists
        Backup = $paths.QtConfBackup
        BackupExists = $backupExists
        BillfishExeExists = $billfishExists
        WindowsArguments = $arguments
        HasFontengineFreetype = $hasFreeType
        HasDpiAwarenessZero = $hasDpiUnaware
        GlobalDpiOverrideDisabled = (-not $hasDpiUnaware)
        BillfishProcessRunning = $isRunning
        PatchInstalled = ($qtConfExists -and $backupExists -and $hasFreeType -and (-not $hasDpiUnaware))
    }
}

function Install-BillfishLayoutPatch {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param()

    $paths = Get-BillfishLayoutPatchPaths
    if (-not (Test-Path -LiteralPath $paths.BillfishExe -PathType Leaf)) {
        throw "Billfish.core.exe was not found at $($paths.BillfishExe)."
    }
    if (-not (Test-Path -LiteralPath $paths.QtConf -PathType Leaf)) {
        throw "qt.conf was not found at $($paths.QtConf)."
    }

    if ($PSCmdlet.ShouldProcess($paths.QtConf, 'back up qt.conf and keep global DPI override disabled')) {
        if (-not (Test-Path -LiteralPath $paths.BackupDir -PathType Container)) {
            New-Item -ItemType Directory -Path $paths.BackupDir -Force | Out-Null
        }
        if (-not (Test-Path -LiteralPath $paths.QtConfBackup -PathType Leaf)) {
            Copy-Item -LiteralPath $paths.QtConf -Destination $paths.QtConfBackup
        }
        $current = Read-TextNoBom -Path $paths.QtConf
        $restored = "[Platforms]$([Environment]::NewLine)WindowsArguments = fontengine=freetype$([Environment]::NewLine)"
        if ($current -ne $restored) {
            Write-TextNoBom -Path $paths.QtConf -Content $restored
            Write-Host 'qt.conf restored to default DPI behavior.'
        } else {
            Write-Host 'qt.conf already uses default DPI behavior.'
        }
    }
}

function Restore-BillfishLayoutPatch {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param()

    $paths = Get-BillfishLayoutPatchPaths
    if (-not (Test-Path -LiteralPath $paths.QtConfBackup -PathType Leaf)) {
        throw "Backup was not found at $($paths.QtConfBackup)."
    }

    if ($PSCmdlet.ShouldProcess($paths.QtConf, "restore from $($paths.QtConfBackup)")) {
        Copy-Item -LiteralPath $paths.QtConfBackup -Destination $paths.QtConf -Force
        Write-Host 'qt.conf restored from backup.'
    }
}

function Get-BillfishLaunchProfileEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('Standard', 'DpiRoundPreferFloor', 'DpiUnaware', 'SoftwareRender', 'NoHighDpi')]
        [string]$Profile
    )

    switch ($Profile) {
        'Standard' {
            return @{}
        }
        'DpiRoundPreferFloor' {
            return @{
                QT_SCALE_FACTOR_ROUNDING_POLICY = 'RoundPreferFloor'
            }
        }
        'DpiUnaware' {
            return @{}
        }
        'SoftwareRender' {
            return @{
                QT_OPENGL = 'software'
                QTWEBENGINE_CHROMIUM_FLAGS = '--disable-gpu --disable-gpu-compositing'
            }
        }
        'NoHighDpi' {
            return @{
                QT_ENABLE_HIGHDPI_SCALING = '0'
                QT_AUTO_SCREEN_SCALE_FACTOR = '0'
                QT_SCALE_FACTOR = '1'
                QT_SCREEN_SCALE_FACTORS = '1'
            }
        }
    }
}

function Set-BillfishLaunchProfileEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Variables
    )

    foreach ($name in $Variables.Keys) {
        Set-Item -Path "Env:$name" -Value $Variables[$name]
    }
}

function Start-BillfishLayoutPatchLauncher {
    [CmdletBinding()]
    param(
        [ValidateSet('Standard', 'DpiRoundPreferFloor', 'DpiUnaware', 'SoftwareRender', 'NoHighDpi')]
        [string]$Profile = 'Standard'
    )

    $paths=Get-BillfishLayoutPatchPaths;$unified=Join-Path $paths.InstallRoot 'start-billfish.ps1'
    if(-not(Test-Path -LiteralPath $unified -PathType Leaf)){throw "Unified Billfish launcher not found: $unified"}
    & $unified
}

function Start-BillfishNormalLauncher {
    [CmdletBinding()]
    param()

    $paths=Get-BillfishLayoutPatchPaths;$unified=Join-Path $paths.InstallRoot 'start-billfish.ps1'
    if(-not(Test-Path -LiteralPath $unified -PathType Leaf)){throw "Unified Billfish launcher not found: $unified"}
    & $unified
}
