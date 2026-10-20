# Billfish 在线自动化与界面同步

本文记录在线写入, 界面进度和本地插件的实际边界. 操作示例见 [automation/README.md](../automation/README.md), AI 入口见 [AI_GUIDE.md](../automation/AI_GUIDE.md), 插件开发见 [PLUGINS.md](../automation/PLUGINS.md).

## 独立 UI 控制与本轮修复

本轮候选增加 `automation/billfish-ui.cjs`, 通过 Billfish 自身 host 读取 Qt 结构化 snapshot, 控件/model/action refs, 并定向派发局部 click, type, key, scroll, drag 和 invoke, capture 只截取目标控件. 该方式不使用全局桌面截图或 `SendInput`. 使用与边界由 [billfish-ui-control.md](billfish-ui-control.md) 维护. 独立 Electron 插件窗口和系统文件对话框不自动属于该 Qt 控制范围. 当前构建, 部署和实际输入验收状态见 [本轮 task_state](../task-memory/task-billfish-independent-control-3d-trim/task_state.md), 不以输入 accepted 或 dispatched 代替业务完成.

实际验证了 Billfish 主窗口最小化, ChatGPT 保持前台时的 Grid/waterfall combo 切换, 插件菜单打开管理页, 主菜单展开最近库子菜单. 系统指针位置保持, 无系统 mouse capture. 最近库切换本身成功, `BFLoadingDialog` 曾取得前台的精确修复已部署, 不据此承诺所有切库路径或弹窗均无焦点变化. `ui.click` 的 right button 只派发 pointer press/release, 当前 `coverage.context_menu_event:false`, 不保证右键菜单. 打开另一历史资源库时曾出现原生 Windows folder dialog 并阻塞 pipe, 已取消该目标窗口; 这仍属于 OS 对话框边界.

视频剪辑报错 `Billfish file id 78168 was not found.` 已修复其库上下文过期根因: 原实现优先使用进程启动时的 `BILLFISH_LIBRARY_ROOT`, 同进程切库后仍查询启动库. 当前每次取原生 manager 的真实库 root, 上下文未就绪时拒绝解析并清除旧路径; 已打开的独立剪辑编辑器保留显式验证过的源快照. 验收期间 78168 已变为 recycled, 未恢复该状态且不推测归因. 同库有效素材 30157 与其初始 source SHA-256 一致, 本次从 QA 库启动再切回个人库后, 已通过应用内输入打开剪辑并拖选 43487..45582 ms 导出 MKV. 输出实际为 H.264 1920 x 1080 / 30 fps, AAC 48 kHz stereo, 2.183 s, 1,964,484 bytes, FFmpeg 全音视频 decode exit 0, 原视频 hash 保持. 这是当前库解析与实际导出的验收结果, 不表示已回收 78168 仍可打开.

3D 格式扩展已通过普通 `plugins.install` 安装并启用. 本轮原生类型注册已通过, 本库新增 22 项, 核验共 25 个声明后缀. OBJ 原生 ID 50 的 `cover_tid:10` (PNG) 指向实际 750 x 750 封面, 网格已截图核验, source hash 保持且模型 width/height 仍为 0. GLB 原生导入得到 ID 51, 动画 FBX ID 52 的 `analysis_ready:false` 如实保留; FBX 显式 thumbnail 返回 `native_custom_cover_verified`, PNG 750 x 696, source/metadata 保持, 实际 UI 双击完成 `plugin_viewer_rendered`, `animation_count:1`. 禁用插件后公开 preview 返回 `preview_not_supported`, 已有 OBJ 封面保留. 新未选中 GLB 53 已自动生成 750 x 750 封面, source/protected metadata 保持, OBJ/FBX 封面跨重启 SHA 保持. GLB 51 显式封面成功, 此前未自动生成原因仍未知. 另有 production Electron viewer/thumbnail 的 12 种格式实测, 不等于全部声明后缀均已验收. 最新范围见 [eagle-plugin-compatibility.md](eagle-plugin-compatibility.md) 和本轮 task_state. 基础本地渲染测试没有网络请求, 设置页市场和可选付费模块不在离线保证内.

