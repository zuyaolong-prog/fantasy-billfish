# Billfish 插件包安装与更新

当前安装器接收本地 `.eagleplugin`, `.zip` 或开发目录. 公开 operation 为 `plugins.install`, 参数统一为 `source_path`. 安装和插件业务运行是不同阶段: 安装只检查, 复制或解包资源, 新插件默认关闭; 开启后才启动其 Electron session. 系统默认关联及本地包 cold/warm 打开已通过实机验收, 插件核心业务与其他安装场景的状态见文末, 不因成功安装就宣称业务已全通过.

## 从下载包安装

在 Edge 下载文件后, 可以在 Billfish 设置的插件页选择该 `.eagleplugin` / `.zip` 文件, 或把 `.eagleplugin` 拖到 Billfish 主窗口. 开发时也可通过该页选择含根 manifest 的目录. 文件选择器支持包和目录入口, 主窗口拖放只接收本地 `.eagleplugin`, 不把任意 ZIP 或普通素材当成插件包.

用户已授权并完成当前用户的 `.eagleplugin` 默认关联, Windows `AssocQueryString` 实际查询确认 effective executable 为根目录 `Billfish.exe`. 本地包双击已通过冷启动和运行中交付验收. Edge 下载列表的 "打开" 使用同一 Windows 文件关联; 本轮工具中的下载直链被 `ERR_BLOCKED_BY_CLIENT` 拦截, 没有取得从 Edge 完整下载再打开的端到端结果, 不把本地打开验收扩大为该浏览器流程已通过. 分发经过根目录唯一 `Billfish.exe`, 不直接启动内部 core, 不接管 `eagle://` 协议.

launcher 支持以下形式, 本地 cold/warm 分发已实际验收:

```powershell
& .\Billfish.exe 'C:\Downloads\material-helper.eagleplugin'
& .\Billfish.exe --install-plugin 'C:\Downloads\material-helper.eagleplugin'
```

显式 `--install-plugin` 还接受 `.zip`. GUI 已运行时向现有 host 交付, 未运行时按正常 launcher 启动链校验并等待 host, 不把包参数传给 vendor core. 等待 host 上限 60 秒, 单次安装结果上限 120 秒; 未知结果需用返回的 job_id 查询, 不盲目重装. 安装成功后打开插件管理页, 新插件仍关闭.

目前不提供商店自动下载, 更新检查或后台拉取第三方包. 普通浏览器下载与本地安装分开, 不把安装 UI 当作远程服务凭据入口.

## CLI / API

将以下内容存为 UTF-8 `install-plugin.json`, 路径替换为实际下载文件:

```json
{"source_path":"C:\\Downloads\\material-helper.eagleplugin"}
```

```powershell
.\billfish-cli.cmd call plugins.install --input .\install-plugin.json
```

开发目录使用相同字段:

```json
{"source_path":"E:\\Billfish\\automation\\examples\\plugins\\example.material-notes"}
```

旧字段 `source_directory` 已删除, 会作为无效参数拒绝. 这次是 breaking change, 不提供转发兼容层. HTTP 仍使用 `/v1/execute` 和相同 arguments, CLI 不新增单独安装命令.

安装由 QProcess helper 异步处理, 不在 GUI 线程遍历或复制大包. 请求等待最终安装结果, 进度同时进入非模态进度区和 jobs. 成功 completion 为 `plugin_installed` 或 `plugin_updated`; 只表示资源和状态已提交, 不表示插件代码已运行或业务成功. 超时后保存 job_id, 查询 `jobs.get`, 不立刻重装.

## 包格式与容量

`.eagleplugin` 按 ZIP 容器读取, 不要求改变原 manifest 或资源树. 根目录必须直接有 UTF-8 JSON `manifest.json`, 不递归猜测多层嵌套项目. 开发目录同样要求 manifest 位于根层.

| 边界 | 限制 |
| --- | --- |
| 总展开资源大小 | 512 MiB |
| 单个展开文件 | 384 MiB |
| 文件和目录 entries | 4096 |
| manifest.json | 256 KiB |

安装器拒绝路径穿越, Windows 非法路径, 重复/大小写冲突, 文件目录冲突, 加密包, 超限条目和实际读取大小变化. 解包不运行插件代码, 不调用包内安装脚本. 含 main 的窗口包, service 与 dependency 各按 manifest 校验, preview/thumbnail/inspector 扩展仍未支持.

## 原子替换与激活

资源先进入独立 staging, 完成检查后整体替换 `%APPDATA%\Billfish\automation\plugins\<id>`. 同 ID 更新先停用原 session; 使用某资源依赖的插件仍在运行时, 不替换其正在使用的依赖. 新版不再包含的旧文件不会被合并残留.

提交失败恢复旧目录; 状态保存失败也回滚替换. 极少数回退本身受文件系统错误阻止时, 错误会指明保留的旧目录位置, 不能将其报告成更新成功.

新安装默认 disabled. 更新保留原 enabled 意愿和独立 profile, 但状态为 `updated`, 显式等待用户激活, 重启也不会自动执行新版. 可在设置页明确启用, 或调用 `plugins.enable` 的 `enabled:true`, 再等待 ready. 更新完成只表示资源提交, 不表示已执行新版.

