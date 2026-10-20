# Eagle 架构取证与 Billfish 设计借鉴

取证日期: 2026-10-18 (Asia/Shanghai). 范围: 用户指定的 `E:\Software\Eagle` 安装制品. 本报告是只读静态取证, 未启动 Eagle, 未调用其 HTTP API, 未访问用户素材库, 未复用其实现代码.

## 制品与证据身份

- `resources/package.json` 与 ASAR 内同名成员内容一致: Eagle `4.0.0`, buildVersion `20260304`, buildNumber `20`, main `run.js`.
- `resources/app.asar` SHA-256: `1f43621a7cbef7f13d488fa1e248137abe18f7f6411f3fccffaf3894f3c2f6d0`.
- ASAR 格式解析枚举 10884 个文件成员, payload 起点 576048. 完整成员表见 `task-memory/task-billfish-cli-api/eagle-asar-inventory.json`.
- 本地证据编号 `EAGLE-S01` 至 `EAGLE-S07` 指本次从精确 ASAR 字节和独立文件读取的静态观察, 不是 REA 的 Evidence ID. 每个选读成员的完整哈希见 `eagle-selected-hashes.json`, 随包文档哈希见 `eagle-document-hashes.json`.
- REA 全包 summary 的客户端等待在约 3 分钟时结束, 当时按目标 SHA-256 查询 retained records 为 0. 后续取证发现服务端仍继续分析并已完成, 得到父 Evidence `ev_31f1d058126116bc2f2e937de1e252549699d1708e7b0bea155f6769a20ce481`. 完整导出记录显示 7049 relevant files, 6099 parsed JavaScript files, 50717888 text bytes, 11 parse failures, graph coverage=partial, limitation_count=12115. 不据此宣称完整恢复 Eagle. 其完整 session bundle 已以 gzip 压缩保存于 `task-memory/task-billfish-cli-api/eagle-plugin-rea-evidence.json.gz` (解压后 267597635 字节). 改为对精确成员字节的最小子集 (ipc.js, model/item.js, ai-action.js) 静态解析, 得到父 Evidence `ev_bb9e02341fa7fbf2d807af647e4519d269b42372c08af38594f60bc885247120`, summary view `ev_23e40352309a548e413e145c96a2ae9487369086d953f54bdbc18818fd944a5b`. 子集 SHA-256 `4b600added03ada9b31ceb1ef35f09a8bdf3ed518a3bb830c712e67d5b03aae4`, 3 个 JS 文件/21866 字节, 0 parse failures; application coverage=partial, semantic coverage=unknown, 766 semantic unknowns, 动态 IPC 未自动配对. 完整 REA 记录导出为 `task-memory/task-billfish-cli-api/eagle-rea-evidence.json`. 原始临时提取文件在分析后删除, 保留成员表、哈希和非实现性证据说明. 主进程入口使用 bytenode 的 `run.jsc`/`main.jsc`; 本次未恢复字节码内部实现.

## 已观察的能力路径