phase5 修复保留现有广格式 renderer: 普通隐藏 viewer 等待帧/PNG 回调曾额外耗时约 3-4 s, 当前 DOM 与真实 Billfish owner 确认后显示 loading UI, 仍等待模型 load 和有效帧才完成. 隔离普通窗口 request 的 OBJ 中位 443 ms, 动画 FBX 中位 1213 ms, 与安装后的完整链路测量分开. 实际 OBJ 公开请求确认完成为 510.25 ms, 窗口首次观察 455.68 ms; GLB 应用内双击到 job 完成为 1009.99 ms, 窗口 485.84 ms, 包含 20 ms job 轮询. viewer 真实 owner 是 Billfish 主窗口, topmost:false; GLB 测试前后 foreground/cursor 相同. 不将这些单次测量作为通用性能保证.

沉浸验收针对真正无侧栏的 immersion 与普通预览. 进入仍使用 `ImmersionBtn`, 当前补齐真 immersion 双击退出前的 source position 与 playing/paused capture, 在目标 Ready/autoplay 后恢复. 最终暂停测试由 UI seek 43.748 s, 对进入按钮双击后 43.668 s, 真 immersion 媒体双击退出后及 1 s 后均保持 43.668 s; 播放测试为 44.351 -> 45.167 s entry -> 46.103 s exit -> 47.159 s after 1 s, 按真实时间继续. 普通预览双击回 grid 的原有行为保留, 不宣称普通双击进入 immersion 或实现双向 toggle. 最终 phase8 已部署且 Healthy, 应用内双击队列的 source/目标校验新增 3 个 fixture 全部通过, 未增加播放器 pending 机制.

最终运行 preview DLL 为 1,483,776 bytes, SHA-256 `780B6DEC7931D8A40F6C1A471B502BF8E02C3FDC1AED09A517A11900AB3E465A`, 166 项 runtime fingerprints, unified `Healthy:true`. 最终状态与个人界面恢复记录以本轮 task_state 为准, 不将本机部署写作已发布新的远程 Release.

个人库已恢复到 `浏览器采集` folder 86, 显示 145 项, waterfall, 双侧栏显示, filter 隐藏, 搜索为空, 公开与 UI 选区均为空, 视频关闭无播放. 最终截图已检查. 保留当前 recycled 78168 和素材数变化, 不恢复删除状态; 不承诺窗口几何/缩略图尺寸与早期起点完全一致. 切库出现的库文件变更同步提示已关闭, 未触发同步, 不将其归因本次功能. 证据见本轮 task_state 的最终恢复记录.

## 在线路径

CLI 和 HTTP 共享 `Service` 与 operation schema. `items.update`, `tags.create`, `tags.assign` 检测到 Billfish GUI 运行时转发到同一安装目录的当前用户 named pipe host. GUI 未运行时保留原来的离线维护路径. GUI 已运行但宿主不可用时拒绝写入, 不回退到 SQL 修改正在打开的库.

在线宿主运行于 Billfish 进程内, 原生 adapter 在 Qt GUI 线程检查冻结 DLL 身份, manager/CLibrary 与实际库 root. 路径来自当前 owner, 不是窗口标题或最近库配置. 显式目标与当前库不匹配时返回 `library_mismatch`. 每次提交后续步骤前再检查 owner; 库关闭或切换后不再提交到原来的库.

写入先做整批只读 preflight, 检查 schema, 素材, 标签和真正需要变化的字段或关联. 然后通过 `IFileAction` / `ITagAction` 的原生 slot 写入, 接收匹配 action ID 的原生 result. 原生业务通知负责刷新相关界面与索引. 相邻步骤通过 Qt 事件循环异步调度, GUI 可以继续处理用户输入.

原生动作成功后, 宿主用独立只读 helper 核对请求涉及的主库 metadata. 读回满足预期才返回 `completion:"persisted_metadata_verified"`, 原生完成类型保留在 `native_completion`. 该验证不替代实际 GUI 验收或全量缓存校验. 验证受 5 秒绝对 deadline 约束; 到期返回 `verification_timeout`, 保留原生已提交结果, `expected`, `last_actual`, `rollback:false`. 未确认的落盘状态不能作为成功报告, 宿主也不自动重发写入.

