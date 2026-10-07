# Billfish layout DPI patch

这个补丁用于修复 Billfish 在 Windows 缩放/DPI/GPU 场景下出现的内容区、缩略图、预览区或右侧信息栏坐标错乱问题。

当前默认状态不再全局启用 `dpiawareness=0`, 因为它可能导致文件夹树的小图标消失。安装目录里的 `qt.conf` 应保持:

```ini
[Platforms]
WindowsArguments = fontengine=freetype
```

它不修改 DLL, 不修改嵌入资源, 不修改素材库数据库, 不移动你的文件。DPI/GPU 实验只通过下面的专用启动器临时启用。

## 文件

- `BillfishLayoutDpiPatch.ps1`: 核心函数库。
- `install-layout-dpi-patch.ps1`: 备份 `..\qt.conf` 并安装补丁。
- `status-layout-dpi-patch.ps1`: 检查补丁和备份状态。
- `restore-layout-dpi-patch.ps1`: 从备份恢复原始 `qt.conf`。
- `launch-billfish-layout-patch.ps1`: 在 Billfish 未运行时启动 `..\Billfish.exe`。
- `launch-gpu-safe.ps1`: 用 software OpenGL / Qt Quick software / WebEngine disable-gpu profile 启动。
- `launch-dpi-rounding.ps1`: 用 `QT_SCALE_FACTOR_ROUNDING_POLICY=RoundPreferFloor` profile 启动。
- `launch-dpi-unaware.ps1`: 用临时 `-platform windows:fontengine=freetype,dpiawareness=0` profile 启动。
- `launch-no-highdpi.ps1`: 用禁用 Qt high-DPI scaling 的诊断 profile 启动。
- `install.cmd`, `status.cmd`, `restore.cmd`, `launch.cmd`, `launch-gpu-safe.cmd`, `launch-dpi-rounding.cmd`, `launch-dpi-unaware.cmd`, `launch-no-highdpi.cmd`: 可双击入口。
- `backups\qt.conf.before-layout-dpi-patch`: 原始 `qt.conf` 备份。

## 使用

当前补丁已经安装。默认启动方式是直接运行 Billfish 安装目录中的统一入口:

```text
Billfish.exe
```

或者双击:

```text
start-billfish.cmd
```

默认启动不设置 `dpiawareness=0`, 不设置 software OpenGL, 应恢复文件夹树小图标。

如果想检查状态, 双击:

```text
status.cmd
```

如果想重新安装补丁, 双击:

```text
install.cmd
```

如果想恢复原始配置, 双击:

```text
restore.cmd
```

如果想用补丁目录里的普通启动器打开 Billfish, 双击:

```text
launch.cmd
```

`launch-gpu-safe.cmd` 不是默认启动方式。只有普通启动仍然出现重复绘制、内容块错位或预览区漂移时, 才先完全关闭 Billfish, 确认任务管理器里没有 `Billfish.exe`, 再双击:

```text
launch-gpu-safe.cmd
```

这个 profile 会为本次启动设置:

```text
QT_OPENGL=software
QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu --disable-gpu-compositing
```

如果 `launch-gpu-safe.cmd` 修好错位但变慢, 再测试较轻量的:

```text
launch-dpi-rounding.cmd
```

如果还是错位, 再测试临时 DPI-unaware profile:

```text
launch-dpi-unaware.cmd
```

如果还是错位, 最后测试诊断 profile:

```text
launch-no-highdpi.cmd
```

## 预期效果

`dpiawareness=0` 会让 Windows 统一接管这个旧 Qt app 的 DPI 缩放, 避免 Qt 5.15 程序内部使用高 DPI 坐标时出现非整数缩放导致的控件/内容错位。

如果三个特殊启动 profile 都无效, 下一步再考虑更高风险的 `preview.dll`/`bfright.dll` 资源级补丁。
