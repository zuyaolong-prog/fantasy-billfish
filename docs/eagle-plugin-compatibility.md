# Billfish 兼容 Eagle 插件的范围与实证

静态评估日期: 2026-10-18 (Asia/Shanghai), 逐包清单截止于当日 04:27:42. 随后用户授权实施 JavaScript/Eagle 接入, 下列当前实现摘要取代早期 "只有 Python 插件, 尚无 runtime" 的判断. 历史取证与合成 probe 保留原阶段边界, 不将后来实现反填为当时已存在.

当前结论: **Billfish 已有独立 Electron `38.8.6` 插件 runtime 和有限 `eagle.*` 适配层, 可读取受支持的 Eagle window/service manifest; 完整原包业务兼容仍需逐插件验收.** Python 保留于 CLI/API, catalog 和验证 helper, Python 插件 runner 已移除, 已安装旧目录保留为 incompatible. 真实选区与已列明的插件 GUI 流程已有实测结果, AI 图片放大的替换原图 GUI 流程已通过, 不称为全生态或全部原包已兼容.

## 当前实现与验收边界

当前 source 公开 API 包含 `items.selected` / CLI `selected`, imports, video.capture 及 `items.replace_file`. selected 严格接受 `{}`, 从真实当前 Qt 视图选区读取素材, 返回 `{items,total,item_ids,source,library_root}`. 不允许手工 IDs 或搜索结果代替选择. 真实空选区返回空数组, 上下文不可验证返回错误. 公有完整响应上限 16 MiB, 插件 SDK 完整 host_result envelope 上限 4 MiB; 超限 `selection_too_large`, 不截断, 可 catch 后继续会话.

Electron 读取本地 `main.url` HTML, 普通 window 首次 run 才建页并 Create, service 和自有 commands 在 ready 前 Create. `billfish.commands` / `permissions` 为自有命令与宿主 allowlist 扩展. 标准 window/service 的 `opened` 只表示窗口/服务入口已触发, 不把页面后续业务称为完成. session 与原生库 owner 固定绑定, 打开/切换/关闭库后废弃整个旧会话, 旧 Item 不漂移到新库. 初始无库纯工具可使用, 库 SDK 调用返回 `library_not_open`.

有限 Item SDK 已实现 `getById`, `get({ids})`, `getSelected`, 及 `save()` 的 annotation/url/tags 映射. 所有不支持的 dirty 字段先拒绝, 不会只保存部分字段后返回整体成功. tags 名称必须唯一可解析, 新标签按根标签创建; 多步写入可能 `partial_update`, 已完成部分不回滚. 保存继续遵循 Billfish 原生动作与 `persisted_metadata_verified` 的主库 metadata 核对, 不等于所有索引全量验证. addFromURL/addFromPath 通过原生 Copy 导入等待 native_import_persisted 与真实 ID, 接受 name/website/零或一个 folders, 未支持的导入选项先拒绝. `Item.replaceFile` 已接入 `items.replace_file`, 只允许同扩展的 owned PNG/JPG/JPEG/WebP, 默认 API dry-run, SDK 显式执行替换; 原文件 backup 与恢复记录持续保留在 `.bf/automation-replacements`. 原生 file monitor 完成分析并独立读回核验后才 completed, partial error 不代表回滚, 不自动重放. rename, 评分, GIF/视频替换及其它格式仍未开放.

当前 session 通过随机名称, 仅当前用户可访问的私有 named pipe 与 Qt 宿主交换 JSONL, 无 stdio fallback. Electron 使用 Node/DOM 与本地 preload, 是可信本地代码宿主而非 OS sandbox. SDK 主题来自操作系统 nativeTheme, 尚未接入 Billfish 自定义配色. thumbnail/viewer 已接入 3D 扩展, 实际 runtime 与原生入口覆盖见下节; inspector 尚未实现. dependencies 已支持安装资源包与平台候选解析, 当前 extraModule 只实现 FFmpeg, 不伪装成完整 AI SDK.

