# 搬到另一台 Windows 电脑

使用完整 portable 导出或完整 Release ZIP, 解压到公司电脑的普通可写目录.
可以换盘符和目录名, 不必保持 `E:\Billfish`. Source-only 导出用于维护代码,
不包含完整运行时, 不能当成可直接运行的软件包.

完整包包含 Billfish 唯一启动入口、已安装的 DPI/HDR/AV1/VideoTrim 能力,
以及 Python x64、Electron、FFmpeg、FFprobe、yt-dlp、维护版 Edge 扩展和
Native Messaging 注册脚本. 新电脑不需要另外安装 Python、Node 或 pip 包.

1. 解压后双击 `tools\initialize-portable.cmd`. 它检查包内 Python 与既有
   Billfish baseline, 并为当前用户注册 Edge Native Messaging host.
   无需管理员, 不修改浏览器 profile、policy 或系统 PATH.
2. 在 Edge 的 `edge://extensions` 开启开发人员模式, 选择 `加载解压缩的扩展`,
   指向本目录的 `browser-extension\maintained`. 固定 ID 应为
   `aloippladlgkfkcabiknfpokgcppcjkp`. 如果装有旧 Billfish 采集器, 手动停用旧版.
   同目录更新后在扩展页点击重新加载, 再刷新网页. 扩展目录改变时,
   移除旧维护版并从新目录重新加载.
3. 从根目录 `Billfish.exe` 启动, 打开公司已有素材库或创建新库.
   软件包不包含个人素材库、账号或插件的本机启用状态.

本机已完成实际 Edge Bilibili 自动入口的下载入库验收: 页面显示 `采集视频`,
点击后选择分辨率并确认, 1080x1920 视频下载到隔离验收库的 `浏览器采集`
文件夹, 完整标题、来源、缩略图和播放均已验证. 首次初始化或重新注册后,
本机曾出现 `Specified native messaging host not found`, 完全退出 Edge 再打开后
恢复; 具体失败原因尚未捕获, 不将静态注册检查视为浏览器端到端保证.
若新电脑也出现此错误, 先运行本页的 Check 检查注册和路径;
确认注册正常后可完整退出并重开 Edge, 但重启不保证修复注册问题.
公司电脑如果禁止加载开发人员扩展, 需遵循公司的现有浏览器管理规则.
初始化脚本不会修改该规则.

检查和撤销可在软件根目录使用:

```powershell
& .\tools\initialize-portable.ps1
& .\tools\initialize-portable.ps1 -Action Initialize
& .\tools\initialize-portable.ps1 -Action RestoreBrowserHost
```

默认操作只检查. Initialize 复用已有 Native Messaging 注册/备份机制,
为新目录重新生成绝对路径. RestoreBrowserHost 撤销本工具拥有的当前用户注册,
保留备份. 若之后再次移动本机安装目录, 先在旧位置撤销注册, 再到新位置初始化.
新目录不会覆盖无法验证归属的旧注册, 因此确认迁移完成前保留旧目录.
Release 的独立扩展 ZIP 与完整包内目录相同, 便于仅更新扩展;
视频采集仍依赖完整包及其 Native Messaging bridge, 无需同时加载两份维护版.

`.eagleplugin` 的默认打开方式属于每台电脑的用户设置, 不随复制自动迁移.
需要在新电脑启用时, 使用 `_launcher-patch\plugin-package-association.ps1`
的 Register/SetDefault 与备份机制; 已有 protected UserChoice 时使用 Windows
标准默认应用界面. 这不会更改 `eagle://` 协议.

从维护目录生成迁移包使用 `tools\export-portable.py`, 不直接复制含本机状态的
整个开发目录. 导出排除 `task-memory`, Native Messaging 的 `generated`,
`registration-state.json` 和 `backups`, 也不读取或复制 Windows registry、
Edge profile、cookies、个人 AppData 或素材库. 新机器首次注册时会生成自己的
manifest、wrapper、备份和状态. 日常插件按需要在新机器重新安装、启用.

