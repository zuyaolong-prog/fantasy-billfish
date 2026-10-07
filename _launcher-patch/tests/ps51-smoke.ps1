[CmdletBinding(PositionalBinding = $false)]
param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
. (Join-Path $RepositoryRoot '_launcher-patch\Launcher.Core.ps1')
. (Join-Path $RepositoryRoot '_launcher-patch\EntryPoints.Core.ps1')

$mutex = Enter-BillfishLaunchMutex -BillfishRoot $RepositoryRoot
Exit-BillfishLaunchMutex -Mutex $mutex
$arguments = @('', 'plain', 'space value', 'a"b', 'C:\path with space\')
$commandLine = ConvertTo-WindowsCommandLine -ArgumentList $arguments
if ($commandLine -ne '"" plain "space value" "a\"b" "C:\path with space\\"') { throw "Unexpected Windows command line: $commandLine" }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Billfish.Launcher.Tests {
    public static class ArgvNative {
        [DllImport("shell32.dll", SetLastError = true)] public static extern IntPtr CommandLineToArgvW([MarshalAs(UnmanagedType.LPWStr)] string commandLine, out int argc);
        [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr memory);
    }
}
'@
$argc = 0
$argvPointer = [Billfish.Launcher.Tests.ArgvNative]::CommandLineToArgvW('fixture.exe ' + $commandLine, [ref]$argc)
if ($argvPointer -eq [IntPtr]::Zero) { throw 'CommandLineToArgvW failed.' }
try {
    $parsed = for ($index = 1; $index -lt $argc; $index++) {
        $pointer = [Runtime.InteropServices.Marshal]::ReadIntPtr($argvPointer, $index * [IntPtr]::Size)
        [Runtime.InteropServices.Marshal]::PtrToStringUni($pointer)
    }
}
finally { [void][Billfish.Launcher.Tests.ArgvNative]::LocalFree($argvPointer) }
if (($parsed | ConvertTo-Json -Compress) -ne ($arguments | ConvertTo-Json -Compress)) { throw 'Windows argv quoting did not round-trip.' }
$missing = Get-BillfishRunValue -Context (Get-BillfishEntryPointContext -FixtureRoot (Join-Path $env:TEMP ('billfish-missing-' + [guid]::NewGuid().ToString('N'))))
if ($missing.Exists) { throw 'Missing fixture Run value was reported as present.' }
[pscustomobject]@{ Outcome = 'Passed'; PowerShell = $PSVersionTable.PSVersion.ToString(); CommandLine = $commandLine }