落盘核对后还会刷新当前原生搜索, 保持用户查询文字与输入焦点, 在 `search_refresh` 中报告结果. 该机制用于更新当前可见结果, 不把本轮主库读回验证扩展解释为所有索引均已完整验证.

在线批量不承诺整批事务或自动回滚. `partial_failure` 返回逐项字段结果及原生错误码, 成功项目保留. 中断, 超时或停止插件也不撤销已经提交的原生动作. 客户端应保存 `job_id`, 先查 `jobs.get`, 再读回数据决定是否补齐. 任务记录保留在当前 GUI 进程内, 最多保留 200 个已结束任务, 不是持久化审计日志. 离线维护的备份和 attached SQLite 事务承诺不适用于在线路径.

原生 ABI 取证与覆盖边界见 [billfish-native-boundary.md](billfish-native-boundary.md). 冻结二进制版本变化时需要重新验证对应边界.

## 进度与设置

主窗口右下角显示非模态进度区域. 进度来自实际原生步骤或插件的 `completed/total`; 未知总量显示忙碌状态. 该区域不获取焦点, 鼠标事件穿透, 全部任务结束后自动隐藏. 它提供状态反馈, 不提供取消操作. 完整结果仍通过 CLI/API 或插件设置页读取.

插件页位于 Billfish 设置内, 提供本地目录安装, 插件列表, 启用开关, 权限说明, 命令选择, JSON 参数和运行结果. 新安装插件默认关闭, 安装不执行入口. 开关即时生效并独立持久化, 下次启动恢复已开启插件. 首次升级承载在线 host 的 DLL 后需要重启一次 GUI; 日常写入, 插件安装和开关不需要重启.

CLI/API 的 `plugins.run` 遵循显式 `--library` / `BILLFISH_LIBRARY_ROOT`. 设置页运行按钮使用当前原生库, 不复用过期的界面缓存路径. 当前 Electron 插件整个 session 固定绑定启动时的真实库, 打开/切换/关闭库时废弃旧 session, 已启用插件在新上下文重新初始化. 旧页面, Item 与异步调用不会跟随新库继续写. 初始无打开库时可以运行纯工具, 其库 SDK 调用返回 `library_not_open`. HTTP 使用并发请求处理, 长任务等待期间仍可查询 job 或关闭插件.

插件现在使用 JavaScript/HTML, 在独立 Electron 进程运行, 支持 Eagle `main.url` / `main.serviceMode` manifest 和有限 `eagle` SDK. 自有 `billfish.commands` / `billfish.permissions` 描述可等待的命令与生效宿主权限; 标准 window/service 自动提供 run/open 入口, `opened` 表示窗口/服务已打开, 不把任意页面业务称作完成. 未支持的 SDK API 明确返回错误. Python 保留用于 CLI/API, catalog 和验证 helper, 旧 Python 插件目录保留 incompatible, 不再执行旧 runner.

安装输入已 breaking 改为 `source_path`, 接收 `.eagleplugin`, `.zip` 或开发目录. 异步 staging 校验后整体替换, 新装默认关闭; 更新等待显式激活, 不自动执行新版. 依赖资源可安装且不独立运行, 当前仅实现 FFmpeg extraModule. Edge 下载包, 主窗口拖放, launcher warm/cold 分发与系统默认关联的当前状态单独记录在 [plugin-installation.md](plugin-installation.md).

宿主 SDK 受实际 allowlist 限制. 该边界不是 OS sandbox, 可信本地插件仍拥有当前用户的一般系统权限. 关闭插件停止接受新任务并请求 `onPluginBeforeExit`, 超时终止进程; 已提交的宿主写入可能已完成, 应读回核对. 完整 manifest, 生命周期, schema 和兼容限制见 [PLUGINS.md](../automation/PLUGINS.md).

当前 Electron 插件通信为每 session 随机私有 named pipe, Qt 使用 `UserAccessOption`, Node builtin net 双向传输 JSONL. stdout/stderr 只排空, 不承载宿主协议, 无 stdio fallback. 这是基于 Windows Electron stdin 提前 EOF 的实测修正, 不改变消息容量或写入完成语义.

