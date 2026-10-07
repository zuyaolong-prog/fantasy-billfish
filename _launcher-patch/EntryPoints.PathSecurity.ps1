Set-StrictMode -Version 3.0

if (-not ('Billfish.Launcher.PathNativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
namespace Billfish.Launcher {
    public static class PathNativeMethods {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern uint GetFinalPathNameByHandle(IntPtr handle, StringBuilder path, uint length, uint flags);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] public static extern bool CloseHandle(IntPtr handle);
    }

    public sealed class VerifiedDirectoryHandle : IDisposable {
        internal IntPtr Handle;
        internal VerifiedDirectoryHandle(IntPtr handle) { Handle = handle; }
        public void Dispose() {
            if (Handle != new IntPtr(-1) && Handle != IntPtr.Zero) {
                HandleNativeMethods.CloseHandle(Handle);
                Handle = IntPtr.Zero;
            }
            GC.SuppressFinalize(this);
        }
        ~VerifiedDirectoryHandle() { Dispose(); }
    }

    public static class HandleNativeMethods {
        const uint FILE_LIST_DIRECTORY = 0x00000001;
        const uint FILE_ADD_FILE = 0x00000002;
        const uint FILE_DELETE_CHILD = 0x00000040;
        const uint FILE_READ_ATTRIBUTES = 0x00000080;
        const uint DELETE = 0x00010000;
        const uint SYNCHRONIZE = 0x00100000;
        const uint GENERIC_READ = 0x80000000;
        const uint GENERIC_WRITE = 0x40000000;
        const uint OPEN_EXISTING = 3;
        const uint FILE_CREATE = 2;
        const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        const uint FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000;
        const uint FILE_NON_DIRECTORY_FILE = 0x00000040;
        const uint FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020;
        const uint OBJ_CASE_INSENSITIVE = 0x00000040;
        const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;
        const uint FILE_ATTRIBUTE_DIRECTORY = 0x10;
        const uint FILE_ATTRIBUTE_REPARSE_POINT = 0x400;
        const int FileAttributeTagInfo = 9;
        const int FileRenameInformation = 10;
        const int FileDispositionInformation = 13;

        [StructLayout(LayoutKind.Sequential)]
        struct FILE_ATTRIBUTE_TAG_INFO { public uint FileAttributes; public uint ReparseTag; }
        [StructLayout(LayoutKind.Sequential)]
        struct IO_STATUS_BLOCK { public IntPtr Status; public UIntPtr Information; }
        [StructLayout(LayoutKind.Sequential)]
        struct UNICODE_STRING { public ushort Length; public ushort MaximumLength; public IntPtr Buffer; }
        [StructLayout(LayoutKind.Sequential)]
        struct OBJECT_ATTRIBUTES {
            public int Length;
            public IntPtr RootDirectory;
            public IntPtr ObjectName;
            public uint Attributes;
            public IntPtr SecurityDescriptor;
            public IntPtr SecurityQualityOfService;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern uint GetFinalPathNameByHandle(IntPtr handle, StringBuilder path, uint length, uint flags);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] static extern bool GetFileInformationByHandleEx(IntPtr handle, int infoClass, out FILE_ATTRIBUTE_TAG_INFO info, uint size);
        [DllImport("ntdll.dll")]
        static extern int NtSetInformationFile(IntPtr handle, out IO_STATUS_BLOCK ioStatus, IntPtr info, uint size, int infoClass);
        [DllImport("ntdll.dll")]
        static extern int NtCreateFile(out IntPtr handle, uint desiredAccess, ref OBJECT_ATTRIBUTES objectAttributes,
            out IO_STATUS_BLOCK ioStatus, IntPtr allocationSize, uint fileAttributes, uint shareAccess,
            uint createDisposition, uint createOptions, IntPtr eaBuffer, uint eaLength);
        [DllImport("ntdll.dll")]
        static extern uint RtlNtStatusToDosError(int status);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] static extern bool ReadFile(IntPtr handle, byte[] buffer, uint bytesToRead, out uint bytesRead, IntPtr overlapped);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] static extern bool WriteFile(IntPtr handle, byte[] buffer, uint bytesToWrite, out uint bytesWritten, IntPtr overlapped);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] static extern bool FlushFileBuffers(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)] internal static extern bool CloseHandle(IntPtr handle);

        static Exception Win32(string operation) {
            int error = Marshal.GetLastWin32Error();
            return new System.ComponentModel.Win32Exception(error, operation + " (Win32 " + error.ToString() + ")");
        }

        static Exception NtStatus(string operation, int status) {
            int error = unchecked((int)RtlNtStatusToDosError(status));
            return new System.ComponentModel.Win32Exception(error, operation + " (NTSTATUS 0x" + status.ToString("X8") + ", Win32 " + error.ToString() + ")");
        }

        static VerifiedDirectoryHandle OpenDirectory(string path, uint access, string operation) {
            IntPtr handle = CreateFile(path, access, 7, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, IntPtr.Zero);
            if (handle == new IntPtr(-1)) throw Win32(operation);
            return new VerifiedDirectoryHandle(handle);
        }

        public static VerifiedDirectoryHandle OpenDirectory(string path) {
            return OpenDirectory(path, FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_DELETE_CHILD | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                "Unable to open verified destination parent");
        }

        public static VerifiedDirectoryHandle OpenDirectoryForTopologyPlacement(string path) {
            return OpenDirectory(path, FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                "Unable to open verified topology destination parent");
        }

        public static string GetFinalPath(VerifiedDirectoryHandle directory) {
            if (directory == null || directory.Handle == IntPtr.Zero) throw new ObjectDisposedException("directory");
            StringBuilder value = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandle(directory.Handle, value, (uint)value.Capacity, 0);
            if (length == 0 || length >= value.Capacity) throw Win32("Unable to resolve verified destination parent");
            return value.ToString();
        }

        static IntPtr OpenOrdinarySource(string path) {
            IntPtr handle = CreateFile(path, GENERIC_READ | FILE_READ_ATTRIBUTES | SYNCHRONIZE, 1, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT, IntPtr.Zero);
            if (handle == new IntPtr(-1)) throw Win32("Unable to open staged shortcut");
            FILE_ATTRIBUTE_TAG_INFO info;
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, out info, (uint)Marshal.SizeOf(typeof(FILE_ATTRIBUTE_TAG_INFO)))) {
                int error = Marshal.GetLastWin32Error(); CloseHandle(handle);
                throw new System.ComponentModel.Win32Exception(error, "Unable to inspect staged shortcut");
            }
            if ((info.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
                CloseHandle(handle);
                throw new InvalidOperationException("Staged shortcut must be an ordinary file.");
            }
            return handle;
        }

        static IntPtr CreateRelativeTemporary(VerifiedDirectoryHandle parent) {
            if (parent == null || parent.Handle == IntPtr.Zero) throw new ObjectDisposedException("parent");
            for (int attempt = 0; attempt < 16; attempt++) {
                string leaf = ".billfish-stage-" + Guid.NewGuid().ToString("N") + ".tmp";
                IntPtr nameBuffer = Marshal.StringToHGlobalUni(leaf);
                IntPtr unicodeBuffer = IntPtr.Zero;
                try {
                    UNICODE_STRING unicode = new UNICODE_STRING();
                    unicode.Length = checked((ushort)(leaf.Length * 2));
                    unicode.MaximumLength = checked((ushort)((leaf.Length + 1) * 2));
                    unicode.Buffer = nameBuffer;
                    unicodeBuffer = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(UNICODE_STRING)));
                    Marshal.StructureToPtr(unicode, unicodeBuffer, false);
                    OBJECT_ATTRIBUTES attributes = new OBJECT_ATTRIBUTES();
                    attributes.Length = Marshal.SizeOf(typeof(OBJECT_ATTRIBUTES));
                    attributes.RootDirectory = parent.Handle;
                    attributes.ObjectName = unicodeBuffer;
                    attributes.Attributes = OBJ_CASE_INSENSITIVE;
                    IO_STATUS_BLOCK ioStatus;
                    IntPtr handle;
                    int status = NtCreateFile(out handle, GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                        ref attributes, out ioStatus, IntPtr.Zero, FILE_ATTRIBUTE_NORMAL, 1, FILE_CREATE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_FLAG_OPEN_REPARSE_POINT,
                        IntPtr.Zero, 0);
                    if (status >= 0) return handle;
                    if (unchecked((uint)status) != 0xC0000035U) throw NtStatus("Unable to create handle-relative destination staging file", status);
                }
                finally {
                    if (unicodeBuffer != IntPtr.Zero) Marshal.FreeHGlobal(unicodeBuffer);
                    Marshal.FreeHGlobal(nameBuffer);
                }
            }
            throw new IOException("Unable to allocate a unique handle-relative destination staging file.");
        }

        static void DeleteByHandle(IntPtr handle) {
            IntPtr disposition = Marshal.AllocHGlobal(1);
            try {
                Marshal.WriteByte(disposition, 0, 1);
                IO_STATUS_BLOCK ioStatus;
                int status = NtSetInformationFile(handle, out ioStatus, disposition, 1, FileDispositionInformation);
                if (status < 0) throw NtStatus("Handle-relative staging cleanup failed", status);
            }
            finally { Marshal.FreeHGlobal(disposition); }
        }

        static void CopyAndFlush(IntPtr source, IntPtr destination) {
            byte[] buffer = new byte[131072];
            while (true) {
                uint read;
                if (!ReadFile(source, buffer, (uint)buffer.Length, out read, IntPtr.Zero)) throw Win32("Unable to read validated shortcut staging file");
                if (read == 0) break;
                uint offset = 0;
                while (offset < read) {
                    byte[] writeBuffer = buffer;
                    uint count = read - offset;
                    if (offset != 0) {
                        writeBuffer = new byte[(int)count];
                        Buffer.BlockCopy(buffer, (int)offset, writeBuffer, 0, (int)count);
                    }
                    uint written;
                    if (!WriteFile(destination, writeBuffer, count, out written, IntPtr.Zero)) throw Win32("Unable to write handle-relative destination staging file");
                    if (written == 0) throw new IOException("Writing the handle-relative destination staging file made no progress.");
                    offset += written;
                }
            }
            if (!FlushFileBuffers(destination)) throw Win32("Unable to flush handle-relative destination staging file");
        }

        static void RenameRelative(IntPtr source, VerifiedDirectoryHandle parent, string leafName) {
            if (parent == null || parent.Handle == IntPtr.Zero) throw new ObjectDisposedException("parent");
            if (String.IsNullOrWhiteSpace(leafName) || leafName.IndexOfAny(new char[] {'\\', '/'}) >= 0 || leafName == "." || leafName == "..")
                throw new ArgumentException("Destination must be a plain leaf name.", "leafName");
            byte[] name = Encoding.Unicode.GetBytes(leafName);
            int rootOffset = IntPtr.Size == 8 ? 8 : 4;
            int lengthOffset = rootOffset + IntPtr.Size;
            int nameOffset = lengthOffset + 4;
            int total = nameOffset + name.Length;
            IntPtr buffer = Marshal.AllocHGlobal(total);
            try {
                for (int i = 0; i < total; i++) Marshal.WriteByte(buffer, i, 0);
                Marshal.WriteByte(buffer, 0, 1);
                Marshal.WriteIntPtr(buffer, rootOffset, parent.Handle);
                Marshal.WriteInt32(buffer, lengthOffset, name.Length);
                Marshal.Copy(name, 0, IntPtr.Add(buffer, nameOffset), name.Length);
                IO_STATUS_BLOCK ioStatus;
                int status = NtSetInformationFile(source, out ioStatus, buffer, (uint)total, FileRenameInformation);
                if (status < 0) throw NtStatus("Atomic handle-relative shortcut placement failed", status);
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }

        public static void PlaceFile(string stagedPath, VerifiedDirectoryHandle parent, string leafName) {
            IntPtr source = OpenOrdinarySource(stagedPath);
            IntPtr temporary = IntPtr.Zero;
            bool renamed = false;
            try {
                temporary = CreateRelativeTemporary(parent);
                CopyAndFlush(source, temporary);
                RenameRelative(temporary, parent, leafName);
                renamed = true;
            }
            finally {
                CloseHandle(source);
                if (temporary != IntPtr.Zero) {
                    if (!renamed) { DeleteByHandle(temporary); }
                    CloseHandle(temporary);
                }
            }
        }

        public static void ReplaceAndDeleteFile(VerifiedDirectoryHandle parent, string leafName) {
            IntPtr temporary = IntPtr.Zero;
            bool renamed = false;
            try {
                temporary = CreateRelativeTemporary(parent);
                if (!FlushFileBuffers(temporary)) throw Win32("Unable to flush handle-relative deletion staging file");
                RenameRelative(temporary, parent, leafName);
                renamed = true;
                DeleteByHandle(temporary);
            }
            finally {
                if (temporary != IntPtr.Zero) {
                    if (!renamed) { DeleteByHandle(temporary); }
                    CloseHandle(temporary);
                }
            }
        }
    }
}
'@
}