已取得前轮 JS fixture 真实 getSelected, 三字段 save, 属性栏同步和重开的证据, 私有 named pipe 的 ready/run/shutdown 实际运行通过. 默认关联已注册并由 AssocQueryString 确认到唯一 launcher, FFmpeg 资源冷启动安装及官方 Pinterest 1.1.9 包 warm 安装均通过. Pinterest 原包已显示约 18 张实际相似图, video2gif 1.0.2 原包另存生成有效 GIF. 本轮导入已部署, 未改动 Pinterest 原包的真实 UI 保存已通过, 包含完整标题文件名/真实素材及默认文件名结果; 不把它扩大为所有 Eagle 写 API 均支持. 当前真实 Edge/Bilibili 4.2.0 按钮经用户选择/确认分辨率后完成唯一新任务, 下载, 原生入库与实际播放已核验, 不只是 direct host 或历史部分链路. 早期 host-not-found 复现真实存在, 完整重启后恢复但失败分支未被日志捕获, 不声称根因已确定或永久修复. GIF 一键替换超出当前 PNG/JPG/JPEG/WebP 替换支持范围, 未通过该包 GUI 验收. 原以图找图包已发生 Bing 自动跳转, 但该搜索结果页视觉仍未核验. 上轮 Edge 插件包下载直链被工具拦截, 与本轮视频链路分开记录. 安装输入为 `source_path`, 支持本地 `.eagleplugin` / `.zip` / 开发目录, 新装默认关闭, 更新等显式激活. 最新契约与结果见 [PLUGINS.md](../automation/PLUGINS.md), [plugin-installation.md](plugin-installation.md) 和 [online-automation.md](online-automation.md). 顶部界面任务另见 [compact-shell.md](compact-shell.md).

## AI 图片放大 1.0.9 当前进展

维护版沿用原插件 UI 与两个 Real-ESRGAN 模型, 已在 Billfish 安装并启用. RTX 5090 上两个模型分别处理 RGB/RGBA 的四次实际运行均 exit 0, 尺寸从 80 x 64 放大到 320 x 256, RGBA 保留透明度. 实际插件 GUI 已完成 4x 对比与另存 PNG, 192 x 192 输入生成 768 x 768 输出; 替换原图的真实 GUI 验收也已通过, 同 ID 的 192 x 192 PNG 更新为 768 x 768, 标签/备注/来源保持, 新原生缩略图和大图已实际打开. 替换结论另由真实 GUI 操作, 宿主读回和原生预览结果支持. 证据与维护状态见 [AI 图片放大记录](../task-memory/task-billfish-ai-image-enlarger/task_state.md).

完整 portable 包通过 `tools/plugin-packages/ai-image-enlarger-1.0.9.eagleplugin` 携带维护包, 在新电脑手动安装并开启, 不复制个人 AppData 或自动启用插件. 该实现使用独立 Vulkan engine, 不代表通用 `eagle.extraModule.ai`, AI Search 或 AI 自动抠图已兼容. 3D 扩展单独记录如下.

## 3D 格式扩展 1.9.0

用户已授权迁移 `3D格式扩展` `1.9.0`, ID `89fb801f-7761-4de6-b7ce-3785a084edaf`. 原 Eagle 安装的 49 个文件只读核对未变, 维护副本保留原渲染器与本地资源, 只改 `manifest.json` 中的 `billfish.previewEngine` 和 `viewer.html` 的 IFC WASM 相对目录. 本地包为 [3d-format-extension-1.9.0.eagleplugin](../tools/plugin-packages/3d-format-extension-1.9.0.eagleplugin), 10,903,622 bytes, SHA-256 `fb3c10d7da4e8f971847adbd5f247a9aa238a05fea6aeda154e6d95ac5c60811`.

production Electron runtime 已实际执行以下 viewer 与 thumbnail 路径: GLB, GLTF, OBJ, STL, FBX, PLY, VTK, XYZ, PCD, DAE, AMF, BVH. 静态 FBX 和动画 FBX/BVH 均有运行结果. 这 12 种格式不能代替包声明的全部 23 种模型格式, 或 HDR/EXR viewer 的验收. 尚未覆盖 Spline/Splinecode, USDZ/USDC, 3DM, 3MF, LWO, 3DS, VOX, IFC, KMZ, HDR, EXR. 输出图片大小与插件返回的 logical dimensions 可以不同, 宿主应以实际 PNG 解码结果校验缩略图, 不把插件返回尺寸写成模型的原生 metadata.

所测基础本地文件路径未产生 HTTP(S) 请求. 包含本地 Draco, KTX2, Rhino3DM, IFC, USDZ 等资源不等于相关格式均已测通; Spline 的可选网络路径仍需按模型验证. 原设置页的市场列表需要联网, 可选付费 animation-control 等模块未迁移, 未绕过许可证或激活, 不承诺整个插件离线. 未复制 Eagle 私密配置或账号.