| 证据 | 观察 | 精确位置 | 对结论的限制 |
| --- | --- | --- | --- |
| EAGLE-S01 | HTTP API 注册固定端口 41595, 使用 `JsonRestServer`, 包含 application/library/folder/tag/item 方法 | `app/js/api-server.js:4`, `:981` | 仅源码声明, 未证明当前端口已监听 |
| EAGLE-S02 | HTTP handler 直接读取 `$bodyScope.itemMappings`/folderMappings, 创建目录后调用 saveFolder, item 更新调用 ayncsImagesChange, 导入进入现有添加/后台流程 | `app/js/api-server.js:309`, `:449`, `:736` | 支持复用宿主业务状态, 不支持推断为独立 headless 服务 |
| EAGLE-S03 | 插件窗口 preload 为 `plugin/api.js`, SDK 暴露 Item/Folder/Tag/TagGroup; Item.get/query/save 通过 r2r/sendTo 调用主 renderer 中同名 IPC handler | `app/js/plugin/main.js:160`, `app/js/plugin/api.js:230`, `app/js/plugin/ipc.js:15`, `app/js/plugin/model/item.js:195`, `app/js/plugin/index.js:1512` | 动态 channel 的完整配对由本次人工阅读推断, 未进行 runtime reachability 验证 |
| EAGLE-S04 | Item.filePath 由库根目录、item ID、文件名与扩展名组成, metadataFilePath 指向同目录 metadata.json; library/info 读取库根 metadata.json | `app/js/plugin/model/item.js:364`, `:385`, `app/js/api-server.js:91` | 是路径构造逻辑, 未检查真实用户库的磁盘内容和完整 schema |
| EAGLE-S05 | 声明 dependencies 包含 ai-sdk 的插件, 在初始化时加载用户目录 `Plugins/ai-sdk/ai-sdk.cjs`, 传入该目录下 config.bin, 合并到 eagle.extraModule.ai | `app/js/plugin/api.js:84`, `app/js/plugin/extra-module.js:65` | 未读取 ai-sdk.cjs 与 config.bin, 不证明已安装、密钥加密或模型连接成功 |
| EAGLE-S06 | AI Action 将选中素材转为 item IDs, 打开 ai-action 插件; AI Search client 调用 localhost:38766 的 status/health/search/text/search/image/search/id/sync/full | `app/js/lib/api/ai-action.js:31`, `app/js/lib/api/ai-search.js:8`, `:158`, `:222` | 未取证搜索插件本体, 不证明 embedding 模型、向量库或索引增量机制 |
| EAGLE-S07 | 随包开发文档将 AI SDK 描述为共享 provider/model 配置, 支持 generateText/generateObject/streamText/streamObject/default model; service 插件以 main.serviceMode 声明 | `eagle-plugin-document/en-US/extra-module/ai-sdk.md:15`, `:68`, `:196`, `eagle-plugin-document/en-US/get-started/plugin-types/service.md:7` | 文档声明与宿主加载逻辑相互支持, 但外部依赖插件实现和实际行为仍未知 |

HTTP 和 SDK 的汇合点可概括为:

```mermaid
flowchart LR
  H[HTTP API 41595] --> R[主 renderer 业务状态和操作]
  P[插件 SDK Item/Folder/Tag] --> I[renderer-to-renderer IPC]
  I --> R
  R --> F[保存/导入/缩略图现有流程]
  A[AI Action 插件: item IDs] --> P
  D[可选 AI SDK: 共享模型配置] --> A
  S[AI Search 独立插件服务 38766] --> P
```

此图中连线是所读源码支持的静态调用或设计关系. AI Search 服务内部没有被本次检查, 图不表示全部调用路径或执行时序.

## 文件可访问性与用户约束

Eagle 的素材 getter 使用 `<library>/images/<id>.info/<name>.<ext>`, 缩略图与 `metadata.json` 放在同一个 `.info` 目录. 这支持保留普通磁盘文件, 但会引入由 Eagle 管理的目录组织. Billfish 的目标是保留当前文件位置和资源管理器可见性, 不需要迁移到 Eagle 的 `.info` 结构. 可借鉴稳定 item ID 与元数据分离的思想, 当前 Billfish 的文件路径仍应作为真实文件位置返回.

## 对 Billfish 的可借鉴设计 (设计判断, 非 Eagle 事实)

1. 先建立可枚举的业务命令与参数/结果 schema, CLI、HTTP API、未来 MCP 共用同一能力描述和执行语义. 这样 AI 可以发现能力, 人也能从命令行复现操作.
2. 查询返回稳定 ID、绝对文件路径、媒体类型、标签和来源; 让 AI 读取普通磁盘原文件, 避免把文件包进专用容器.
3. 写入通过 Billfish 当前 runtime 的业务边界完成, 保存后更新索引和 UI. 不绕过正在运行的宿主直接修改其活跃数据库.
4. 用明确的 metadata patch 支持 AI 分类建议, 让导入、移动、删除、标签和扩展处理具备可辨别的结果. 批量工作需要逐项结果与错误, 避免把队列接受误报为全部完成.
5. 模型配置、生成能力和素材业务 API 可以分离. 本轮 CLI/API 底座无需绑定某个模型供应商; 外部 agent 可以先通过查询/文件路径/标签写入完成整理.
6. Eagle 已读代码依赖 renderer 状态, 并不能证明 AI 接入必须先重建为完整独立服务. Billfish 可先把现有业务能力形成稳定边界, 再按实际需求增加插件或后台工作.

