# Billfish AV1 Internal Player Patch

该补丁让内嵌查看器播放 AV1 MP4, 不转向外部播放器, 不创建代理视频. 已发布的运行目录包含此能力, 正常运行不需要重新安装或编译. 将完整目录放在任意位置并打开根目录的 `Billfish.exe`.

## 媒体链路

```text
ui_plugins\preview.dll / sdk_player
  -> plugins\media_runtime\libMediaSDK64.dll
  -> plugins\media_runtime\libFFmpeg64.dll
```

`libFFmpeg64.dll` 是 ABI shim. 它转发原 DLL 的导出, 仅接管 AV1 decoder 相关入口并通过 `dav1d.dll` 解码. 原始 DLL 保存在运行目录的 `plugins\media_runtime\libFFmpeg64_orig.dll`.

以下命令均从仓库根目录执行.

## 按需编译

重新安装前需要先生成 `_av1-internal-player-patch\build\libFFmpeg64.dll`. Git 发布不包含 build 目录. 编译需要 Python 的 pefile, MSVC x64 工具, FFmpeg 4.2.1 源码头文件及 dav1d 1.5.0 头文件:

```powershell
$env:FFMPEG_INCLUDE = "<FFmpeg-4.2.1-source-directory>"
$env:DAV1D_INCLUDE = "<dav1d-1.5.0-include-directory>"
.\_av1-internal-player-patch\build-shim.cmd
```

`FFMPEG_INCLUDE` 下应包含 `libavcodec\avcodec.h`, `DAV1D_INCLUDE` 下应包含 `dav1d\dav1d.h`. 编译脚本使用 PATH 中的 cl 或通过 vswhere 查找已安装的 MSVC, 无需固定 Visual Studio edition 或安装目录. SDK 路径必须显式提供.

## 重新安装与验证

仅在需要修复或重新安装时执行. 先关闭 Billfish 和相关 PluginWorker, 再运行:

```powershell
.\_av1-internal-player-patch\install-av1-internal-player.ps1
```

安装时创建本机备份. 要验证实际视频解码, 明确传入样例文件:

```powershell
.\_av1-internal-player-patch\install-av1-internal-player.ps1 -ValidationSamples "<AV1-video-file>", "<H264-or-HEVC-video-file>"
```

不提供样例时仅执行安装与文件校验, 不宣称验证了视频解码. 正常启动使用 `Billfish.exe`.

## 恢复

恢复依赖本机安装时生成的 `_av1-internal-player-patch\backups`. Git 发布不包含这些本机备份. 没有备份时, 不可用恢复命令还原之前的运行状态.

关闭 Billfish 和相关 PluginWorker 后执行:

```powershell
.\_av1-internal-player-patch\restore-av1-internal-player.ps1
```

也可通过 `-BackupDir "<BackupDirectory>"` 选择明确的备份目录.