function ConvertFrom-BillfishDevicePath {
    param([string]$Path)
    if ($Path.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) { return '\\' + $Path.Substring(8) }
    if ($Path.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $Path.Substring(4) }
    $Path
}

function Resolve-BillfishExistingFinalPath {
    param([Parameter(Mandatory = $true)][string]$LiteralPath, [string]$Context = 'path')
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-Path -LiteralPath $full)) { throw "$Context does not exist: $full" }
    $handle = [Billfish.Launcher.PathNativeMethods]::CreateFile($full, 0, 7, [IntPtr]::Zero, 3, 0x02000000, [IntPtr]::Zero)
    if ($handle -eq [IntPtr](-1)) { $code=[Runtime.InteropServices.Marshal]::GetLastWin32Error();throw "Unable to open $Context for final-path verification (Win32 $code): $full" }
    try {
        $builder=New-Object Text.StringBuilder 32768
        $length=[Billfish.Launcher.PathNativeMethods]::GetFinalPathNameByHandle($handle,$builder,$builder.Capacity,0)
        if($length-eq0-or$length-ge$builder.Capacity){$code=[Runtime.InteropServices.Marshal]::GetLastWin32Error();throw "Unable to resolve final $Context path (Win32 $code): $full"}
        [IO.Path]::GetFullPath((ConvertFrom-BillfishDevicePath $builder.ToString())).TrimEnd('\','/')
    }
    finally { [void][Billfish.Launcher.PathNativeMethods]::CloseHandle($handle) }
}