## 边界与未决项

- 未查到 Eagle 原生 CLI 的实现证据; 本报告不宣称 Eagle 本身已 CLI 化.
- 未读取 AI 插件配置、凭据、模型请求和用户资产; 不评价 AI 输出质量或隐私行为.
- 未逆向 bytenode 主进程字节码, 不断言完整进程拓扑、全局安全策略或所有业务能力均经过同一服务层.
- `my_modules/json-rest-light/lib/json_server.js:68` 的源码调用 `listen(port)` 未显式传 host; 日志中的 localhost 不等于已验证 loopback-only. Billfish 新 HTTP 接口应根据自己的目标明确绑定 loopback, 不能照搬该细节.
- 插件窗口源码声明 nodeIntegration=true、contextIsolation=false、webSecurity=false. 这说明其所读插件机制给予较广的 Node/Electron 能力, 不能据此认定这是 Billfish AI 接入所必需的权限模型.
- HTTP 中存在 script/inject 路由; 本轮 Billfish 设计不需要增加任意脚本执行能力, 已命名业务命令足以覆盖当前整理场景.

## 验收问题回访

| 问题 | 状态 | 证据 |
| --- | --- | --- |
| 本机 Eagle 版本和入口 | 已回答 | package.json / run.js / hashes |
| HTTP API 如何接入素材业务 | 已回答到所读 handlers | EAGLE-S01/S02 |
| 插件 SDK/IPC 如何接入业务 | 已回答到 Item/Folder/Tag 路径 | EAGLE-S03 |
| 文件与元数据布局 | 部分回答 | EAGLE-S04, 未检查真实库 |
| AI 如何复用已有能力 | 已回答宿主入口, 插件内部未知 | EAGLE-S05/S06/S07 |
| 是否已有独立 headless service/CLI | 未证明 | bytenode 主入口与插件本体未分析 |

## 关键成员 SHA-256

| ASAR 成员 | SHA-256 |
| --- | --- |
| `app/js/api-server.js` | `1bacf57575b881b2f79e85a1568ace9e95ff8c08d71b7863111fe392753ce626` |
| `app/js/lib/api/ai-action.js` | `285be895def463aef5a0a5fbdaedf554fbc3733d7ae334fbc1df7c9bdf474c1a` |
| `app/js/lib/api/ai-search.js` | `aebdfaa8a6426546b9d10a0ded93cf774709ec3f3c618893b9b07d13bb0a4ad2` |
| `app/js/plugin/api.js` | `aa0d34c01ad6e0dd2e37a587266ba7be89b2c7c7908765e16bd23876fa55900b` |
| `app/js/plugin/extra-module.js` | `4bd7343db90ddd550b5d9dc7025dfc9ea5d7dc36c0f6da9d54e2e60936305fe8` |
| `app/js/plugin/index.js` | `5c25bd45c4cc8317a045335f7aff849b8032a88bc2307cb08813cf15988c053f` |
| `app/js/plugin/ipc.js` | `b09bfe8444f4fb15e7fba290cda1158a76d2a053fd904b2c22c98512fccce271` |
| `app/js/plugin/main.js` | `23975c9b65714250c4f4c35905abff4bcc1ee14a7fb42c73e176d04316ace49f` |
| `app/js/plugin/model/item.js` | `524f5070494b00ddceceb48e90d426b9b7a5588f195c5d9e3cd10d2bd9b66c5e` |
| `package.json` | `d692f93fc7f0cdb01e49e7161477b0b81e22c8b071479c9cdf1a5add4a59df8a` |
| `run.js` | `e84f463ab8be495e946432bd8e171cccdcf560cac4c0a1271ce33ada97284edf` |

外置模块 `resources/my_modules/json-rest-light/lib/json_server.js` SHA-256: `3afbf69ce68adea8ca3ee930d08f78eb34ebee49844d0e666e1be53dc4048280`.


## 插件生态续查: 模块化、设置开关与进度

