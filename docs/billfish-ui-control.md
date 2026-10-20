# Billfish 应用内 UI 控制

`automation/billfish-ui.cjs` 提供 Billfish 自身窗口的结构化 snapshot, 控件引用, 局部鼠标/键盘输入和控件截图. AI 可以在 Node REPL 中加载它, 不需要全局 Computer Use 截图, 桌面鼠标或 `SendInput`. 先通过唯一入口 `Billfish.exe` 启动软件. 该接口依赖当前运行的 Billfish host, 不启动 GUI, 不创建新的 MCP server, 不修改 Codex 配置.

```js
var bfrequire = (await import('node:module')).createRequire('E:/Billfish/automation/billfish-ui.cjs');
var bfm = bfrequire('E:/Billfish/automation/billfish-ui.cjs');
var bf = await bfm.connect();
var state = await bf.snapshot();
console.log(state);
// 从真实 snapshot 中选择目标, 不猜 ref.
const node = state.nodes.find(node => node.ref === '从上一步获得的 ref');
const receipt = await bf.scope(state, node.ref).call('click', {});
console.log(receipt);
const after = await bf.snapshot();
console.log(after.latest_action, after.nodes);
```

上例使用 `createRequire` 加载 CommonJS client, 适用于当前 Codex Node REPL; 不要直接 `import('file:///.../billfish-ui.cjs')`, 该 REPL 的文件导入只接受 `.js/.mjs`. 普通 CommonJS Node 环境也可直接 `require('E:/Billfish/automation/billfish-ui.cjs')`.

`connect()` 默认安装根目录为 client 文件的上级目录, 移盘后无需改写内部路径. 把示例中两处 client 路径改成实际位置即可. 可显式传入 `{installRoot:'F:/Billfish',timeout:30000}`; timeout 单位为毫秒. client 从 `%LOCALAPPDATA%/Billfish/automation/host.json` 发现当前用户的 host, 验证 protocol, PID, pipe 名和 install_root, 再以 `host.info` 确认连接. 该 client 仅允许 `host.info` 和下面的 `ui.*` 操作. 操作使用现有当前用户 named pipe, 无 npm 依赖. Billfish 重启后重新 `connect()`.

`bf.snapshot({max_nodes:1000,max_items:100,include_hidden:false})` 返回原生 Qt 控件/项目/action 引用. 上限分别为 4000 和 500. `bounds` 使用窗口局部坐标; `local_bounds` 使用目标控件局部坐标. 先读取结构和可见/可用状态, 选择实际 `ref`, 再创建 `bf.scope(state,ref)`. scope 保留本 client 获得的 snapshot 身份, 自动填写每次操作的 `snapshot_id` 和 `ref`, 禁止参数覆盖它们. 原生 host 仍会检查引用是否过期, 目标是否存在和坐标是否位于控件内.

| `scope.call(action, arguments)` | 参数 |
| --- | --- |
| `click` | 可选 `x,y` 必须成对, 相对目标控件; 默认中心. `button` 为 `left/right/middle`, `count` 为 1 或 2, `modifiers` 为 `Ctrl/Shift/Alt` 数组. 右键仅发送 pointer press/release; snapshot 的 `coverage.context_menu_event:false`, 不发送 ContextMenuEvent, 不保证弹出右键菜单. |
| `type` | `text`, 可选 `replace`, 默认 true; 单次文字上限 65536 字符. |
| `key` | `key`, Qt PortableText 单组按键, 如 `Enter`, `Escape`, `Ctrl+A`. |
| `scroll` | `delta_x,delta_y`, 范围 -12000..12000, 至少一轴非零; 120 为一格. 可选成对 `x,y`. |
| `drag` | `from:{x,y}`, `to:{x,y}`, 均为同一目标 widget 的局部坐标; `steps` 为 2..64, 默认 12; 可选 `modifiers`. model item 不支持 drag, 需要使用对应 view viewport 的 widget ref. |
| `capture` | 无额外参数, 返回目标 QWidget 的 `{mime_type:'image/png',base64,width,height,device_pixel_ratio,snapshot_id,ref}`. |
| `invoke` | 无额外参数, 使用 snapshot 中实际 QAction 的 ref. |

也可直接 `bf.call('ui.click',{snapshot_id:state.snapshot_id,ref:node.ref})`, 或通过 CLI/HTTP 调用相同 operation. 真实参数契约见 `billfish-cli.cmd schema` 和 [openapi.json](../automation/openapi.json), 额外参数会被拒绝. 这些能力不自动授予 Eagle 插件.

截图仅取目标控件, 在提供 `nodeRepl.emitImage` 的 Node REPL 中可显示:

```js
const image = await bf.scope(state, node.ref).call('capture');
await nodeRepl.emitImage({bytes:Buffer.from(image.base64,'base64'),mimeType:image.mime_type});
```

snapshot 和 capture 的完整响应上限为 16 MiB, 超限返回错误而不截断. scope 中的 snapshot 必须来自当前 client. 每次输入后读取新 snapshot, 对照 `latest_action.request_id` 与回执判断是否已 dispatch, 再检查实际界面或业务结果. `{accepted:true,queued:true}` 只代表排队; `latest_action.state:'dispatched'` 只代表已发送 Qt 事件, 不证明视频导出, 文件写入等业务已经完成. 遇到失败, 超时或连接断开, 先读回状态再决定下一步, client 不会自动重发输入.

覆盖范围为 Billfish 进程内可发现的 Qt widgets, model items 和 QActions. 插件的独立 Electron 窗口, 系统原生文件对话框和 OS 文件拖放不保证可通过这个接口控制. 不把应用内 drag 视作系统 drag/drop, 不把隐藏节点列表视为所有目标均可交互.
