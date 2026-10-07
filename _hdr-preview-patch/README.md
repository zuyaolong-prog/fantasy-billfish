# Billfish Embedded HDR Preview Patch

HDR -> SDR 修正在 `plugins\media_runtime\libMediaSDK64.dll` 的渲染链中完成. 10-bit YUV 路径使用 PQ EOTF, BT.2020 -> BT.709 转换和 SDR tone mapping. 软件解码设置确保视频进入修正后的 shader 路径.

已发布的运行目录包含该能力, 正常运行不需要 Python 或重新安装补丁. 目录可以放在任意位置, 直接打开根目录的 `Billfish.exe`. 快捷方式也指向这个入口. 资料库在 Billfish 内打开或切换.

以下命令均从仓库根目录执行. `<LibraryRoot>` 替换为实际资料库目录, 不是软件安装目录.

## 状态与维护

检查统一运行状态:

```powershell
.\_launcher-patch\status-unified-billfish.ps1 -BillfishRoot $PWD.Path -AsJson
```

仅在需要修复或重新安装时, 关闭 Billfish 和相关 PluginWorker, 使用 Python 执行:

```powershell
python .\_hdr-preview-patch\install-render-chain-shader.py --dry-run
python .\_hdr-preview-patch\install-render-chain-shader.py
python .\_hdr-preview-patch\disable-mediasdk-hwaccel.py
```

旧 LUT 数据库绑定清理需要明确指定资料库. 可先检查计划:

```powershell
python .\_hdr-preview-patch\disable-embedded-lut.py --library-root "<LibraryRoot>" --dry-run
python .\_hdr-preview-patch\disable-embedded-lut.py --library-root "<LibraryRoot>"
```

## 恢复

恢复需要本机维护时生成的 `_hdr-preview-patch\backups` 与 `_hdr-preview-patch\state`. Git 发布不包含这些本机文件. 没有备份时, 不可使用恢复命令还原原始 DLL. 恢复前关闭 Billfish 和相关 PluginWorker:

```powershell
python .\_hdr-preview-patch\disable-mediasdk-hwaccel.py --restore
python .\_hdr-preview-patch\install-render-chain-shader.py --restore
```

如果两项补丁连续安装, 恢复按相反顺序执行. 保留维护时的目录和对应状态, 因为状态记录了本机备份位置.

## 备用预览与取舍

`play-hdr-sdr.cmd` 和右键菜单提供便携 mpv 外部预览. 它们不是日常启动入口.

软件解码可能使高码率视频增加 CPU 使用量. 此设置用于让 10-bit HDR 视频进入已修正的 shader 路径.