本节于同日延续取证. 复用相同 ASAR 字节与上轮成员哈希, 只定向读取 plugin index/main/api、plugin-panel/center、preferences/default-preferences、随包 event/notification 文档与原安装 `resources/plugin_templates`. 未再次启动全包扫描, 未执行插件, 未读取用户配置或凭据.

模板目录 REA 父 Evidence 为 `ev_44db71d3baa34307a3256605ee3061b13ff76fbd78cde9aa4e8f308b1398a90d`, summary Evidence 为 `ev_6a1aab540df18cec35536d0afa3dfc1c11ca4b19b71f7b475a773c86ea6700fc`. 原目录 SHA-256 为 `ef548fe1ba2d6ab553299d1d3e74cf77ec1fbd1a72d85dc0681c475152383aea`. 覆盖 13 relevant files/10618 字节, 5 parsed JS, 0 parse failures; application coverage=complete 仅针对该模板目录, semantic coverage=unknown, 229 semantic unknowns. 新增逐文件哈希见 `eagle-plugin-ecosystem-hashes.json`. 完整 session 导出见上述 `.json.gz`, 包含本节模板记录与上轮分析记录.

### 实际观察与边界

| 证据 | 静态观察 | 来源 | 边界 |
| --- | --- | --- | --- |
| EAGLE-P01 | 枚举 `<userData>/Plugins` 的目录, 合并本地开发目录, 去重后逐个 loadPlugin; loadPlugin 读取 manifest.json 与 _locales | `app/js/plugin/index.js:10`, `:941`, `:952`, `:968`, `:1116`, `:2713` | 未执行目录扫描, 不证明当前已安装列表 |
| EAGLE-P02 | ZIP 安装提取后读取 manifest.id, 复制到 Plugins/id, reload 并广播 installed; 本地开发目录路径持久化到 localStorage 的 PLUGIN_LOCAL_PLUGINS | `app/js/plugin/index.js:498`, `:530`, `:733` | 不是 Billfish 的安装实现建议; 未完成该 ZIP 流程的完整安全审计 |
| EAGLE-P03 | manifest.main 为 window 或 serviceMode; preview 声明 viewer/thumbnail/inspector 能力, 注册到按扩展名索引的映射 | `app/js/plugin/index.js:1026`, `:1135`, `resources/plugin_templates/*/manifest.json` | 能力类型由 manifest 结构和宿主代码确定, 不是任意动态 RPC 注册证据 |
| EAGLE-P04 | SDK 生命周期为 onPluginCreate/onPluginRun/onPluginBeforeExit/onPluginShow/onPluginHide/onLibraryChanged, API 注册回调转发实际 IPC 事件 | `app/js/plugin/api.js:109`, `app/js/plugin/ipc.js`, `eagle-plugin-document/en-US/api/event.md:9` | 所读接口没有 onShutdown 名称. BeforeExit 没有异步清理完成保证 |
| EAGLE-P05 | 主进程 create-plugin-window 建立 BrowserWindow, 首次加载发送 create/run, 已有窗口每次 open 发送 run; serviceMode 初始隐藏, close 阻止关闭并 hide, initServicePlugins 统一创建 | `app/js/plugin/main.js:182`, `:306`, `:331`, `app/js/plugin/index.js:2756`, `:2767`, `:2842` | service 仍是 Electron 窗口宿主, 未证明独立 worker 或后台 OS service |
| EAGLE-P06 | destroyPlugin 对 service 移除 close 监听并 destroy; 普通窗口关闭发送 plugin-exit | `app/js/plugin/index.js:775`, `app/js/plugin/main.js:339` | 强制 destroy 路径不能视作已承诺每次执行 BeforeExit |
| EAGLE-P07 | 设置页检测 mcp-server/ai-search/ai-sdk 是否安装; 未安装则触发 install-plugin, 已安装嵌入插件 HTTP UI | `app/js/preferences.js:1268`, `:1276`, `app/preferences.html:1221`, `:1270`, `:1323` | 这是三个已知插件的专用集成, 不证明任意插件都可注册设置页 |
| EAGLE-P08 | MCP 设置 iframe 为 localhost:41596/web-ui, AI SDK 为 localhost:41597/web-ui, AI Search 为 127.0.0.1:38766; 传 theme/locale | `app/js/preferences.js:1283`, `:1293`, `:1310` | 仅 URL 与嵌入入口, 未观察插件 HTTP service 的实际启停实现 |
| EAGLE-P09 | 插件下载 onProgress 更新 installingPluginMaps[id].percentage 并通知 Angular UI 更新 | `app/js/plugin/index.js:374`, `:406`, `:422` | 这是下载/安装进度, 不能当作所有插件任务的统一进度 API |
| EAGLE-P10 | SDK notification.show 发送标题/正文/静音/时长, 固定 progress:false; service 模板允许自己的 UI | `app/js/plugin/api.js:179`, `resources/plugin_templates/service/index.html` | 未找到通用任务百分比上报/统一任务中心协议 |