普通安装/开启已完成, 原生类型注册在 QA 库新增 22 项并核验全部 25 个声明. OBJ 原生 ID 50 已发布真实 750 x 750 自定义封面, 原生网格已核验; source `tid=320`, cover `cover_tid=10` (PNG), 模型 width/height 仍为 0. GLB 已原生导入 ID 51, 动画 FBX ID 52 的 `analysis_ready:false` 如实保留. FBX 显式 thumbnail 返回 `native_custom_cover_verified`, PNG 750 x 696, source/metadata 保持, 实际原生双击完成 viewer, `animation_count:1`. 禁用插件返回 `preview_not_supported`, 已有 OBJ 封面保留. 新未选中的 GLB 53 已自动生成 PNG 750 x 750, source/protected metadata 保持. OBJ 50 与 FBX 52 封面跨重启 SHA 保持. GLB 51 显式生成封面成功, 其此前未自动生成的原因仍未知.

封面经 `IFileAction::slotSetFileCover` 提交为真实原生 custom cover, 不把 sidecar PNG 或设置页打开称为原生缩略图成功, 不宣称新增 Billfish 3D analyzer. 最新部署和 GUI 结果见 [本轮 task_state](../task-memory/task-billfish-independent-control-3d-trim/task_state.md), 原始证据包含 `qa3d-types-progress-result.json`, `qa3d-thumbnail-fbx-precise-result.json` 和 `3d-plugin/production-preview-*-result.json`. 下方早期静态评估不再代表当前实现能力.

后续性能修复继续保留这条广格式路线. 实测数秒等待来自普通隐藏 viewer 的帧与 PNG 回调, 当前改为 DOM 和 Billfish owner 确认后显示 loading UI, 成功仍要求真实模型 load 与有效帧. 隔离普通窗口 request 的 OBJ 三次中位 443 ms, 动画 FBX 1213 ms, 不能当作 installed 入口点击到可交互的端到端结果. FBX Review 的独立应用和 FBX SDK 均不能直接替代目前的广格式/完成协议, 详见 [独立路线评估](../task-memory/task-billfish-independent-control-3d-trim/principal-3d-route-assessment.md). viewer 使用真实 Billfish owned-window 关系, 不采用全局 alwaysOnTop; phase5 实机读回确认 owner 为真实 Billfish 主窗口, topmost:false. 实际 OBJ 请求确认完成 510.25 ms, GLB 应用内双击到 job 完成 1009.99 ms, 后者含 20 ms job 轮询; 不作为精确首帧或通用性能保证.

## 历史静态评估的阅读方式

以下章节保存 2026-10-18 04:27:42 及之前的静态评估与离线 probe. 其中 Python runner, 17 个 operation, 尚无 production JavaScript adapter 等叙述只描述当时基线. 该阶段仅增加报告和合成 probe, 没有改生产代码, 执行完整第三方插件入口或启动其服务; 这不是后来已授权实施阶段的操作范围说明. 历史 probe 的 `live_selection_implemented:false` 不代表当前 source 没有选区实现, 也不能代替本轮真实 GUI 结果.

## 证据与覆盖边界

