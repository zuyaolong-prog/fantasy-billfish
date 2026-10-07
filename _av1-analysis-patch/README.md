# Billfish AV1 Analysis Patch

该维护工具修复 AV1 MP4 的分析失败, 封面, 尺寸和时长元数据. 它通过 AV1-capable FFmpeg 读取视频并更新资料库, 不转码原视频. 内嵌 AV1 播放由 `_av1-internal-player-patch` 提供.

正常启动已发布的 Billfish 不需要运行本工具或重新安装补丁. 直接打开根目录的 `Billfish.exe`.

以下命令均从仓库根目录执行. `<LibraryRoot>` 替换为实际资料库目录. 维护工具需要 Python 和 Pillow; FFmpeg/ffprobe 优先使用 `tools\ffmpeg\bin`, 其次使用 PATH. 也可以分别通过 `--ffmpeg` 和 `--ffprobe` 指定工具文件.

## 扫描与修复

不带 `--apply` 时输出修复计划, 不写资料库数据库和正式预览图. 扫描会保存缓存, 分析会生成临时预览图:

```powershell
python .\_av1-analysis-patch\fix-av1-analysis.py --library-root "<LibraryRoot>" --scan-av1
```

关闭 Billfish 和相关 PluginWorker 后执行修复:

```powershell
python .\_av1-analysis-patch\fix-av1-analysis.py --library-root "<LibraryRoot>" --scan-av1 --apply
```

扫描所有可读且分析失败或元数据缺失的 MP4:

```powershell
python .\_av1-analysis-patch\fix-av1-analysis.py --library-root "<LibraryRoot>" --scan-mp4 --apply
.\_av1-analysis-patch\apply-readable-mp4-analysis.cmd --library-root "<LibraryRoot>"
```

`--scan-av1` 与 `--scan-mp4` 必须选择其一. 可用 `--targets <file_id> ...` 筛选扫描结果, 或用 `--limit <count>` 限制数量. 不再维护固定本机样例列表.

也可通过 `BILLFISH_LIBRARY_ROOT` 提供资料库目录. 未指定资料库时拒绝运行, 不猜测磁盘位置. 不可读的云占位文件会跳过, 下载到本机后可再次扫描.

## 备份与恢复

应用修改前备份 `<LibraryRoot>\.bf\billfish.db` 和受影响的 `.bf\.preview` 文件. 本机备份保存在 `_av1-analysis-patch\backups`, 缓存与临时文件保存在 `state` 和 `work`. Git 发布不包含这些本机数据.

恢复需要此工具生成的本机备份, 并要求备份中的资料库身份与当前目录一致. 缺少资料库身份的历史备份或属于其他资料库的备份会拒绝恢复. 关闭 Billfish 和相关 PluginWorker 后执行:

```powershell
python .\_av1-analysis-patch\fix-av1-analysis.py --library-root "<LibraryRoot>" --restore-latest
```

恢复最新备份会覆盖资料库数据库, 包括备份之后的其他数据库变更. 请先另行保存当前资料库.
