[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('Status', 'Register', 'Restore')][string]$Action = 'Status',
    [string]$BillfishRoot,
    [string]$BackupPath,
    [switch]$SetDefault
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($BillfishRoot)) { $BillfishRoot = Split-Path -Parent $PSScriptRoot }
$progId = 'Billfish.EaglePlugin'
$classPath = 'Software\Classes\' + $progId
$openWithPath = 'Software\Classes\.eagleplugin\OpenWithProgids'
$extensionPath = 'Software\Classes\.eagleplugin'
$userChoicePath = 'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.eagleplugin\UserChoice'
$launcher = Join-Path ([IO.Path]::GetFullPath($BillfishRoot)) 'Billfish.exe'
$openCommand = '"' + $launcher + '" --install-plugin "%1"'
$user = [Microsoft.Win32.Registry]::CurrentUser

# Query Windows' effective association, including the user's default-app choice.
# https://learn.microsoft.com/windows/win32/api/shlwapi/nf-shlwapi-assocquerystringw
if (-not ('BillfishLauncher.AssociationNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace BillfishLauncher {
    public static class AssociationNative {
        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int AssocQueryStringW(uint flags, uint kind, string association, string extra, StringBuilder output, ref uint size);
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(uint eventId, uint flags, IntPtr item1, IntPtr item2);
        public static string Query(uint kind) {
            uint size = 32768;
            var value = new StringBuilder((int)size);
            return AssocQueryStringW(0, kind, ".eagleplugin", "open", value, ref size) == 0 ? value.ToString() : null;
        }
    }
}
'@
}

function Get-AssociationStatus {
    $choice = $user.OpenSubKey($userChoicePath)
    try {
        $choicePresent = $null -ne $choice
        $choiceProgId = if ($choice) { [string]$choice.GetValue('ProgId') } else { $null }
    } finally { if ($choice) { $choice.Dispose() } }
    $extension = $user.OpenSubKey($extensionPath)
    try { $fallback = if ($extension) { [string]$extension.GetValue('') } else { $null } }
    finally { if ($extension) { $extension.Dispose() } }
    $effectiveExe = [BillfishLauncher.AssociationNative]::Query(2)
    $effectiveCommand = [BillfishLauncher.AssociationNative]::Query(1)
    $effectiveProgId = [BillfishLauncher.AssociationNative]::Query(20)
    [pscustomobject]@{
        UserChoicePresent = $choicePresent
        UserChoiceProgId = $choiceProgId
        CurrentUserClassDefault = $fallback
        EffectiveProgId = $effectiveProgId
        EffectiveExecutable = $effectiveExe
        EffectiveCommand = $effectiveCommand
        EffectiveUnifiedLauncher = ([string]::Equals($effectiveExe, $launcher, [StringComparison]::OrdinalIgnoreCase))
    }
}

function Read-RegistryTree {
    param([Microsoft.Win32.RegistryKey]$Key)
    if ($null -eq $Key) { return $null }
    $values = @($Key.GetValueNames() | ForEach-Object {
        [pscustomobject]@{ Name = $_; Kind = [string]$Key.GetValueKind($_); Value = $Key.GetValue($_, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) }
    })
    $children = @($Key.GetSubKeyNames() | ForEach-Object {
        $child = $Key.OpenSubKey($_)
        try { [pscustomobject]@{ Name = $_; Tree = (Read-RegistryTree $child) } } finally { $child.Dispose() }
    })
    [pscustomobject]@{ Values = $values; Children = $children }
}

function Write-RegistryTree {
    param([Microsoft.Win32.RegistryKey]$Key, [object]$Tree)
    foreach ($value in $Tree.Values) {
        $data = $value.Value
        switch ($value.Kind) {
            'Binary' { $data = [byte[]]$data }
            'None' { $data = [byte[]]$data }
            'DWord' { $data = [int]$data }
            'QWord' { $data = [long]$data }
            'MultiString' { $data = [string[]]$data }
        }
        $Key.SetValue([string]$value.Name, $data, [Microsoft.Win32.RegistryValueKind]::$($value.Kind))
    }
    foreach ($child in $Tree.Children) {
        $subKey = $Key.CreateSubKey([string]$child.Name)
        try { Write-RegistryTree $subKey $child.Tree } finally { $subKey.Dispose() }
    }
}

$existingClass = $user.OpenSubKey($classPath)
try { $classTree = Read-RegistryTree $existingClass } finally { if ($existingClass) { $existingClass.Dispose() } }
$openWith = $user.OpenSubKey($openWithPath)
$previousValue = $null
try {
    if ($openWith -and $openWith.GetValueNames() -contains $progId) {
        $previousValue = [pscustomobject]@{ Name = $progId; Kind = [string]$openWith.GetValueKind($progId); Value = $openWith.GetValue($progId) }
    }
} finally { if ($openWith) { $openWith.Dispose() } }

