# Billfish AI 接入入口

自动化入口为安装根目录的 `billfish-cli.cmd`, 自动优先使用包内 `tools/python/python.exe`, 完整维护包无需额外安装 Python 或修改 PATH. 直接入口为 `tools\python\python.exe -I automation\billfish_cli.py`, bundle 为官方 Python 3.14.8 x64 embeddable. 开发环境缺少 bundle 时可用 Python 3.11+ x64, 无 pip 依赖. GUI 仍通过根目录 `Billfish.exe` 启动, 日常写入不需要重启 GUI. 新电脑初始化与扩展加载见 [portable-migration.md](../docs/portable-migration.md).

1. 先运行 `schema`, 获取当前 operation 的真实输入 schema 和 effect. 用 `info` 确认库, GUI 运行时还可 `call host.info` 确认原生桥接 `ready` 和实际 `library_root`. 显式 `--library` 必须放在子命令前, 在线写入目标必须匹配 GUI 当前库.
2. 用 `search`, `item`, `folders`, `tags` 获取真实 ID. 以当前 `schema` 发现 operation, `selected` 或 `call items.selected` 读取当前原生 GUI 视图的真实选区, 只接受 `{}`, 不允许传入 item_ids 代替选区. 返回 `{items,total,item_ids,source,library_root}`; 真实空选区返回空数组, 不可用上下文返回错误. 公有响应上限 16 MiB, 插件完整 host_result envelope 上限 4 MiB; 超限 `selection_too_large`, 不截断, 可减少选择后重试读取. ID 为十进制字符串. 搜索为字面子串匹配, 不是语义搜索或原生 FTS 语法; `folder_id` 仅匹配直接子素材.
3. 通过返回的 `path` 或现有 preview 路径, 使用 agent 自己的文件/媒体工具读取素材. 处理 `path_error`, `exists=false` 和 `null` preview 路径. 链接或回收素材没有经确认的普通文件路径.
4. 只执行用户任务范围内的修改. `items.update` 修改 `note` / `origin`, `tags.create` / `tags.assign` 维护原生标签. 使用 `call OPERATION --input request.json`, 避免 Windows shell 对中文或引号的转义问题. 无参数操作可省略 `--input`.
5. 备注/来源/标签修改默认 `dry_run:true`. 检查计划后, 使用 `dry_run:false` 执行已获授权的变更. GUI 开着时自动进入原生在线业务层; GUI 关闭时采用离线维护. 新增 imports 仅在线且没有 dry_run, 不能套用离线写入语义. `online_unavailable`, `library_mismatch`, `native_not_ready` 等错误不得通过直接 SQL 写运行中库或终止 GUI 来绕过. 接口本身不要求额外的人为批准.
6. 区分两种写入语义. 实际在线修改需等待匹配 action ID 的原生完成结果和独立只读 helper 的主库 metadata 核对, 成功返回 `source:"online"`, `job_id`, `completion:"persisted_metadata_verified"`, 原生完成类型保留为 `native_completion`. 验证有 5 秒绝对 deadline; `verification_timeout` 保留原生结果, `expected`, `last_actual`, `rollback:false`, 不自动重新提交. 多个原生步骤不组成整批事务. `partial_failure` 的 `error.details.result` 含逐项结果, 已完成修改保留. 离线模式每批实际修改前创建双库备份, 在一个 attached SQLite 事务内维护主库和全文索引, 保存 `backup_directory`. 不同 API 请求不组成跨请求事务.
7. 超时, CLI 中断, 插件停用或连接断开不保证任务取消或回滚. 保留返回的 `job_id`, 用 `call jobs.get --input job.json` 查询任务, 并用 `items.get` / `items.search` / `tags.list` 核对实际状态. 不自动重试有不确定结果的写入. jobs 记录仅在当前 GUI 进程内保留, 重启后应直接读回数据.
8. 插件能力先用 `call plugins.list` 发现, 核对 mode/compatible/permissions/commands. 当前插件为独立 Electron 的 JavaScript/HTML, 接入 Eagle window/service manifest 子集; Python 插件 runner 已移除, 旧目录保留 incompatible. `plugins.install` 只接受 `source_path`, 可为本地 `.eagleplugin` / `.zip` 或开发目录, 异步安装不执行入口, 请求等待最终结果, 进度可查 jobs. 新插件默认关闭; 更新需停用旧 session, 完整替换后 updated, 等待显式激活. `plugins.enable` 保存开关并启动或停止会话, 运行前等待 `status:"ready"`. `plugins.run` 遵循显式 `--library` / `BILLFISH_LIBRARY_ROOT`, 参数按声明 schema 验证, 不保证默认 dry-run. 普通 window/service 的 `opened:true` 只表示入口已打开; `billfish.commands` 的 handler 才返回可等待的命令结果. `ui.settings.open` 打开软件内插件页. 只启用用户授权范围内的插件; SDK operation allowlist 不是 OS sandbox.
9. 读回验证后报告真实改动和失败项目. 在线落盘确认后刷新当前 GUI 原生搜索, 保持查询文字和输入焦点, `search_refresh` 提供本次结果. 主窗口的非模态进度条展示原生任务/插件任务进度, 不获取输入焦点, 不提供取消. 不把任务已接收或进度条出现当作成功, 不把主库 metadata 验证描述为所有索引完整校验.