- 复用 [Eagle 架构取证](eagle-architecture-evidence.md) 中已保存的 ASAR 哈希, 所选 SDK/IPC/Item 文件证据和模板 REA 记录. 模板父 Evidence 为 `ev_44db71d3baa34307a3256605ee3061b13ff76fbd78cde9aa4e8f308b1398a90d`; 未再次执行全包分析. 原分析的 semantic coverage=unknown, 不把静态引用视为实际执行.
- 本机安装声明为 Eagle 4.0.0, buildVersion `20260304`, buildNumber `20`. `resources/package.json` 中 Electron 的 `^22.3.7` 是开发依赖声明, 不作为实际进程 runtime 版本证明.
- 重新只读核对了 `E:\Software\Eagle\resources\plugin_templates` 的 4 个 manifest 与入口引用. 精确字段, 文件 SHA-256, 静态 `eagle.*` / `require()` / DOM 引用列表在 [evidence-inventory.json](../task-memory/task-billfish-eagle-plugin-compat/evidence-inventory.json). 引用扫描是字面索引, 不是调用图或完整依赖解析.
- 当前 Eagle 进程的 renderer/GPU/crashpad 命令行一致指定实际 userData 为 `C:\Users\Administrator\AppData\Roaming\Eagle`, 不只根据默认路径猜测. 已在实际 `Plugins` 目录读取 9 个插件包的 manifest 与入口代码引用. 还定向检查了开发目录的精准 localStorage key `PLUGIN_LOCAL_PLUGINS`, 未输出其它 key/value, 也未读取模型凭据或插件私密配置. 具体时间点边界见下节.
- 评估时对照 Billfish 的真实 `service.py` / `catalog.py` / Python runner / SDK, 当时共有 17 个 operation. 其中 `jobs.*`, `plugins.*`, `ui.settings.open` 是管理能力, 不能计作等价的 Eagle 素材 API. 该数量和旧 runner 属于历史基线, 当前实现见本文开头及链接的现行文档.
- 同日查询 Eagle 和 Electron 官方文档. 线上文档包含比本机 build20 更新的字段, 如 manifest 的 `followCursor` 标注需要 build22, 因此未来兼容目标需要固定 SDK 范围, 不能只写含糊的 "Eagle 4". [官方 manifest 文档](https://developer.eagle.cool/plugin-api/tutorial/manifest)

## 本机已安装插件清单核对

进程路径证据保存为 [eagle-process-paths.json](../task-memory/task-billfish-eagle-plugin-compat/eagle-process-paths.json), 只保留 PID, 可执行路径, userData 与 Electron crashpad 版本声明, 没有持久化整条命令行. 当前进程的 crashpad annotation 还观察到 Electron `22.3.7`, 这是比 package.json 开发依赖声明更直接的 runtime 版本线索.

2026-10-18 04:22 前后的早期目录检查未见 `Plugins`, 但 04:23:05 (Asia/Shanghai) 的下一次快照已观察到 9 个包. 本轮未执行安装操作, 该变化属于调查期间的外部状态变化, 早期阴性结果不再作为当前清单结论. 目录存在和 manifest 可读只证明包已落地, 不证明安装后首次初始化、模型下载或每个功能成功. [installed-inventory.json](../task-memory/task-billfish-eagle-plugin-compat/installed-inventory.json)

`Local Storage/leveldb` 当时只有一个 `000003.log` WAL 和 CURRENT/MANIFEST/锁/日志元数据, 没有 `.sst`/`.ldb` 表文件. 定向检查唯一 WAL 时, `PLUGIN_LOCAL_PLUGINS` 的 UTF-8 与 UTF-16LE 编码命中均为 0; 读取前后 size/mtime 未变化. 原始 localStorage 内容没有复制到取证目录. 此范围内未观察到持久化的额外开发插件路径. 这不是通用 LevelDB 解码器, 不声称已经检查其它用户 profile 或一切可能的外部开发路径.

以下 9 包为本轮实际目标. 安装包自带的四个 `plugin_templates` 是开发模板, 不计入. 名称只读取公开 `_locales/zh_CN.json` 的 `manifest.app.name`, 版本与类型来自各包自己的 manifest. 详细逐文件哈希和引用列表在 [installed-entry-references.json](../task-memory/task-billfish-eagle-plugin-compat/installed-entry-references.json).

| 已落地插件 / 固定版本 | 类型, 关键静态宿主依赖 | 静态评估时 Billfish 的核心缺口 | 当时建议的复用路线与相对工作量 |
| --- | --- | --- | --- |
| 以图找图 / Reverse Image Search `1.0.7`, `eagle-plugin-search-by-image` | Window, `getSelected`, 主题/窗口/光标位置, Node `electron/path/url`, 自带 preload, webview.loadURL 与消息事件, 最后 openExternal | 真实 GUI selection, Electron webview/preload/IPC 及外部搜索网站流程 | 库写入耦合较低, 适合作为首个试点候选, 但绝非改一个搜索 URL 就可兼容. 须有相应 Electron 宿主, 保留页面上传与结果通信. 相对中等, 远端网站流程未运行验证 |
| Pinterest 以图找图 `1.1.9`, `22069a4c-58e0-44d7-89d9-69f014158acd` | Window, `getSelected/getById`, clipboard.readImage, `addFromURL`, DOM, Node fs/path | 选择, 剪贴板图像, 网络搜索 UI, 将结果导入 Billfish 的写入能力 | 可先评估只搜索/打开结果的核心路径. 要完整保留导入功能还需新增导入边界. 相对中等 |
| AI 自动抠图 `1.0.8`, `ai-background-remover` | Window, `getSelected/getById`, `Item.replaceFile`, Node child_process/fs/https/net, 本地计算程序与下载代码 | 真实选择与替换原文件并刷新索引/预览; 计算程序/模型可用性未运行核验 | 可复用处理算法/外部程序调用与 UI. 替换动作必须先实现并验收. 相对中高 |
| AI 图片放大 `1.0.9`, `ai-image-enlarger` | Window, `getSelected/getById`, `Item.replaceFile`, Node child_process/crypto/fs/https, 放大模块 | 与抠图相同的文件写入边界及外部计算依赖 | 算法模块与工作流可复用, 同一选择/replaceFile 桥可服务两包. 相对中高 |
| 一键换大图 `1.0.5`, `eagle-plugin-maximum-image` | Window, Electron preload, `getSelected/getById`, `replaceFile`, `url` 后 `save`, contextMenu, setReferer | 嵌入网页/网络环境, 原图替换, 外部 URL 来源保存 | 可保留搜索/下载算法和 UI; 完整入口比以图找图多文件写和网页宿主耦合. 相对中高 |
| 视频转 GIF 工具 `1.0.2`, `video2gif` | Window, `dependencies:["ffmpeg"]`, 动态 `extraModule[name].getPaths`, child_process.spawn, selection, library.path, replaceFile | FFmpeg dependency resolver, 选择和输出文件/替换动作, 本地库路径语义 | 转换核心可复用. 可先保留导出到普通目录的路径, 要兼容完整替换模式仍需新增 native API. 相对中等到中高 |
| 3D 格式扩展 `1.9.0`, `89fb801f-7761-4de6-b7ce-3785a084edaf` | Preview thumbnail/viewer 加初始化 Window, Electron, child_process, fs/zlib, `lib/core.cjs`, 外部 API 域名 | Billfish 缩略图生成/预览扩展宿主, core runtime 与可能外部服务依赖未验证 | 先评估格式解析/渲染算法可重用部分. 9.7 MB core.cjs 未深入恢复, 不承诺其内部环境可移植. 相对高, 不宜首个目标 |
| MCP Server `0.3.1`, `mcp-server` | serviceMode, localhost/127.0.0.1:41596, Node http/net/fs/child_process, 动态工具枚举, item/folder/tag/tagGroup/smartFolder/AI Search 多 API | 大量素材写动作、逻辑分类、评论、智能目录、额外 AI Search 服务等尚不存在 | 完整原包兼容相对最高. 若仅需让 AI 使用现有 Billfish 能力, 直接对其 operation schema 做小型 MCP transport 通常比补齐此包全部假设少; 本轮不实施 |
| FFmpeg `6.1.0`, `ffmpeg-win-x64` | win/x64 依赖包, 无 main, `preview:{}`, 包含 ffmpeg.exe 与 ffprobe.exe | dependency 声明/路径分发; 二进制运行未验收 | 作为已装编解码依赖看待, 不把它算作已兼容 UI 插件. 处理路径桥相对低, 不等于任意 FFmpeg 业务完成 |

相对工作量是基于已读调用面的工程判断, 没有用百分比表达兼容率, 也没有把静态入口调用当作运行成功. 8 个功能/服务包和 1 个 dependency 包的完整入口均未运行. 没有读它们保存的设置、模型凭据、window-state.json 或完整浏览器 storage.

新增 REA 针对上述入口及直接本地 JS 子集, 父 Evidence `ev_981edd4f68dfdceb27d878e31cd554aeaf88b9c13ff0f9dec111108efed0d23e`, summary `ev_bff7599925f3bc5a911c8d23bf947beffd9cfc59d186aee5f55625ca63c18346`, 子集 SHA-256 `b54edf4e324b7d900c50e7726fa352ee6f43f4c415bc3f430c86001fdf6bf773`. 静态解析 256 relevant files/238 JS/8,323,582 text bytes, 0 parse failures, 但 application=partial, semantic=partial/truncated, 包括资源约束与动态绑定未解析, 不能据此宣称完整解析了所有插件. [installed-rea-summary.json](../task-memory/task-billfish-eagle-plugin-compat/installed-rea-summary.json)

后续针对可见动态目录的补读只增加字面引用和哈希清单, 没有重复全量 REA. `.node` 字样可能来自 DOM 的 `.nodeType` 等字段, 不能当作发现 native addon 的证据. 早期字面扫描中的 `eagle.pome.run` 经上下文核验实为 URL `https://api.eagle.pome.run`, **不是 SDK 调用**, 已修正 SDK 引用扫描避免将域名当作 API. `lib/core.cjs` 存在但大于本次 8 MB 单文件解析上限, 其依赖仍未知; 原包调用面的整体难度不能被一个语法解析成功结果掩盖.

最终只读目录差量快照为 2026-10-18 04:27:42 (Asia/Shanghai): 仍为上述 9 包, 相对本轮逐包扫描没有新增/移除 ID, 9 个 manifest SHA-256 均未改变, 没有缺失 manifest 的目录. 这是本报告的清单截止点, 不继续监控用户安装. [final-manifest-snapshot.json](../task-memory/task-billfish-eagle-plugin-compat/final-manifest-snapshot.json)

## 必须分开的五种兼容

| 层面 | 含义 | 2026-10-18 早期静态评估时的状态 |
| --- | --- | --- |
| 调用兼容 | 同名 `eagle.item.get()` / `Item.save()` 有相同参数, 返回值与错误语义 | 当时可设计部分映射; 尚无 production JavaScript adapter |
| 包与 manifest 兼容 | 解析 `.eagleplugin`, 入口页面, 平台, 架构, dependencies, 资源相对路径 | 当时安装器仅接受 Billfish 的 Python manifest, 不接受 Eagle manifest |
| 生命周期兼容 | 创建, 重复运行, 显示, 隐藏, 退出, 切库, 主题改变有对应时序 | 当时 Python `on_create/run/on_shutdown` 不等价于窗口事件 |
| UI 与运行环境兼容 | HTML/CSS/DOM, Node, Electron 窗口, 对话框等按插件期待工作 | Python runner 不提供这些环境; Qt WebView 单独也不等于 Node/Electron |
| 数据与磁盘布局兼容 | item 字段语义, 文件路径, 缩略图, metadata 文件和分类关系一致 | Billfish 保留普通目录, 不能伪造 Eagle `.info/metadata.json` 并让插件直接写它 |

Eagle 官方划分 Window, Background Service, Format Extension, Inspector 四类. 一个包可以声明多种能力, 不能按文件后缀或名称判为简单插件. [官方插件类型](https://developer.eagle.cool/plugin-api/get-started/plugin-types)

## 本机四个模板实际需要什么

| manifest 名称与 ID | 真实声明与入口 | 宿主要求 | 可复用判断 |
| --- | --- | --- | --- |
| Window Plugin, `LARSK1QMR0DEP` | `main.url=index.html`, 640 x 480 | 入口 JS 使用 DOM 和 Create/Run/Show/Hide/BeforeExit | HTML/JS 可保留, 但要有浏览器窗口及正确事件分发; Python runner 不能承载 |
| Service Plugin, `LARSJLVDQEB0B` | `main.serviceMode=true`, `main.url=index.html` | Create/Run/Show/Hide, 隐藏后仍运行的窗口生命周期 | 服务模式不等于普通 Python 命令. 需独立宿主与关闭策略 |
| Preview Plugin, `LARSKLB8OTOC2` | `preview.icns.thumbnail.path=thumbnail/icns.js`, `viewer.path=viewer/icns.html` | 缩略图入口为 CommonJS, 接收 src/dest/item; require fs 与随包解析模块. viewer 使用 DOM, window.location 和相对 require | 解析算法有复用价值, 仍要接入 Billfish 缩略图调度, 输出校验和预览窗口; 不能只注入 eagle 全局 |
| Inspector Plugin, `cc41e899-5fc3-445c-a113-2d9573d6edcc` | `preview["jpg,png"].inspector`, height=100, multiSelect=false | `eagle.app.theme`, `eagle.item.getSelected`, `onPluginCreate`, `onThemeChanged`, DOM | 需右侧栏 HTML 宿主, 真实选择上下文和主题事件; 不能当作独立命令装入现有设置页 |

上述模板只是类型和最低依赖的证据, 没有实际第三方功能, 不能用它们能加载的结果估计全商店成功比例. 官方也明确 Inspector 是嵌入右侧检查器的扩展. [官方 Inspector 说明](https://developer.eagle.cool/plugin-api/get-started/plugin-types/inspector)

## 素材 API 的映射与真实缺口

下表是基于早期静态评估时 Billfish 源码的设计判断, 不是现行实现清单; 后续已补齐的 JS Item/真实 selection 等见本文开头.

| Eagle 需求 | 对应 Billfish 能力 | 还需要什么 |
| --- | --- | --- |
| 按明确 ID 读取 Item | `items.get(item_id)` | 构造有方法的 JS Item; 字段映射, 缺失值和错误须逐项定义. ID 全程保留字符串 |
| 读取 `filePath`, 现有缩略图 | `path`, `thumbnail_path` | 返回实际原文件路径; 缩略图可能不存在. 不推导 `.info` 路径 |
| 保存 `annotation` / `url` | `items.update` 的 `note` / `origin` | 只提交实际变更字段; 等待真实结果, 不能把任务 accepted 当作 save 成功 |
| 读写 tags 的名称数组 | `tags.list/create/assign`, item tags 为带 ID 的记录 | 名称到 ID 的唯一性与层级策略; 数组替换要计算 add/remove. 多次调用非事务, 失败可能留下部分结果 |
| `getSelected`, `select`, `open` | 当时没有公开 GUI selection/open operation | 需要新增并验证真实 Qt GUI 边界. 不能拿上一次搜索结果, 全部素材或手工 ID 充当当前选择 |
| 通用 `get(options)` | `items.search` 只有自己的文本, 单目录, 单标签, 扩展名, 分页语义 | Eagle 的组合筛选不能直接传入 Billfish `query`; 不支持的筛选必须拒绝, 不能默默扩大命中集 |
| Item 的改名, 评分, 分类关系, 导入时间等写入 | 当前没有这些公开写 operation | 各自补原生动作及副作用验证; 不能把 folder 数组赋值简单变成磁盘移动 |
| 导入, replaceFile, 回收/删除, 自定义缩略图 | 早期评估时没有等价写 API; 当前导入与有限图片 replaceFile 已实现, 见本文开头 | 后续各文件/索引/界面业务边界仍须单独实现与隔离库验收 |
| library/folder/smartFolder/tagGroup | 只有 library.info, folders.list, tags.list 等有限读能力 | 字段, 层级, 逻辑分类与物理目录需明确; 同名对象不表示等价 |

Eagle 的 Item 契约同时涵盖对象修改保存, 广泛筛选和直接文件路径等接口, 因而仅实现 `save()` 名称远远不够. [官方 Item API](https://developer.eagle.cool/plugin-api/api/item)

`Item.save()` 最容易造成表面成功: 插件可能同时改 `name`, `tags` 和 `annotation`. 只保存备注后返回 true 会谎报其余字段已保存. 生产 adapter 应在任何写入前检查所有脏字段, 未支持的改动明确报错; 支持的多步骤写入发生部分失败时保留细节, 不返回整体成功. `tags.push()` 这样的原地数组变更也必须被差异检测覆盖. 未修改字段不可随旧 Item 快照整包写回, 否则会覆盖用户后续修改.

Billfish 在线动作的已验证成功边界是 `persisted_metadata_verified`, 批量非原子, 切库必须继续绑定原库并拒绝向新库跟随写入. 兼容层应保留这些语义. 一次成功的备注写入不代表 rename/tag/folder 等其它属性也成功, 任务超时不代表可安全重试.

## 为什么完整插件需要独立 runtime

Eagle 插件本来就可使用 Node 标准模块和第三方 npm 包. [官方 Node API](https://developer.eagle.cool/plugin-api/tutorial/node-js-native-api), [官方第三方模块说明](https://developer.eagle.cool/plugin-api/tutorial/3rd-modules)

从本机已有静态证据看, 插件 UI 由 Electron BrowserWindow/preload 提供, SDK 通过 IPC 进入宿主业务层. 因此, 对具有 HTML UI, Electron API 或 Node/DOM 混用的原包, 最有希望保留代码的路线是 **独立 Electron companion + 自行实现的 `eagle.*` preload adapter + Billfish 原生业务 API**. 不需要重写 Billfish 主界面为 Electron. 只使用 Node 和纯算法的模块可以用较小的 Node worker, 但这不覆盖 UI 插件.

即使采用 Electron, `require('electron')`, native `.node` 模块, 进程架构和依赖的 ABI 仍需逐包验收. Electron 官方明确 native Node 模块可能需要为目标 Electron 重新编译. 不能假定带 Node 就可原样使用所有预编译扩展. [Electron Native Node Modules](https://www.electronjs.org/docs/latest/tutorial/using-native-node-modules)

窗口事件也不能随意改名转发. Eagle 的 Create 是窗口创建, Run 是用户再次运行, Hide 后服务可以继续运行; 当时 Python 的启用时导入, 每次 command 调用, 停用 shutdown 具有不同含义. 后续 Electron 生命周期的实现范围见当前摘要. [官方生命周期事件](https://developer.eagle.cool/plugin-api/api/event)

另外有三类外部假设需要逐包识别:

- **固定 HTTP API**: 插件或外部浏览器扩展可能直接访问 `localhost:41595`. Billfish 当前 API 是显式启动, `127.0.0.1:27393`, bearer token, 命名 operation. 改一个 URL 不会得到同样路由和结果; 也不应全局占用 Eagle 的端口冒充它. [Eagle 官方 HTTP API](https://api.eagle.cool/)
- **依赖插件**: `dependencies:["ai-sdk"]` 与 `eagle.extraModule.ai` 涉及独立 AI SDK, 配置和设置 UI. 这不是给 `eagle` 填同名空对象便可工作. 本轮不迁移 Eagle 的私密模型配置. [官方 AI SDK](https://developer.eagle.cool/plugin-api/extra-module/ai-sdk)
- **额外后台与编解码器**: AI Search 要求插件安装并运行; FFmpeg 是额外 dependency 的调用面. 这些服务, 模型, 索引, 二进制及其协议都需核对, Billfish 已有 VideoTrim 也不自动等于 Eagle FFmpeg SDK 兼容. [官方 AI Search](https://developer.eagle.cool/plugin-api/extra-module/ai-search), [官方 FFmpeg](https://developer.eagle.cool/plugin-api/extra-module/ffmpeg)

凡是直接读写 `images/<id>.info/metadata.json`, 扫描 Eagle 库根缓存或依赖 Eagle 私有 IPC 的插件, 不能承诺原包直接运行. 不应为了兼容而把 Billfish 素材搬进 Eagle 文件结构. 可复用公开算法或 UI 的部分, 对宿主耦合点做显式适配.

## 历史离线 contract probe

[contract-probe.cjs](../task-memory/task-billfish-eagle-plugin-compat/contract-probe.cjs) 使用 Node v24.14.1 运行, 只创建内存中的一条合成 Billfish 记录. 没有读取素材文件, HTTP token 或数据库, 也没有连接 named pipe/API. 业务片段是本轮自写的 Eagle API 形式 JavaScript, 直接使用:

```javascript
const selected = await eagle.item.getSelected();
selected[0].annotation = 'synthetic annotation';
selected[0].url = 'https://example.invalid/new';
await selected[0].save();
const again = await eagle.item.get({ ids: [selected[0].id] });
```

执行时 `getSelected` 的 ID 由 fixture 显式注入, **不是 Billfish 真实 GUI selection**. adapter 把读取转为 `items.get`, 保存转为一次 `items.update({updates:[{item_id,note,origin}],dry_run:false})`, 实际接收方是内存 mock. `save()` 只证明此窄子集的调用形状和 Promise/error 传播可行, 不是原生持久化证明. 它没有 manifest loader, 窗口生命周期, HTML UI 或第三方 plugin entry.

通过的 8 项边界断言: 备注/来源映射, 64-bit ID 字符串精度, 原路径保留, 不支持的写入拒绝, 不支持的筛选拒绝, 缺少 selection 拒绝, 素材不存在错误传播, save 失败不伪报成功. 另由 [capture-evidence.py](../task-memory/task-billfish-eagle-plugin-compat/capture-evidence.py) 调用真实 `service.validate` 与 `OPERATIONS` schema 验证 probe 产生的 4 次调用参数均合法. 参数合法不等于 operation 已执行.

结果见 [contract-probe-result.json](../task-memory/task-billfish-eagle-plugin-compat/contract-probe-result.json). `full_plugin_compatibility=false`, `live_selection_implemented=false`, `production_calls=0`. 没有用 mock 结果宣称任何真实 Eagle 插件已兼容.

复现:

```powershell
node .\task-memory\task-billfish-eagle-plugin-compat\contract-probe.cjs
python .\task-memory\task-billfish-eagle-plugin-compat\capture-evidence.py
```

第二条读取已保存的 probe-result; 如修改 probe, 需先将第一条的 JSON 输出保存到相同结果文件再捕获清单. 无需 pip/npm 安装, 不启动应用.

## 当时建议的实际落地顺序

1. 当时建议以实际清单中的 `eagle-plugin-search-by-image@1.0.7` 为首个候选, 先确认用户最常用的搜索引擎和核心流程, 并把 Electron webview/preload/IPC 作为必需工作量. Pinterest 可以作为第二个目标; 只读搜索与结果导入应分别验收, 后者需新增 `addFromURL` 对应原生能力. 该阶段仅给出评估, 尚未实施 runtime.
2. 按该目标做独立 JavaScript runtime 与有限 `eagle.*` 契约, 尽可能保留插件算法与 HTML UI. 在隔离库验证完整入口, 真实 selection, 写后 GUI 更新, 切库和停用, 然后才能称该版本插件兼容. 不支持的 API 在启动检查或调用时明确失败.
3. 仅当目标需要时增加 rename/import/replaceFile 等原生能力. 为全商店一次性铺全 SDK 会变成另一套完整平台, 当前没有证据支持其投入收益.

以上顺序说明当时如何避免逐个重写算法, 以及避免将 UI 运行环境, 私有存储和业务动作的缺口藏在同名 API 后面. 该静态评估阶段完成了本机 9 包的有界调查, 尚未安装 production 兼容层或运行目标包完整入口. 后续用户授权的实施现状与验收以本文开头及 [online-automation.md](online-automation.md) 为准.