function Resolve-BillfishProjectedFinalPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $full=[IO.Path]::GetFullPath($Path);$existing=$full;$suffix=New-Object 'System.Collections.Generic.List[string]'
    while(-not(Test-Path -LiteralPath $existing)){$leaf=Split-Path -Leaf $existing;if([string]::IsNullOrWhiteSpace($leaf)){throw "No existing ancestor for path: $full"};$suffix.Insert(0,$leaf);$existing=Split-Path -Parent $existing}
    $resolved=Resolve-BillfishExistingFinalPath $existing 'existing ancestor'
    foreach($segment in $suffix){$resolved=Join-Path $resolved $segment}
    [IO.Path]::GetFullPath($resolved).TrimEnd('\','/')
}

function New-BillfishTrustedRootPolicy {
    param([Parameter(Mandatory = $true)][string]$Root,[string]$Name='trusted root')
    $raw=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    [pscustomobject]@{Name=$Name;RawRoot=$raw;FinalRoot=(Resolve-BillfishProjectedFinalPath $raw)}
}

function Test-BillfishPathHasPrefix {
    param([string]$Path,[string]$Root)
    $Path.Equals($Root,[StringComparison]::OrdinalIgnoreCase)-or$Path.StartsWith($Root.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)
}

function Assert-BillfishNoChildReparsePoint {
    param([string]$Path,[object]$Policy,[switch]$IncludeLeaf)
    $full=[IO.Path]::GetFullPath($Path);$raw=$Policy.RawRoot
    if(-not(Test-BillfishPathHasPrefix $full $raw)){throw "Path is outside trusted root '$($Policy.Name)': $full"}
    if($full.Equals($raw,[StringComparison]::OrdinalIgnoreCase)){return}
    $relative=$full.Substring($raw.Length).TrimStart('\');$segments=@($relative.Split([char]'\')|Where-Object{$_})
    $current=$raw
    for($index=0;$index-lt$segments.Count;$index++){
        $current=Join-Path $current $segments[$index]
        $isLeaf=$index-eq($segments.Count-1)
        if((Test-Path -LiteralPath $current)-and($IncludeLeaf-or-not$isLeaf)){
            $item=Get-Item -LiteralPath $current -Force
            if(($item.Attributes-band[IO.FileAttributes]::ReparsePoint)-ne0){throw "Untrusted reparse point below '$($Policy.Name)': $current"}
        }
    }
}

function Assert-BillfishPathWithinPolicy {
    param([Parameter(Mandatory = $true)][string]$Path,[Parameter(Mandatory = $true)][object]$Policy,[switch]$IncludeLeaf,[string]$RequiredExtension)
    $full=[IO.Path]::GetFullPath($Path)
    if($RequiredExtension-and-not[IO.Path]::GetExtension($full).Equals($RequiredExtension,[StringComparison]::OrdinalIgnoreCase)){throw "Path must use extension '$RequiredExtension': $full"}
    Assert-BillfishNoChildReparsePoint $full $Policy -IncludeLeaf:$IncludeLeaf
    $final=Resolve-BillfishProjectedFinalPath $full
    if(-not(Test-BillfishPathHasPrefix $final $Policy.FinalRoot)){throw "Final path escapes trusted root '$($Policy.Name)': $full -> $final"}
    [pscustomobject]@{RawPath=$full;FinalPath=$final;Policy=$Policy}
}

function Get-BillfishEntryPointRootPolicies {
    param([Parameter(Mandatory = $true)][object]$Context)
    @($Context.Roots|ForEach-Object{New-BillfishTrustedRootPolicy $_ 'entry-point root'})
}

function Resolve-BillfishShortcutPolicy {
    param([Parameter(Mandatory = $true)][string]$Path,[Parameter(Mandatory = $true)][object[]]$Policies,[switch]$IncludeLeaf)
    $full=[IO.Path]::GetFullPath($Path)
    foreach($policy in $Policies){
        if(Test-BillfishPathHasPrefix $full $policy.RawRoot){return Assert-BillfishPathWithinPolicy $full $policy -IncludeLeaf:$IncludeLeaf -RequiredExtension '.lnk'}
    }
    throw "Shortcut path is outside all fixed entry-point roots: $full"
}

function Get-BillfishEntryPointAllowlist {
    param([Parameter(Mandatory = $true)][object]$Context)
    @(@($Context.Required) + @($Context.Optional) | ForEach-Object { [IO.Path]::GetFullPath([string]$_) })
}

function Assert-BillfishAllowedShortcutPath {
    param([Parameter(Mandatory = $true)][string]$Path,[Parameter(Mandatory = $true)][object]$Context)
    $full=[IO.Path]::GetFullPath($Path)
    $allowed=@(Get-BillfishEntryPointAllowlist $Context | Where-Object { $_.Equals($full,[StringComparison]::OrdinalIgnoreCase) })
    if($allowed.Count-ne1){throw "Shortcut path is not one of the five fixed Billfish entry points: $full"}
    $full
}

function Assert-BillfishAllowedShortcutSet {
    param([Parameter(Mandatory = $true)][object[]]$Entries,[Parameter(Mandatory = $true)][object]$Context,[string]$Label='shortcut set')
    $seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $Entries){$path=Assert-BillfishAllowedShortcutPath ([string]$entry.path) $Context;if(-not$seen.Add($path)){throw "$Label contains a duplicate path: $path"}}
    foreach($required in @($Context.Required)){$canonical=[IO.Path]::GetFullPath([string]$required);if(-not$seen.Contains($canonical)){throw "$Label is missing required entry point: $canonical"}}
}

