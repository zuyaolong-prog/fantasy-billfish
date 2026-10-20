# Billfish Unified Launcher

当前 v4 拓扑只有一个权威用户入口: `.\Billfish.exe`. 该文件是统一 wrapper. 冻结的厂商程序迁移为 `.\Billfish.core.exe`. 旧路径 `.\launcher\Billfish.exe` 在 v4 提交前删除, 不再作为兼容入口.

wrapper 从自身所在的 Billfish root 调用 `start-billfish.ps1`, 等待 PowerShell 完成并原样返回 exit code. 启动事务会完成以下工作:

- 按 `Billfish.core.exe` 的完整 executable path 检查 singleton.
- 验证 DPI, HDR, AV1 和 VideoTrim 四项持久补丁状态.
- 注入 `BILLFISH_INSTALL_ROOT` 和 `BILLFISH_LIBRARY_ROOT`, 设置统一运行环境, 并无损透传原始参数.
- 仅在全部检查通过后启动 `Billfish.core.exe`.

## Portable runtime

从 GitHub Releases 下载完整便携 ZIP, 解压到任意 Windows 11 x64 可写目录后运行根目录 `Billfish.exe`. Git 仓库只发布维护源码. wrapper 自包含 .NET runtime, 可使用 Windows 自带 PowerShell 5.1, 无需安装 .NET SDK 或 PowerShell 7. 不需要运行历史 migration 或 shortcut installer.

`portable-runtime.json` 是发行文件基线, 只保存相对路径和 fingerprint. `libFFmpeg64_orig.dll` 与 `preview.stock.bin` 是补丁运行时需要加载的厂商 payload, 必须随 runtime 分发. 维护人员替换 baseline DLL/EXE 后必须更新对应 fingerprint; 启动不会自动接受文件漂移或重装补丁.

素材库通过显式 `-LibraryRoot` 或当前用户 `%APPDATA%\Billfish\config\recent_lib.ini` 解析. 全新账户或默认库不可用时不注入旧路径, 由 Billfish UI 选择或创建素材库. 真实启动初始化 HDR 必需的 software-decode preferences; `-DryRun` 不创建账户配置.

系统快捷方式和 autostart 属于本机可选安装状态, 默认启动和状态检查均不创建或要求它们. 以下显式检查仅适用于保有原安装交易记录的本机:

```powershell
& .\_launcher-patch\status-unified-billfish.ps1 -CheckInstallationHistory
```

历史 topology manifests, pointers, backups 不可跨机器复用, 也不属于可移植运行所需文件.
## Build

构建只发布到 staging, 不会替换已安装的 root EXE:

```powershell
& .\_launcher-patch\build-wrapper.ps1
```

默认产物为 `.\_launcher-patch\staging\root-wrapper\Billfish.exe`. 失败的 build 不会覆盖上一个有效 staging 产物.

## v3 to v4 migration

v4 installer 要求当前 v3 entry-point manifest 为 validated `Active`, 并要求当前 v3 root vendor, legacy wrapper, staged root wrapper 和启动脚本都存在. 它先获取 user SID + resolved Billfish root entry mutex, 再获取 launch mutex. 进程和 lock preflight 在任何 filesystem 或 registry 写入之前完成. `Billfish.exe`, `Billfish.core.exe`, legacy wrapper 或 root 内 `PluginWorker*` 正在运行时, transaction 直接拒绝.

```powershell
& .\_launcher-patch\install-root-launcher.ps1
```

transaction 会备份 root vendor, legacy wrapper, staged wrapper, HKCU Run 和 validated v3 shortcut snapshot. 每个 phase mutation 前先原子写入 intent. phase 顺序为 `Core`, `RootWrapper`, `Entries`, `Run`, `LegacyWrapper`, `Pointer`. 文件发布使用已验证 destination parent handle 下的同卷临时文件和原子替换. root wrapper 提交后, current-user Desktop, Start Menu, Quick Launch 和 manifest 管理的其他 shortcut 指向 root `Billfish.exe`; icon 指向 `Billfish.core.exe`; HKCU Run 也指向 root wrapper.