## 当前 GUI 真实选区

公有接口当前 source 包含 `items.selected`, CLI 快捷命令为 `selected`. 输入严格为 `{}`, 不接收手工 item IDs. 原生 adapter 读取当前可见布局的 Qt selection model, 筛出素材 ID, 不使用预览 ID, 历史选择或整个搜索结果代替选区. 只读 helper 在同一 catalog snapshot 补齐素材数据, 返回 `{items,total,item_ids,source,library_root}`.

只有确认上下文可用且确实没有选中素材时, 才返回空数组; 未打开库或无法验证当前视图时返回明确错误. 读取前后核对相同 owner 和库 root. 插件的 `eagle.item.getSelected()` 使用该真实宿主接口, 不再使用合成选区.

公有选区完整响应上限为 16 MiB, 插件 SDK 完整 host_result envelope 上限为 4 MiB. 超限返回 `selection_too_large`, 不截断; SDK 调用方可 catch, session 继续可用. window ready 只证明进程可接受打开请求, 首次 run 才执行 create; service/commands ready 前已完成页面 create. SDK theme 目前来自操作系统 nativeTheme, 不代表 Billfish 主界面的自定义主题.

## Eagle 借鉴与本地选择

本地 Eagle 静态取证确认了 manifest, 宿主 API, 插件生命周期和设置中已知 AI 插件的嵌入入口. 可借鉴的是把模块声明, 生命周期和业务接口分开, 让 AI 功能复用宿主素材动作. 在限定读取范围内未证实 Eagle 的通用插件启用状态或统一任务百分比协议, 因而 Billfish 的即时开关和统一进度是本次产品设计, 不称为 Eagle 的既有实现. 证据, 未覆盖代码与限制见 [eagle-architecture-evidence.md](eagle-architecture-evidence.md).

本地宿主适配实现了 Eagle window/service 包格式与 SDK 的有限兼容层, 不等于完整 Eagle 插件生态. 通用资源依赖安装与 FFmpeg resolver 已有实现, 单文件/URL 图片导入已有下节实测结果, 同扩展 owned PNG/JPG/JPEG/WebP 的有限 replaceFile 已接入. thumbnail/viewer 本轮候选与 3D 实测范围见本文开头, 其他 extraModule, inspector 和未测原生 addon 仍未补齐. 历史逐包静态评估见 [eagle-plugin-compatibility.md](eagle-plugin-compatibility.md), 当前实际 SDK 契约见 [PLUGINS.md](../automation/PLUGINS.md). Billfish 媒体继续保留普通目录, 通过同一原生业务入口维护素材.

## AI 图片放大与图片替换

AI 图片放大 `1.0.9` 维护版已安装并开启. 实际插件 GUI 的 4x 对比和另存 PNG 已通过, 192 x 192 输入输出为 768 x 768. RTX 5090 上 `realesrgan-x4plus` 与 `realesrgan-x4plus-anime` 两模型的 RGB/RGBA 四次 engine 运行通过, RGBA 保留 alpha. 替换原图的真实 GUI 验收也已通过, 同 ID 的 192 x 192 PNG 更新为 768 x 768, 标签/备注/来源保持, 新原生缩略图和大图已实际打开, 替换结论另有真实 GUI 与宿主读回证据.

`items.replace_file` 仅在线, 默认 `dry_run:true`, 参数为 `item_id`, `source_path` 和可选 `dry_run`. SDK `Item.replaceFile(sourcePath)` 显式提交 `dry_run:false`. 目标须为当前库 owned 且未回收的 PNG/JPG/JPEG/WebP, source 为独立绝对路径且扩展与真实编码匹配, 不支持 `.jpg` 与 `.jpeg` 互换, linked 素材, GIF 或视频. 替换保留 ID, 路径和用户 metadata, 原文件 backup 与 `state.json` 持续保存在 `.bf/automation-replacements/<transaction>/`.