function Open-BillfishVerifiedShortcutParent {
    param([Parameter(Mandatory = $true)][string]$Destination,[Parameter(Mandatory = $true)][object]$Context,[switch]$AllowCreateParent)
    $full=Assert-BillfishAllowedShortcutPath $Destination $Context
    $parent=Split-Path -Parent $full
    if(-not(Test-Path -LiteralPath $parent -PathType Container)){
        if(-not$AllowCreateParent){throw "Shortcut destination parent does not exist: $parent"}
        New-Item -ItemType Directory -Path $parent -Force -ErrorAction Stop|Out-Null
    }
    $policies=Get-BillfishEntryPointRootPolicies $Context
    $resolved=Resolve-BillfishShortcutPolicy $full $policies -IncludeLeaf
    $relativeParent=$parent.Substring($resolved.Policy.RawRoot.Length).TrimStart('\')
    $expected=$resolved.Policy.FinalRoot
    if(-not[string]::IsNullOrWhiteSpace($relativeParent)){$expected=[IO.Path]::GetFullPath((Join-Path $expected $relativeParent)).TrimEnd('\','/')}
    $handle=[Billfish.Launcher.HandleNativeMethods]::OpenDirectory($parent)
    try{
        $opened=[IO.Path]::GetFullPath((ConvertFrom-BillfishDevicePath ([Billfish.Launcher.HandleNativeMethods]::GetFinalPath($handle)))).TrimEnd('\','/')
        if(-not$opened.Equals($expected,[StringComparison]::OrdinalIgnoreCase)){throw "Verified shortcut parent changed while it was opened: $parent"}
        [pscustomobject]@{Handle=$handle;Destination=$full;Leaf=[IO.Path]::GetFileName($full);OpenedParent=$opened}
    }catch{$handle.Dispose();throw}
}

function Resolve-BillfishRootIdentity {
    param([Parameter(Mandatory = $true)][string]$BillfishRoot)
    Resolve-BillfishExistingFinalPath $BillfishRoot 'BillfishRoot'
}