manifest 保存 SHA-256 和 size. pointer, pending journal, transaction storage, backup leaf, shortcut allowlist 和 final path 都会验证. 安装中断或注入失败时, 下一次操作先恢复完整 v3 installed state. 重复 install 仅在 validated v4 topology 完整健康时返回 `AlreadyInstalled`; drift 或 backup tamper 会被拒绝.

## Restore v3

```powershell
& .\_launcher-patch\restore-root-launcher.ps1
```

v4 restore 不是恢复厂商原始入口, 而是恢复迁移前的完整 v3 installed architecture: root `Billfish.exe` 恢复为冻结 vendor, `launcher\Billfish.exe` 恢复为 legacy wrapper, `Billfish.core.exe` 删除, HKCU Run 和 shortcuts 恢复为 v3 installed snapshot. 最初 v3 entry installer 保存的 origin backup 和 active pointer 不会被重建或覆盖, 因而首次 origin backup contract 保持不变.

restore 在 mutation 前创建带 fingerprint 的 v4 rollback snapshot. phase 顺序为 `RootVendor`, `LegacyWrapper`, `Core`, `Entries`, `Run`, `Pointer`. 如果 restore 失败或进程中断, pending recovery 验证 rollback snapshot 后恢复完整 v4 state. 成功提交才移除 v4 active pointer.

`install-user-shortcuts.ps1` 和 `restore-user-shortcuts.ps1` 仍是 v3 entry transaction 的底层工具, 不是 v4 root topology 的安装或卸载入口.

## Status and tests

默认只读状态验证 portable runtime: root wrapper metadata, `Billfish.core.exe`, legacy wrapper 缺失, qt.conf 和 runtime 文件 SHA-256/size. 不要求旧账户配置, 安装 receipt, 开发 build 目录或任何历史 backup. 所有命令从软件根目录运行:

```powershell
& .\_launcher-patch\status-unified-billfish.ps1
```

fixture suite 覆盖 v3 to v4, idempotence, install/restore 全 phase failure, pending recovery, backup tamper, running-process zero-write preflight, cross-volume handle-relative placement, root wrapper dry-run, core singleton 和 Windows PowerShell 5.1:

```powershell
& .\_launcher-patch\tests\run-tests.ps1
```

## Local Eagle package ingress

The unified entry accepts one local package with `Billfish.exe "C:\Downloads\plugin.eagleplugin"` or `Billfish.exe --install-plugin "C:\Downloads\plugin.eagleplugin"`. The explicit command also accepts `.zip` packages. Package arguments are consumed by the launcher and never reach the vendor core. Other Billfish arguments keep their existing pass-through behavior.

The launcher uses `BILLFISH_PYTHON` or Python on PATH, matching the existing automation host. It waits up to 60 seconds for the current-user host, sends one `plugins.install` request with `source_path`, waits up to 120 seconds for the final result, then opens Billfish's plugin settings. New plugins remain disabled until enabled in that page. A timeout reports its `job_id`; query `jobs.get` with that ID before retrying installation. Failure to open settings after a completed installation reports that installation has succeeded and must not be repeated.

`plugin-package-association.ps1` is a current-user registration tool. Its default `Status` action is read-only and reports the effective Windows executable and command with `AssocQueryString`, together with the presence of a protected `UserChoice`. `Register -BackupPath <new JSON file>` adds the Billfish-owned `Billfish.EaglePlugin` ProgID and its `.eagleplugin\OpenWithProgids` value. Add `-SetDefault` to make Billfish the current-user `.eagleplugin` default when no protected `UserChoice` exists. If `UserChoice` exists, the script refuses that default change before mutation; register without `-SetDefault`, then use the standard Windows Open With default-app dialog. `Restore -BackupPath <same JSON file>` restores the previous ProgID tree, that one OpenWith value, and the previous current-user extension default when changed, preserving other applications' entries. No action writes the protected `UserChoice` or the `eagle://` protocol. Registration and default changes require the user's authorization. Keep the backup until the registration is no longer needed.

Focused checks without starting Billfish or writing registry:

```powershell
& .\_launcher-patch\tests\plugin-ingress.ps1
python .\_launcher-patch\tests\test_plugin_ingress.py
& .\_launcher-patch\plugin-package-association.ps1 -Action Status
```