本轮实际替换输出为 PNG, 231,299 bytes, MD5 `78DCD5EA171426AFBA18317743C4D9E7`, QA 库仍为 49 素材/2 目录. 随后通过真实 SDK 将同一测试素材恢复为原 192 x 192 PNG, 验证 replaceFile 保留尚未 save 的 annotation/url/tags, 再 save 并独立读回, 最后恢复测试 metadata. QA harness 首次仅因 tags 顺序比较而断言失败, 按成员比较修正后继续完成恢复, 没有重放已成功替换. 证据为 `gui-replace-success.jpg`, `gui-after-replace-selected-result.json`, `gui-native-after-replace.jpg`, `gui-native-preview.jpg` 与 `sdk-live-roundtrip-result.json`.

本轮个人环境已恢复: 20,781 files / 357 folders, 当前选区全部 data 与本轮 before 逐项一致, 四列, 双侧栏, filter 隐藏, 搜索为空, 1600 x 770. QA 原图与持久 backup 全部 bytes 一致且没有新增素材. 证据为 `personal-restored-library-result.json`, `personal-restored-selected-result.json`, `personal-restored.jpg` 和 `final-readback.json`. 原生 Catalog/helper 受影响测试 26/26 与追加 review 通过. 公司电脑 GPU 尚未实测, 迁移步骤见 [portable-migration.md](portable-migration.md).

实际文件原子替换后由 Billfish 原生 file monitor 触发分析, helper 只读核对 bytes/hash/尺寸, metadata 和分析结果; 验证完成才返回 `completed`, `completion:"native_replace_persisted"`, `analysis_ready:true`. 超时或 partial error 保留真实写入状态与恢复信息, `rollback:false`, 不自动重放. 客户端先查原 job 和实际文件, 等明确的 `source_safe_to_delete:true` 后清理 source. 完整合同见 [PLUGINS.md](../automation/PLUGINS.md), 本轮证据状态见 [task_state.md](../task-memory/task-billfish-ai-image-enlarger/task_state.md).

## 当前新增导入与浏览器视频采集

状态: 本轮已授权范围的实现, 实际 GUI/浏览器验收与个人环境恢复已完成. URL 图片导入, 未修改 Pinterest 原包的实际 UI 保存, 真实 Edge/Bilibili 4.2.0 按钮到分辨率选择/确认, 下载, 原生入库和 Billfish 播放均实际通过. 视频结果来自用户浏览器操作, 由 root 读回任务/数据库/文件和 GUI 核验, 不是 direct nativecmd. 已知间歇 host lookup 失败的原因仍未确定, 按下面边界保留, 不声明永久解决.

该浏览器验收阶段 preview SHA-256 为 `042ABAAEF7AF842927D1A7DD51FEA3B2064598C3256F884F41661BC50F0DEDBB`, 部署事务 `0ccef78a-5b23-4c83-a24b-df0063587324`.

本轮已取得的实际导入结果:

- URL 图片已取得原生 ID, `native_import_persisted`, 库内文件与 source SHA-256 一致, source lease 释放后下载临时目录已清理.
- Pinterest 1.1.9 原包 99 个文件不改动, 通过真实 UI 点击保存成功. 一项有标题结果完整文件名落盘, 实际 852 x 480 / 13,743 bytes; `name_action` 返回真实 action ID 和 completed, 并独立读回确认. 无标题结果也以默认 `image.png` 保存成功.
- Pinterest 证据为 `pinterest-fullname-jobs-result.json`, `pinterest-fullname-item48-result.json`.
- 实际 HLS 弹窗曾显示 360p/180p 两项, 取消不创建目标目录; 确认下载后真实 browser folder 通过原生创建并保存配置. 新目录首次只读可见性处理使用同一精确 ID 的有界 fresh preflight, 不重复创建. 这些结果仍不代表 Edge/Bilibili 网页入口已通过.