if ($Action -eq 'Status') {
    [pscustomobject]@{ ProgId = $progId; Launcher = $launcher; OpenCommand = $openCommand; Registered = ($null -ne $classTree); OpenWithListed = ($null -ne $previousValue); Association = (Get-AssociationStatus) }
    return
}
if ([string]::IsNullOrWhiteSpace($BackupPath)) { throw 'Specify -BackupPath for registration or restoration.' }
$BackupPath = [IO.Path]::GetFullPath($BackupPath)

if ($Action -eq 'Register') {
    if (-not [IO.File]::Exists($launcher)) { throw "Unified launcher not found: $launcher" }
    $before = Get-AssociationStatus
    if ($SetDefault -and $before.UserChoicePresent) {
        throw 'Windows has a protected UserChoice for .eagleplugin. Register without -SetDefault, then select Billfish through the standard Windows Open With default-app dialog. No registry values were changed.'
    }
    $extensionDefault = $null
    $extension = $user.OpenSubKey($extensionPath)
    try {
        if ($extension -and $extension.GetValueNames() -contains '') {
            $extensionDefault = [pscustomobject]@{ Name = ''; Kind = [string]$extension.GetValueKind(''); Value = $extension.GetValue('', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) }
        }
    } finally { if ($extension) { $extension.Dispose() } }
    $backup = [pscustomobject]@{ Version = 2; ProgId = $progId; OpenCommand = $openCommand; ClassTree = $classTree; OpenWithValue = $previousValue; DefaultAssociationChanged = [bool]$SetDefault; ExtensionDefault = $extensionDefault }
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($backup | ConvertTo-Json -Depth 100))
    $stream = [IO.File]::Open($BackupPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
    $key = $user.CreateSubKey($classPath)
    try {
        $key.SetValue('', 'Billfish Eagle plugin package')
        $commandKey = $key.CreateSubKey('shell\open\command')
        try { $commandKey.SetValue('', $openCommand) } finally { $commandKey.Dispose() }
        $iconKey = $key.CreateSubKey('DefaultIcon')
        try { $iconKey.SetValue('', '"' + $launcher + '",0') } finally { $iconKey.Dispose() }
    } finally { $key.Dispose() }
    $key = $user.CreateSubKey($openWithPath)
    try { $key.SetValue($progId, '', [Microsoft.Win32.RegistryValueKind]::String) } finally { $key.Dispose() }
    if ($SetDefault) {
        $key = $user.CreateSubKey($extensionPath)
        try { $key.SetValue('', $progId, [Microsoft.Win32.RegistryValueKind]::String) } finally { $key.Dispose() }
    }
}
else {
    $backup = [IO.File]::ReadAllText($BackupPath) | ConvertFrom-Json
    if ($backup.Version -ne 2 -or $backup.ProgId -cne $progId) { throw 'Invalid Billfish association backup.' }
    if ($backup.DefaultAssociationChanged -and (Get-AssociationStatus).CurrentUserClassDefault -cne $progId) {
        throw 'The current-user .eagleplugin default changed after this backup. Restore was stopped to preserve the newer choice.'
    }
    $current = $user.OpenSubKey($classPath + '\shell\open\command')
    try {
        if ($current -and $current.GetValue('') -cne $backup.OpenCommand) { throw 'Billfish registration changed after this backup. Restore was stopped to preserve the newer registration.' }
    } finally { if ($current) { $current.Dispose() } }
    $user.DeleteSubKeyTree($classPath, $false)
    if ($backup.ClassTree) {
        $key = $user.CreateSubKey($classPath)
        try { Write-RegistryTree $key $backup.ClassTree } finally { $key.Dispose() }
    }
    $key = $user.OpenSubKey($openWithPath, $true)
    if ($key) {
        try {
            $key.DeleteValue($progId, $false)
            if ($backup.OpenWithValue) { Write-RegistryTree $key ([pscustomobject]@{ Values = @($backup.OpenWithValue); Children = @() }) }
        } finally { $key.Dispose() }
    }
    if ($backup.DefaultAssociationChanged) {
        $key = $user.OpenSubKey($extensionPath, $true)
        try {
            $key.DeleteValue('', $false)
            if ($backup.ExtensionDefault) { Write-RegistryTree $key ([pscustomobject]@{ Values = @($backup.ExtensionDefault); Children = @() }) }
        } finally { $key.Dispose() }
    }
}
[BillfishLauncher.AssociationNative]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
[pscustomobject]@{ Outcome = $Action; ProgId = $progId; BackupPath = $BackupPath; DefaultAssociationChanged = [bool]$backup.DefaultAssociationChanged; Association = (Get-AssociationStatus) }
