using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace BillfishLauncher;

internal static class Program
{
    private const string DryRunVariable = "BILLFISH_WRAPPER_DRY_RUN";
    private const string DryRunOutputVariable = "BILLFISH_WRAPPER_DRY_RUN_FILE";
    private const string ErrorOutputVariable = "BILLFISH_WRAPPER_ERROR_FILE";
    private const string SuppressUiVariable = "BILLFISH_WRAPPER_SUPPRESS_UI";

    private static async Task<int> Main(string[] args)
    {
        try
        {
            string launcherDirectory = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string billfishRoot = File.Exists(Path.Combine(launcherDirectory, "start-billfish.ps1"))
                ? launcherDirectory
                : Directory.GetParent(launcherDirectory)?.FullName ?? launcherDirectory;
            string scriptPath = Path.Combine(billfishRoot, "start-billfish.ps1");

            if (!File.Exists(scriptPath))
            {
                return Fail($"start-billfish.ps1 not found: {scriptPath}");
            }

            string? powershellPath = ResolvePowerShell();
            if (powershellPath is null)
            {
                return Fail("PowerShell was not found. Install PowerShell 7 or restore Windows PowerShell.");
            }

            LauncherCommand command = new(powershellPath, billfishRoot, scriptPath, args);
            if (IsDryRun())
            {
                WriteDryRun(command);
                return 0;
            }

            using Process process = new();
            process.StartInfo.FileName = command.PowerShellPath;
            process.StartInfo.WorkingDirectory = command.WorkingDirectory;
            process.StartInfo.UseShellExecute = false;
            process.StartInfo.CreateNoWindow = true;
            process.StartInfo.RedirectStandardOutput = true;
            process.StartInfo.RedirectStandardError = true;
            foreach (string argument in command.Arguments)
            {
                process.StartInfo.ArgumentList.Add(argument);
            }

            if (!process.Start())
            {
                return Fail("PowerShell launch failed without an error.");
            }
            Task<string> outputTask = process.StandardOutput.ReadToEndAsync();
            Task<string> errorTask = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync().ConfigureAwait(false);
            string[] streams = await Task.WhenAll(outputTask, errorTask).ConfigureAwait(false);
            if (process.ExitCode != 0)
            {
                string detail = string.Join(Environment.NewLine, streams.Where(value => !string.IsNullOrWhiteSpace(value)).Select(value => value.Trim()));
                string message = string.IsNullOrWhiteSpace(detail)
                    ? $"Billfish launcher preflight failed with exit code {process.ExitCode}."
                    : $"Billfish launcher preflight failed with exit code {process.ExitCode}.{Environment.NewLine}{Environment.NewLine}{detail}";
                ReportFailure(message);
            }
            return process.ExitCode;
        }
        catch (Win32Exception ex)
        {
            return Fail($"Launch failed: {ex.Message}");
        }
        catch (Exception ex)
        {
            return Fail(ex.Message);
        }
    }

    private static string? ResolvePowerShell()
    {
        string?[] candidates =
        [
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "PowerShell", "7", "pwsh.exe"),
            ResolveOnPath("pwsh.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe"),
            ResolveOnPath("powershell.exe")
        ];

        foreach (string? candidate in candidates)
        {
            if (!string.IsNullOrWhiteSpace(candidate) && File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private static string? ResolveOnPath(string fileName)
    {
        string? pathValue = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrWhiteSpace(pathValue))
        {
            return null;
        }

        foreach (string entry in pathValue.Split(Path.PathSeparator))
        {
            if (string.IsNullOrWhiteSpace(entry))
            {
                continue;
            }

            string candidate = Path.Combine(entry.Trim('"'), fileName);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private static bool IsDryRun()
    {
        string? value = Environment.GetEnvironmentVariable(DryRunVariable);
        return value is "1" or "true" or "TRUE" or "True";
    }

    private static void WriteDryRun(LauncherCommand command)
    {
        string output = command.ToDiagnosticString();
        string? outputFile = Environment.GetEnvironmentVariable(DryRunOutputVariable);
        if (!string.IsNullOrWhiteSpace(outputFile))
        {
            File.WriteAllText(outputFile, output + Environment.NewLine, new UTF8Encoding(false));
            return;
        }

        Console.WriteLine(output);
    }

    private static int Fail(string message)
    {
        ReportFailure(message);
        return 1;
    }

    private static void ReportFailure(string message)
    {
        string? outputFile = Environment.GetEnvironmentVariable(ErrorOutputVariable);
        if (!string.IsNullOrWhiteSpace(outputFile))
        {
            File.WriteAllText(outputFile, message + Environment.NewLine, new UTF8Encoding(false));
        }
        string? suppressUi = Environment.GetEnvironmentVariable(SuppressUiVariable);
        if (suppressUi is not ("1" or "true" or "TRUE" or "True"))
        {
            ShowMessage(message);
        }
    }

    private static void ShowMessage(string message)
    {
        _ = MessageBoxW(IntPtr.Zero, message, "Billfish", 0x00000010);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int MessageBoxW(IntPtr hWnd, string lpText, string lpCaption, uint uType);
}

internal sealed class LauncherCommand
{
    public LauncherCommand(string powershellPath, string workingDirectory, string scriptPath, IReadOnlyList<string> passThroughArgs)
    {
        PowerShellPath = powershellPath;
        WorkingDirectory = workingDirectory;
        Arguments =
        [
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            scriptPath,
            .. passThroughArgs
        ];
    }

    public string PowerShellPath { get; }

    public string WorkingDirectory { get; }

    public IReadOnlyList<string> Arguments { get; }

    public string ToDiagnosticString()
    {
        return $"WorkingDirectory={Quote(WorkingDirectory)}{Environment.NewLine}Command={Quote(PowerShellPath)} {string.Join(' ', Arguments.Select(Quote))}";
    }

    private static string Quote(string value)
    {
        return "\"" + value.Replace("\"", "\\\"", StringComparison.Ordinal) + "\"";
    }
}