成功 CLI 输出为单个 `{"ok":true,"data":...}` JSON 对象; 失败为 `{"ok":false,"error":...}`. Exit code: 0 成功, 2 参数错误, 1 业务失败, 130 一次性命令被中断. `schema` 不需要选定库. 同一批 operations 也通过显式启动的本机 HTTP API 提供, 现有鉴权和端点用法见 [README.md](README.md) 和 [openapi.json](openapi.json).

插件 session 固定绑定启动时真实库. 初始无库可运行纯工具, 库 SDK 调用返回 `library_not_open`. 打开, 切换或关闭库会废弃旧 session 并为已启用插件创建新会话, 旧 Item 不得换绑新库继续保存. Electron Eagle SDK 的未实现 API 返回 `unsupported_api`, `Item.save()` 仅允许 annotation/url/tags, 多步保存可能 `partial_update`. 不因原包窗口能打开就报告所有功能已兼容.

dependency 为资源包, 不独立运行. 新装资源成功后即使 disabled 也可供消费者解析; updated 依赖需显式激活, 活消费者阻止更新. 当前 extraModule 只有 FFmpeg, `install()` 返回 dependency_install_required 并指引管理安装, 不自行下载. 本机 `.eagleplugin` 默认关联已注册并验证到唯一根 launcher, cold/warm 本地包打开通过; 不将其扩大为 Edge 完整下载流程已通过. 换机后先查 effective association, 细节见 [plugin-installation.md](../docs/plugin-installation.md).

已实测的原包核心路径包括 Pinterest 1.1.9 相似图结果与 UI 保存入库, video2gif 1.0.2 一键另存普通 GIF. Pinterest 保存已核对真实 ID/文件及完整文件名, imports 已部署. imports 直接执行, 成功等待 native_import_persisted 并返回真实 ID/item 与 analysis_ready; video.capture 只立即 accepted/job_id, 需用户在软件选择实际分辨率, 通过 jobs.get 查询下载及真实浏览器采集 folder 入库结果. SDK addFromURL/addFromPath 只支持已声明选项, 不能伪造 tags/annotation 成功. 超时保留真实 ID/错误, 不自动重试. 当前真实 Edge/Bilibili 4.2.0 采集按钮经用户点击和分辨率选择/确认, 新任务已完成下载入真实浏览器采集 folder, 实际文件/probe/缩略图与 Billfish 播放均已核验. 先前 host-not-found 失败真实存在, 重启后恢复但日志未捕获失败分支, 不编造已确定的根因或永久修复. 首次初始化后建议完整退出重开 Edge, 不添加采集管理策略. GIF 一键替换仍未实现, 原 UI 按钮存在不代表宿主有能力.

应用内鼠标/键盘控制使用 [billfish-ui-control.md](../docs/billfish-ui-control.md) 的无依赖 Node client. 当前 Codex Node REPL 用 `createRequire` 加载 `.cjs`, 普通 CommonJS Node 也可直接 `require`:

```js
var bfrequire = (await import('node:module')).createRequire('E:/Billfish/automation/billfish-ui.cjs');
var bfm = bfrequire('E:/Billfish/automation/billfish-ui.cjs');
var bf = await bfm.connect();
var state = await bf.snapshot();
```

从 `state.nodes` 选择真实 ref 后 `await bf.scope(state,node.ref).call('click',{})`. 支持 `click/type/key/scroll/drag/capture/invoke`, 每次操作绑定 `snapshot_id/ref`, 只在 Billfish 自身 Qt 窗口内作用, 不依赖全局桌面截图或鼠标. 右键只发送 pointer press/release, `coverage.context_menu_event:false`, 不保证弹出右键菜单. 操作回执 accepted/queued 不是业务完成; 新 snapshot 的 `latest_action` 仅跟踪事件 dispatch, 仍需核对实际业务结果. UI snapshot/capture 完整响应最大 16 MiB; 未知结果不自动重试. 该能力不自动开放给 Eagle 插件, 不保证系统原生对话框或 OS 文件拖放覆盖.

媒体保持普通文件路径, 标注不移动或改写原始图片/视频. 当前没有 MCP server, agent 可直接使用 CLI, HTTP 或上述应用内 Node client. 库内移动/重命名, 回收/删除和评分写入仍未开放. PNG/JPG/JPEG/WebP 原文件替换使用 `items.replace_file`, GUI 选区可通过应用内 UI 输入修改后由 `items.selected` 读回. 插件开发见 [PLUGINS.md](PLUGINS.md), 在线边界与本轮导入/视频验收阶段见 [online-automation.md](../docs/online-automation.md).