最新实际视频结果: 浏览器按钮和用户选择/确认产生唯一新 video.capture 任务, 终态 completed / native_import_persisted, 素材位于真实 browser capture folder. FFprobe 核对 H.264 1080 x 1920, 30 fps, AAC 44,100 Hz stereo, 44.280544 seconds, 15,468,006 bytes. 缩略图和预览存在, 下载临时工作区已清理, Billfish GUI 实际播放已验证. 证据为 `auto-entry-after-restart-jobs-result.json`, `actual-bili-item49-result.json`, `actual-bili-final-verification.json`, `actual-bili-playback-success.jpg`. 等待期间页面自动连播到了另一视频, 验收按当时实际条目的完整 title/origin 核对, 没有冒称下载的是最初测试页面.

早前 Specified native messaging host not found 的复现仍是真实历史. 注册/manifest/HKCU/wrapper 静态检查合规, 两次完整重启 Edge 后实际流程恢复, 但日志未捕获失败分支, 未确定该间歇错误的根因, 不声称永久解决. 首次初始化后建议完整退出并重开 Edge, 无需增加采集管理策略. 独立代理修复与 Native Messaging 故障不混为同一结论.

完整维护包现含官方 Python 3.14.8 x64 embeddable, 37 个文件包括 license/provenance. 便携初始化和 Unicode/空格路径/空 PATH 检查通过, 实际初始化报告 NativeHostUsesBundledPython=true. 这解决运行依赖与新电脑注册准备, 不代替真实浏览器采集验收. 公司电脑复制说明见 [portable-migration.md](portable-migration.md), 本文不修改该迁移文档的独立验收状态.

最终恢复已验证: 日常库 20,779 files / 357 folders 与原数一致, 原选中素材的 MD5/size/dimensions 与 before 完全一致, 真实 native_current_view 单选正确, 艺术风格视图保持五列, 双侧栏/filter显示且搜索为空. 证据为 `final-personal-selected-result.json`, `final-personal-library-result.json`, `final-personal-restored.jpg`. 原本地 HTTP fixture 进程与端口均已不存在, 没有遗留需要终止的测试服务. Runtime fingerprints 已更新为 143 项. 不再要求用户重复 reload 或重做已通过流程.

`items.import_path` 与 `items.import_url` 仅在线, 无 dry_run. 前者 Copy 单个绝对路径文件并保留 source, 后者下载和解码 HTTP(S) 图片后复用同一原生 importer. folder_id 为 signed 32-bit 范围的十进制字符串, 默认 0 为库根, 不使用伪目录 ID. ID 从原生 TaskFinished 返回, 后续独立核对真实素材记录, 路径, 文件和请求字段; 成功 `completion:native_import_persisted` 含真实 item_id/item_ids/item 与 analysis_ready, 不保证缩略图或全部分析已就绪.

SDK `eagle.item.addFromURL` / `addFromPath` Promise 返回真实 ID, 支持 name/website/零或一个 folders 选项; 未支持的 tags/annotation 等在导入前拒绝. 多步/取消/超时不假报回滚, 保留已观察到的 ID 并先核对 jobs. 已提交 source lease 直到原生任务完整释放才可清理. 精确 SDK 合同由 [PLUGINS.md](../automation/PLUGINS.md) 维护.

`video.capture` 只立即返回 accepted/job_id, 下载前在 Billfish 窗口选择探测到的实际分辨率, 不自动降级. 下载核对真实视频/画幅/帧率/所需音轨, 缺片段失败; 完成后导入绑定库真实浏览器采集 folder. 页面受理提示不等于下载或入库完成. 维护版扩展保留普通图片采集路径, 视频经 Native Messaging 单次提交, 不采集 cookies/账号. 参数和运行步骤见 [automation/README.md](../automation/README.md), [浏览器维护版](../browser-extension/maintained/README.md), [MEDIA_TRANSFER.md](../automation/MEDIA_TRANSFER.md).

## 前轮 JS / 插件包 / 真实选区验收

状态: 本轮已验证范围的实现, GUI 验收与环境恢复均已完成. 真实选区与 JS 保存, 系统默认关联, 本地包 cold/warm 安装, Pinterest 相似图搜索和视频转 GIF 另存核心流程均取得实机结果, 四种原生布局与 toolbar 交互也通过检查. 日常库仍为 20,779 项, 原目录, 选区和窗口已恢复. 这不是全 Eagle API 或原包所有按钮均已兼容的声明.