## 资源依赖

无 main 且 `preview:{}` 的依赖资源识别为 `mode:"dependency"`, 不创建窗口或独立 runner. 新装资源即使 disabled 也能供 resolver 使用, 不需要运行它. 更新后的资源依赖仍需显式激活, 活消费者阻止更新. 当前 `eagle.extraModule` 只实现 FFmpeg, 通过消费者声明和真实安装路径解析; AI SDK / AI Search 的接口尚未实现.

安装 FFmpeg 包只证明资源可用, 不证明某转换任务已完成. 同理, AI 窗口能打开不证明外部 exe, 模型与实际输出可用. manifest/SDK 的完整范围见 [PLUGINS.md](../automation/PLUGINS.md), 当前实机结果见 [online-automation.md](online-automation.md).

## 默认关联检查与恢复

只读检查当前系统的实际解析结果:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\_launcher-patch\plugin-package-association.ps1 -Action Status
```

检查报告的 `Association.EffectiveExecutable`, `EffectiveCommand` 和 `EffectiveUnifiedLauncher`, 不能只根据写入某 registry value 推断已成为默认. 注册维护脚本支持独立备份与恢复; 本轮原关联已保存到 `association-backup.json`, 注册结果保存为 `association-registered.json`. 如已有受保护的 UserChoice, 脚本不会改写其 hash, 需要使用标准 Windows 默认应用界面. 当前环境注册前没有该 UserChoice, 本轮 effective 查询已确认生效.

## 前轮包安装与原包核心流程实机记录

包安装与核心流程验收阶段的 preview DLL SHA-256 为 `3741CCE9FEF30712674034F8CBCCD096715B0A10A8D29CE900363DB244C6E8E2`, 当时 unified status 为 Healthy. 后续 caption 阶段为 `68A02EDFC816493C7903BA4F99869FA28BD9BF6A6D20CC7FC8E36A2F7716223E`; 导入/视频阶段的当前部署身份与进行中的验收见 [online-automation.md](online-automation.md). caption 当时的 GUI 结果见 [compact-shell.md](compact-shell.md).

- 冷启动: 从 FFmpeg 原文件重打的约 268 MB ZIP 容器打开, 启动唯一 core, 安装 dependency 并自动定位插件管理页. 证据为 `cold-package-install.png`.
- GUI 已运行: 打开真实官方 Pinterest 1.1.9 包, 同一 core 接收并完成安装, 插件保持 disabled, 界面定位新插件. 证据为 `warm-package-install.png`.
- 同一 GUI 下经 Windows 默认关联打开 video2gif 1.0.2 原文件重打包, 完成 warm 安装, 未修改原 Eagle 安装目录.
- `association-cold-warm-result.json` 保留上述 jobs 与关联查询结果. 官方包通过 HTTP 获取并校验哈希; Edge 直链导航受工具拦截, 未声称其完成下载.

核心流程已取得实际结果:

- Pinterest 1.1.9 原包在隔离库读取当前唯一选中的合成 PNG, 窗口显示约 18 张来自 `i.pinimg.com` 的相似结果, 证据为 `pinterest-search-success.png`.
- video2gif 1.0.2 原包使用已安装 FFmpeg 依赖, 对测试 MP4 执行原生页面的一键另存, 生成普通文件 `GIF-QA.gif`. ffprobe 确认为 217,008 bytes, 320 x 180, 10 fps, 32 frames, 3.2 seconds. 证据为 `gif-output-probe.json` 与 `gif-conversion-success.png`.

上述为包安装阶段的历史结果, 分别证明相似图搜索和 GIF 另存流程. 后续 `items.import_path` / `items.import_url` 和 SDK addFromPath/addFromURL 已部署, Pinterest 原包 UI 保存已成功, 包括完整标题文件名和无标题默认文件名的真实素材核对; 原插件 99 个文件未修改. 此前 HLS 拖拽进入分辨率后取消的当次结果仍保留. 当前真实 Edge/Bilibili 4.2.0 按钮在用户选择/确认后已下载并原生入库, 文件/probe/预览/实际播放通过, 是实际浏览器 E2E. 早期 host-not-found 复现并在完整重启 Edge 后恢复, 日志未捕获失败分支, 不能据此声明永久解决或确定根因. 首次初始化后建议完整退出再打开 Edge, 不改采集管理策略. GIF 一键替换对应 replaceFile API 仍未实现, 不能称全功能兼容. 当前导入参数与验收阶段见 [automation/README.md](../automation/README.md) 和 [online-automation.md](online-automation.md).

该阶段结束时日常库, 原目录, 选区和窗口已恢复, 记录仍为 20,779 项; unified status 六项均健康, effective association 仍为根 launcher, core 进程唯一. `final-personal-restored.png`, `final-unified-status.json`, `final-association-status.json` 保存当时状态. `final-synthetic-file-integrity.json` 对 40 个测试原文件核对结果为 Changed 0. `installed-package-integrity.json` 确认 FFmpeg 13 个, Pinterest 99 个及 video2gif 原有 93 个文件未改动, video2gif 仅额外生成自己的 `temp/41.gif`. 这些是前轮范围与环境恢复的证据, 不代替新增 Pinterest 导入/视频采集验收.