AI 图片放大使用完整 portable 包内的
`tools\plugin-packages\ai-image-enlarger-1.0.9.eagleplugin`. 在公司电脑从
Billfish 设置的插件页选择该本地包安装, 然后手动开启. 新装默认关闭,
初始化脚本不自动启用插件, 不复制本机或 Eagle 的 AppData, 模型私密配置或账号.
包内携带维护版插件及已验证的 Real-ESRGAN Vulkan engine 和两个模型,
不需要另外安装 Python, Node 或 CUDA 来执行放大; 新电脑仍须具备可运行
Vulkan 的 GPU 和驱动. 本机 RTX 5090 的验证不代表公司电脑 GPU 已通过验收.

本机实际已通过原 UI 的 4x 对比与另存 PNG, 192 x 192 输入得到
768 x 768 输出. 替换原图的真实 GUI 验收也已通过, 同 ID 的 PNG 更新为
768 x 768, 标签/备注/来源保持, 新原生缩略图和大图已实际打开. 当前替换接口只支持库内
owned PNG/JPG/JPEG/WebP 的同扩展替换, 原文件 backup 与恢复记录保存在
该素材库的 `.bf\automation-replacements`, 应随素材库保留.
此包不代表通用 AI SDK, AI Search 或抠图插件已兼容.

独立 Billfish UI 控制 client 为 `automation\billfish-ui.cjs`, 从其实际安装位置
加载即可, 默认按 client 的相对位置定位安装根. 无 npm 依赖, 使用当前用户
Billfish host 的 named pipe, 不需要在新机器配置全局桌面控制或另建 MCP server.
先从根目录 `Billfish.exe` 启动, 再连接 client. 完整使用与控制范围见
[billfish-ui-control.md](billfish-ui-control.md). 本轮的部署/验收状态见
[task_state.md](../task-memory/task-billfish-independent-control-3d-trim/task_state.md).

当前完整包内置 3D/动画 viewer, 资料库就绪后自动启动, 支持的模型可直接双击打开.
Solid/Material 双按钮, 动画发现与切换, 播放速度/逐帧/骨架和自由漫游均随包提供.
不需要先安装额外插件才能使用内置 viewer. 既有 3D 格式扩展维护包为
`tools\plugin-packages\3d-format-extension-1.9.0.eagleplugin`.
它保留原插件本地资源, 不复制 Eagle 账号, 私密设置或付费模块.
在 Billfish 设置的插件页安装本地包, 然后手动开启. 新装默认关闭,
不复制本机启用状态. 插件 window 是设置页, 支持的模型可从原生双击打开 viewer.
当前 runtime 已测试 12 种格式的 viewer/thumbnail, OBJ 原生封面和动画 FBX
原生封面/双击预览已有实测. GLB 已原生导入, 新未选中 GLB 自动封面和 OBJ/FBX 封面跨重启保持已实测.
启用时通过原生类型动作补齐当前库缺少的声明后缀, 已有封面在关闭插件后保留.
这些本机结果不作为公司电脑 GPU/驱动已通过的保证. 原设置页市场列表需要联网,
可选付费模块与 Spline 网络行为不在基础本地文件测试的离线保证内.
全部声明后缀及公司电脑 GPU/驱动尚未完成实测, 详细覆盖见
[eagle-plugin-compatibility.md](eagle-plugin-compatibility.md).

2026-10-20 便携版运行基线的 preview DLL SHA-256 为
`002C700DEA52448467AD18C61CDD04957DB34E68D785B9D73AFFAE6372943B95`,
1,665,536 bytes, 230 项 runtime fingerprints, unified `Healthy:true`.
包内 `_launcher-patch/portable-runtime.json` 是逐文件校验依据,
Release 的 `SHA256SUMS.txt` 用于核对下载的 ZIP.