### 启用/禁用的证据边界

在本次明确读取的 `app/js/plugin/index.js`, `main.js`, `api.js`, `plugin-panel.js/html`, `plugin-center.js/html`, `preferences.js`, `preferences.html`, `default-preferences.js` 与模板中, 没有观察到通用插件 `enabled/disabled` 状态的持久化与加载过滤. 可确认的持久化项目包括本地开发路径、快捷键、置顶、排序和最近使用. `disabled` 搜索命中包括不可执行列表项、不可用下载按钮和排序组件选项, 不能当作禁用插件的实现.

因此, “用户在设置中开关功能模块”应作为 Billfish 自己的明确产品能力实现. 本次负面搜索仅限上述范围; 未反编译 bytenode 主入口或下载的 mcp-server/ai-sdk/ai-search 本体, 不断言 Eagle 全产品没有任何模块开关.

### 可直接借鉴的最小模式 (Billfish 设计选择)

插件 manifest 与启用状态应分开. manifest 描述模块身份、版本、入口、运行模式和可发现命令; 用户设置保存 id 对应的 enabled 和配置, 刷新插件时不覆盖用户选择. 最小字段可以是:

| 字段 | 用途 |
| --- | --- |
| id/name/version | 稳定身份、设置列表展示、版本核对 |
| entry | 插件实际入口, 相对于插件目录 |
| mode | command 或 service, 初期以 command 满足按需整理功能 |
| commands | 命令名、说明、input schema、是否写入素材; 接入同一 CLI/API 能力发现 |
| settings | 少量有类型的用户配置及默认值; 可由宿主生成设置表单 |

生命周期建议明确为 `onCreate(context)`, `onRun(context, args)`, `onShutdown(context)`. 这是 Billfish 自选命名, 不是逐字继承 Eagle API. 插件在 onCreate 接入宿主授予的素材查询/metadata patch/任务进度能力, onRun 返回实际结果; onShutdown 用于正常停用时释放资源. 对强制终止仍应由宿主独立完成任务终态与清理, 不把插件回调成功当作唯一保障. 避免以任意脚本注入作为插件业务接口.

开关与运行状态应是两个字段. 用户关闭模块后, 列表仍保留该模块与关闭状态, 能力发现可显示但应明确不可调用; 新调用返回可识别的 plugin_disabled 错误. 正在运行任务是否取消必须明确采用一个策略并记录到接口文档, 不能把“窗口隐藏”当作“后台任务已停止”. command 模式可以先阻止新调用并让已受理任务完成, service 模式需要显式 shutdown 路径. 初次加载插件不要自动执行一次素材写入.

进度应以宿主 jobId 贯穿 CLI/API 与设置页的运行列表, 最小记录 `pluginId/command/status/createdAt/updatedAt/result/error`. 只有插件报告可测量的 completed/total 时才生成百分比; 否则用 stage/message 表达执行状态. 终态明确区分 succeeded/failed/cancelled. 插件调用完成与素材磁盘保存完成的语义需要一致, 批量任务返回逐项结果. 这些是满足用户进度需求的设计, 本次未在 Eagle 中证实等价统一协议.

本节没有复制 Eagle 的实现代码或模板到 Billfish runtime. manifest 字段与生命周期建议依据观察重新设计, 能在保留当前磁盘文件组织的前提下添加功能模块.