本轮已观察:

- 新 JavaScript `example.material-notes` 经真实 `items.get` / `Item.save()` 修改一项测试素材, 原生属性栏即时更新. 本地证据为 `gui-js-command-write.json` 与对应 `.png`.
- 新随机私有 named pipe 的 Electron ready/run/shutdown 有界实际运行通过.
- 插件完整 QtTest 原执行 15 项通过, startup error 修正后定向测试通过; UI 原测试通过, caption 修复后 toolbar CTest 1/1 通过. 这些是各阶段已执行的实际报告, 不将其描述为所有历史测试在同一 build 上重新完整运行.
- Native 真实空选区和修正后的非空选区已通过实机 fixture; JS fixture 已完成 getSelected, annotation/url/tags 三字段 save 与关闭后重开. 该结果不自动覆盖所有原生布局或第三方包业务.
- 原 Eagle 以图找图包已观察到 Bing 自动跳转, 但结果页视觉尚未核验, 不称为完整搜索结果验收通过.
- `.eagleplugin` 默认关联已按授权注册, `AssocQueryString` 确认 effective executable 为根 `Billfish.exe`. 冷启动打开 FFmpeg 约 268 MB 原资源重打容器后, 只启动一个 core, 完成 dependency 安装并定位设置页; 已运行时打开官方 Pinterest 1.1.9 包, 同一 core 完成安装, 新插件 disabled. 证据为 `association-cold-warm-result.json`, `cold-package-install.png`, `warm-package-install.png`.
- 上述部署后的 unified status 为 Healthy. Edge BrowserUse 的下载直链导航返回 `ERR_BLOCKED_BY_CLIENT`, 未完成下载; 官方包另经 HTTP 获取并校验哈希, 不称为 Edge 下载到安装的端到端验收.
- Pinterest 1.1.9 原包读取隔离测试库唯一选中的合成 PNG, 显示约 18 张 `i.pinimg.com` 相似结果, `pinterest-search-success.png` 已保留实际窗口结果.
- video2gif 1.0.2 原包经 Windows 默认关联 warm 安装, 使用测试 MP4 执行一键另存, 输出 `GIF-QA.gif`. ffprobe 实测 217,008 bytes / 320 x 180 / 10 fps / 32 frames / 3.2 seconds, 证据为 `gif-output-probe.json` 和 `gif-conversion-success.png`. 原 Eagle 安装目录未改动.
- caption 修复版实机通过 `Alt+1..4` 四种布局, 每种当前选区均正确返回测试 id 40; 选区证据为 `final-layout1-selected.json` 至 `final-layout4-selected.json`. 窗口, 快捷键, 侧栏, caption 与真实 topmost 的详细检查见 [compact-shell.md](compact-shell.md).

最终恢复结果: 原目录与选中素材恢复, 无搜索, 双侧栏/筛选条显示, 五列瀑布流, 1160 x 700 窗口. unified status 六项 Healthy, effective association 正确, core 进程唯一. 40 个合成测试原文件 Changed 0, 三包原文件完整性核对通过, video2gif 仅新增自己的临时 GIF. 证据为 `final-personal-restored.png`, `final-unified-status.json`, `final-association-status.json`, `final-synthetic-file-integrity.json`, `installed-package-integrity.json`.

在该验收阶段 Pinterest 保存入库尚未实现; 当前 source 新增导入后的验收状态见上节. GIF 一键替换超出当前 PNG/JPG/JPEG/WebP 替换范围, 未通过该包 GUI 验收. 未覆盖的更新/拖入路径不能由上述历史结果推断通过, 上轮 Edge 包下载限制也仍保留, 与本轮视频采集分开. 顶部界面范围以对应 shell 文档为准.

## 上轮在线 API / Python 插件 GUI 验收基线

以下为 JavaScript 迁移前的已完成基线, 其中插件进程与生命周期属于当时的 Python 实现. 在线写入, 当前界面同步, 进度和原插件功能的实际 GUI 验收已完成, 验收环境也已清理并恢复日常素材库. 验收使用隔离的 40 素材测试库, 未向个人库执行验收写入. 这些历史结果不替代当前迁移验收.

本轮部署的 preview DLL SHA-256 为 `47304F7607D6696B01E71C9D1137CBC9416AA837D60A54D493FFB3B462EDAA63`. 以下结果来自该阶段的真实运行与聚焦测试, 不以源码或对象探针代替界面结果.

| 已完成项目 | 实际观察 |
| --- | --- |
| 备注与来源在线修改 | 当前选中素材的详情即时更新; 切换素材后再次查看仍保留 |
| 原生标签关联 | 添加/移除后当前素材标签面板同步 |
| 原生搜索索引 | 重新执行关键词查询后, 新备注关键词命中, 旧关键词不再命中 |
| 当前搜索结果自动刷新 | 查询 `GUIQueryAlpha42` 命中 40 项, API 将备注改成另一关键词后, 无需重输查询即变为空结果; `query_unchanged`, `focus_preserved`, `refreshed` 均为 `true` |
| 落盘后立即查询 | `acceptance_online.py` 通过 update/create/add/remove, 每次返回后立即用公开 get/标签 list 核对, 实际写入均返回 `persisted_metadata_verified`, 本轮观测 `verification_count:3` |
| 请求边界 | no-op, 整批中含无效输入, 错误库目标与相同请求 replay 的验收通过 |
| 示例插件批量写入 | `example.material-notes` 完成 40 项素材备注写入 |
| 设置入口与布局 | `Ctrl+,` 和 `ui.settings.open` 均打开原生 Preferences 的插件页; 深色样式清晰, 两个测试插件完整列出 |
| GUI 开关同步 | 设置页关闭插件后, CLI 查询 `enabled:false` 且 `commands:[]` |
| 任务中关闭插件 | GUI 关闭执行中的插件后撤下命令, job 返回 `plugin_disabled`, 约 2 秒后状态为 `disabled` |
| 插件开关持久化 | 开启和关闭状态均经过重启保持验证 |
| 插件异常隔离 | GUI 运行错误命令后显示预期 error, 随后仍可运行 30 tick 慢任务 |
| 进度与窗口操作 | 慢任务期间最大化/恢复主窗时进度跟随, 主窗仍可操作; 非模态布局与输入行为通过 GUI 验收 |
| 插件生命周期 | GUI 重新启用并在空闲时关闭插件, 实际 `on_create` / `on_shutdown` 标记均完成 |

本轮当前搜索刷新报告 `elapsed_ms:1`, `mask:511`. 这只是隔离 40 素材库的一次测量, 不代表大型生产库性能基线或每次执行耗时.

自动化验证: Python 55 项全部通过. Native/插件的 4 组测试通过; UI fixture 修正后, 完整 UI 测试可执行文件报告 7 项通过, 其中 5 项业务测试及 init/cleanup. 构建 exit code 为 0. 这些测试与实际 GUI 验收共同支持上述结果, 不意味着所有异常分支均在 GUI 中逐一制造.

环境收尾已完成:

- 通过原生 GUI 切回日常素材库, 记录仍为 20,779 项. 携带旧测试库 root 的在线 `dry_run` 更新返回 `library_mismatch`, 没有写入个人库.
- 已停用并移除安装后的 QA fixture 插件, 测试源和日志留作本地验收记录. 最终插件列表仅保留 `example.material-notes`, `enabled:false`, `status:"disabled"`.
- 通过根目录 `Billfish.exe` 重启后, native host 为 `ready:true`, 未遗留 `billfish_plugin_runner` 孤儿进程.
- 最终 unified status 为 `Healthy:true`, Launcher, DPI, HDR, AV1, VideoTrim 均健康, legacy launcher 入口不存在.
- 最终 GUI 截图显示原生 720 x 530 设置页及唯一关闭的示例插件. 本地验收记录为 `gui-plugin-settings-final.png`, `final-unified-status.json` 和 `final-runtime-status.json`.

该轮基线任务当时已完成全部验收与环境收尾; 当前 JS/选区迁移状态见上节. 在线批次非原子, 主库验证覆盖范围及插件非 sandbox 等产品边界仍按本文说明保留.
