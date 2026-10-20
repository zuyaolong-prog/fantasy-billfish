# Billfish JavaScript / Eagle 插件接入

当前插件 runtime 使用 JavaScript, HTML 和独立 Electron 进程, 支持 Eagle window/service manifest 的已实现子集, 以及 Billfish 自有命令扩展. 素材查询和在线写入复用同一宿主 API, 原图与视频保持普通磁盘路径. Python 继续用于 CLI/API, catalog 和只读验证 helper; Python 插件 runner 与旧 SDK 已移除.

本轮 JS fixture 已取得真实选区, 三字段保存和重开的 GUI 结果, 本地包 cold/warm 安装与系统默认关联已实测通过. Pinterest 1.1.9 的实际相似结果与 video2gif 1.0.2 的普通目录另存也已验证. 上轮 Python 插件或本轮入口成功均不证明全部 Eagle 功能兼容, 当前结果与边界见 [online-automation.md](../docs/online-automation.md).

## 安装, 开关与依赖

1. 准备 `.eagleplugin` / `.zip` 包或含根 `manifest.json` 的开发目录. 示例目录为 `automation/examples/plugins/example.material-notes`.
2. 在 Billfish 设置的插件页选择本地包或开发目录安装, `.eagleplugin` 也可拖入主窗口. 安装异步检查和解包/复制, 不执行入口, 新插件默认关闭.
3. 开启插件并等待 `status:"ready"`. 普通 window 的 ready 仅表示进程可接受 run/open, 尚未创建页面或执行 `onPluginCreate`; 首次 run 才建窗并 create. service 和自有 commands 插件在 ready 之前创建隐藏页面并完成 create.
4. window 插件选择 `run`, service 的 `run` 触发生命周期, `open` 显示服务界面; 自有命令按声明填写 JSON 参数运行.
5. 关闭开关停止接受新任务, 清除可执行命令, 请求 `onPluginBeforeExit`, 超时终止进程. 已提交的宿主写入不撤销.

插件目录为 `%APPDATA%\Billfish\automation\plugins\<id>`, 开关独立保存在 `plugins-state.json`. 已开启插件下次启动 Billfish 时恢复会话. Electron 用户数据位于同级 `plugin-data/<id>`, 与原 Eagle 配置分开. 源目录的 manifest 不由宿主改写.

素材右键菜单在"导出"前提供带拼图图标的"插件"子菜单, 鼠标悬停展开, 复用原生导出菜单的样式. 菜单随当前库存刷新, 显示已开启且兼容的插件功能, 依赖资源包不单独展示. 按用户指定, 3D 格式扩展的全部入口均不出现在右键插件菜单, 顶栏入口和素材双击预览保持可用. 其他普通 window/service 插件可直接打开, 不根据素材扩展名猜测其业务支持范围. 自有 commands 插件打开已有参数页, 底部"插件管理..."进入管理页. 实现与实机证据见 [右键插件菜单记录](../task-memory/task-billfish-context-plugin-menu/task_state.md).

包内根目录须直接包含 `manifest.json`, 不自动递归寻找嵌套项目. 安装上限为 4096 entries, 512 MiB 总展开大小, 单文件 384 MiB, manifest 256 KiB. 不提供商店下载或自动升级. 同 ID 更新需要先停用现有 session; 使用依赖的插件仍在运行时也拒绝更新该依赖. 安装器 staging 校验后完整替换, 不把旧文件混入新版; 失败恢复旧目录与状态. 更新保留 enabled 偏好, 但状态为 `updated`, 跨重启仍等待显式激活, 不自动执行新版. 具体入口, `source_path` API 与状态语义见 [plugin-installation.md](../docs/plugin-installation.md).

宿主使用精确锁定的 Electron `38.8.6`, 默认路径为安装目录的 `tools/electron/electron.exe`, 可由 `BILLFISH_ELECTRON` 指向已准备的对应 runtime. 应将完整 Electron 运行目录和 licenses 一起交付, 仅有 `electron.exe` 不足以运行. 源码的 `automation/plugin_runtime/package-lock.json` 锁定依赖, 生产插件不会自行下载 runtime. 缺失时显示 `Pinned Electron plugin runtime is unavailable`, 不回退 Python 插件.

已有 Python 插件目录保留并显示为 `mode:"incompatible"`, `compatible:false`, 不能启用. 需要将 manifest 和入口改写为 JavaScript, 不提供自动转译或旧接口兼容层. Python CLI/API 的使用要求仍是 Python 3.11+ x64, 无 pip 依赖.

## 标准 Eagle window/service manifest

最小 window 插件:

```json
{
  "id": "my.material-helper",
  "name": "素材助手",
  "version": "1.0.0",
  "platform": "win",
  "arch": "x64",
  "main": {"url": "index.html", "width": 640, "height": 480}
}
```

`main.url` 必须指向插件目录内已存在的相对 `.html` / `.htm` 文件, 不能使用绝对路径或 `..`. `id`, `name`, `version` 必填; ID 为 1..128 个 ASCII 字母, 数字, `.`, `_`, `-`, 首字符为字母或数字. `platform` 可省略或为 `win` / `all`, `arch` 可省略或为 `x64` / `all`.

`main.serviceMode:true` 在启用时创建隐藏服务窗口. 普通 window 首次 `run` 创建并显示窗口, 后续 `run` 触发 `onPluginRun`; service 的 `run` 可继续隐藏执行, `open` 显示窗口. 宿主同时传递 `width`, `height`, `resizable`, `minWidth`, `minHeight`, `maxWidth`, `maxHeight`, `frame`, `alwaysOnTop`, `opacity`, `transparent` 的支持字段.

没有 `billfish.commands` 时, 宿主生成 `run`, service 另生成 `open`. 返回 `opened:true`, `completion:"plugin_window_opened"` 或 `plugin_service_started` 表示窗口/服务生命周期入口已触发, 不表示插件页面中的任意业务任务已完成. `plugins.list` 同时报告 `mode`, `compatible`, `can_open_window`, `open_command`, 生效权限与可运行命令.

非空 `preview` 的 thumbnail/viewer 已接入已验证的 3D 扩展, 实际部署与覆盖见下节, 不等于所有格式扩展均兼容; inspector 尚未实现. `dependencies` 接受插件 ID 数组, 由安装后的 dependency 资源包解析, 不是 npm 安装器. 当前 extraModule 只实现 FFmpeg, 详见下文. 原生 `.node` addon, 外部计算程序, 模型及所需 Electron ABI 均需逐插件验证, 不能因 manifest 可读取就宣称原包兼容.

## 3D 格式扩展 1.9.0

维护包为 `tools/plugin-packages/3d-format-extension-1.9.0.eagleplugin`, ID 为 `89fb801f-7761-4de6-b7ce-3785a084edaf`. 在设置的插件页安装并开启后, 其 window 打开设置页, 受支持模型的原生双击进入 viewer. 本轮实际 Electron production runtime 已测试 viewer 与 thumbnail 的 12 种格式: GLB, GLTF, OBJ, STL, FBX, PLY, VTK, XYZ, PCD, DAE, AMF, BVH; 包含静态 FBX 和动画 FBX/BVH. 原生集成已验证 OBJ 入库/网格封面和动画 FBX 入库/750 x 696 原生封面/双击动画预览. GLB 已真实原生导入, 新未选中 GLB 的自动封面和 OBJ/FBX 跨重启封面保持均已实测, 其状态见 [本轮记录](../task-memory/task-billfish-independent-control-3d-trim/task_state.md).

启用扩展时宿主通过原生类型动作补齐当前库缺少的声明后缀, 本次 QA 库实际新增 22 项并核验 25 个总声明. 注册类型不等于对应模型已通过全部解码路径. 缩略图由真实渲染 PNG 经 `IFileAction::slotSetFileCover` 发布为原生自定义封面, 不新增 Billfish 3D analyzer, 不将渲染尺寸写成模型 metadata. 禁用插件拒绝新的 preview, 保留已有封面.

phase5 viewer 在 DOM 与真实 Billfish owner 确认后显示 loading UI, 避免普通隐藏窗口额外等待帧/PNG 回调, thumbnail 继续隐藏. 前台请求只首次 show/focus, 后台使用 showInactive, 结束不再次夺取焦点. 窗口绑定真实 Billfish owner, 不使用全局 alwaysOnTop. 显示成功不替代 `plugin_viewer_rendered`: 后者仍等待模型 load, 双帧回调和有效 PNG. build/27 项 Node 测试/完整 plugin suite 已通过; 安装后 OBJ 请求确认完成 510.25 ms, GLB 应用内双击到 job 完成 1009.99 ms, 实际 owner 为 Billfish 主窗且 topmost:false. 这些单次实机结果与隔离 request 中位分开报告.

维护 runtime 现已加入独立本地动画控制, 覆盖播放进度, 播放/暂停, 动画切换与指定帧封面, 并提供速度, loop/once, 显式 FPS 步进与快捷键. 不修改原远程模块的许可证或激活状态. 生产实现已部署; 37 项 Node tests, 真实 Core/DOM 与窄窗高 DPI 验证已通过. Billfish 实机完成 GLB 两个不同姿态及 FBX 指定帧的原生封面更新, `native_custom_cover_verified` 与 source/metadata 保持均已读回, 正常重启后封面 SHA 和 metadata 一致. 支持范围, 部署哈希与完整证据见 [3D 动画控制 V2 记录](../task-memory/task-billfish-3d-animation-v2/task_state.md), 不据此推断 USDZ/Spline 等独立动画路径已受支持.

包的 manifest 声明 23 种模型后缀, 另有 HDR/EXR viewer, 未测后缀不能由上述结果推断通过. 所测基础路径的 HTTP(S) 尝试为 0; Spline 的可选网络行为, 设置页市场列表以及付费 animation-control 模块不在离线保证内. 未迁移 Eagle 账号, 许可证或付费模块, 未绕过购买或激活. 维护副本仅添加 `billfish.previewEngine` 并修正 viewer 的 IFC WASM 相对资源路径; Eagle 原安装的 49 个文件保持不变. 更详细的覆盖与迁移边界见 [兼容记录](../docs/eagle-plugin-compatibility.md).

## Billfish 自有命令扩展

需要可等待的结构化结果和统一任务进度时, 在原 manifest 上声明 `billfish`:

```json
{
  "id": "my.material-helper",
  "name": "素材助手",
  "version": "1.0.0",
  "main": {"url": "index.html", "width": 640, "height": 480},
  "billfish": {
    "apiVersion": 1,
    "permissions": ["items.get", "items.update"],
    "commands": [
      {
        "id": "annotate",
        "title": "填写备注",
        "input_schema": {
          "type": "object",
          "properties": {"item_id": {"type": "string"}, "note": {"type": "string"}},
          "required": ["item_id", "note"],
          "additionalProperties": false
        }
      }
    ]
  }
}
```

`billfish.apiVersion` 可省略或为 `1`. `commands` 一旦声明即使用自有命令模式, 需有 1..100 个命令, 不自动添加 window/service 的 `run/open`. 每个命令须有唯一 `id`, 非空 `title`, 对象类型 `input_schema`, ID 规则与插件 ID 相同. 入口 HTML 可引用本地 script:

```html
<!doctype html>
<html><head><meta charset="utf-8"><title>素材助手</title></head>
<body><script src="plugin.js"></script></body></html>
```

```javascript
'use strict';
eagle.onPluginCreate(() => { console.log('Plugin ready'); });

billfish.registerCommand('annotate', async ({item_id, note}) => {
  billfish.progress(0, 1, '正在填写备注');
  const item = await eagle.item.getById(item_id);
  item.annotation = note;
  await item.save();
  billfish.progress(1, 1, '备注已更新');
  return {item_id};
});

eagle.onPluginBeforeExit(() => { console.log('Plugin stopped'); });
```

每个自有命令只能注册一个 handler. 返回 object 或 `undefined` (`{}`); Promise 完成后才返回命令结果. 抛错, 不支持的返回类型或不可序列化结果会报错. 同一插件一次只运行一个命令. `billfish.progress(completed,total,message)` 只在自有命令执行期间使用, 要求整数 `0 <= completed <= total`; `total:0` 表示未知总量, 此时 `completed` 也为 0.

命令参数在调用 handler 前验证. 支持的 JSON Schema 子集为单字符串 `type` (`object`, `array`, `string`, `integer`, `number`, `boolean`, `null`), `properties`, `required`, 布尔 `additionalProperties`, 单 schema 的 `items`, `minItems`, `maxItems`, `minLength`, `maxLength`, `minimum`, `maximum`, `enum`. `title`, `description`, `default` 为说明, `default` 不自动填参. 每个 schema 要声明 type, 最大深度 16, 顶层为 object. 不支持 `$ref`, `oneOf`, `pattern` 等其他关键词, 安装时拒绝.

## SDK 与宿主权限

插件主页面获得 `window.eagle` 与 `window.billfish`. 当前支持范围:

| 接口 | 行为与边界 |
| --- | --- |
| `eagle.item.getById(id)` | 按十进制字符串 ID 获取 Item |
| `eagle.item.get({ids:[...]})` | 仅支持该精确查询形式, 最多 500 个 ID, 不支持其他 Eagle get 筛选参数 |
| `eagle.item.getSelected()` | 调用真实 `items.selected`, 返回当前原生视图选中素材, 不使用调用者伪造的 ID |
| `eagle.item.addFromURL(url,options)` / `addFromPath(path,options)` | Promise 返回原生导入确认后的真实 Item ID 字符串, 见下文支持范围 |
| `Item.save()` | 保存 `annotation`, `url`, `tags`, 见下文 |
| `Item.replaceFile(sourcePath)` | 替换库内 owned PNG/JPG/JPEG/WebP 的内容, source 必须同扩展, 等待原生分析和落盘核验, 见下文 |
| `eagle.library.info()` / `eagle.library.path` | info 返回 Billfish 实际结构; path 同步返回固定 session root, 无库抛 library_not_open |
| `eagle.plugin` | 当前插件的 `path` 和 `manifest` |
| `eagle.app` | `theme`, `isDark`, `isDarkColors()`, `platform`, `arch`, `isWindows`, `isMac`, `locale`, 独立 `userDataPath`, Promise getPath(name) |
| `eagle.window` | show/hide/close/minimize/maximize/unmaximize, get/setPosition, get/setSize, get/setOpacity, setAlwaysOnTop/isAlwaysOnTop, isVisible/isMinimized, setTitle; setReferer 为同步 void |
| `eagle.screen.getCursorScreenPoint()` | 当前光标坐标 |
| `eagle.shell` | openExternal, openPath, showItemInFolder |
| `eagle.dialog` | showMessageBox, showOpenDialog, showSaveDialog |
| `eagle.clipboard` | read/writeText, read/writeImage, read/writeBuffer, read/writeHTML, has, clear |
| `eagle.log` / `eagle.os.tmpdir()` / `eagle.isDev` | debug/info/warn/error 同步 void 写独立 profile 日志; tmpdir 同步字符串; 已安装插件 isDev=false |
| `eagle.contextMenu.open(items)` | 原生菜单与后续 click callbacks, 同步 void, 支持字段子集 |
| `eagle.extraModule.ffmpeg` | Promise isInstalled/getPaths; install 明确要求管理安装, 不假下载 |
| `billfish.call(operation,args)` | Promise 形式调用允许的宿主 operation |
| `billfish.registerCommand` / `billfish.progress` | 自有命令与宿主进度扩展, 不是 Eagle 原生 SDK |

本地 window/shell/screen/dialog 转发返回 Promise, 迁移代码应 `await` 读取返回值. 未实现的 API 返回 `unsupported_api`, 不能当作已支持的空操作. `eagle.onLibraryChanged` 当前没有单独的继续运行回调; 切库使用销毁整个旧 session 并重新初始化的语义.

`eagle.app.theme`, `isDark`, `isDarkColors()` 和 `onThemeChanged` 当前取自 Electron `nativeTheme`, 表示操作系统的深浅色状态, 尚未连接 Billfish 主界面自定义配色事件. 不应据此判断当前 Billfish 使用了哪种自定义主题.

公有 CLI/API `items.selected` 完整响应上限为 16 MiB. 插件 SDK 的完整 `host_result` envelope 上限为 4 MiB, 因而 `eagle.item.getSelected()` 可能在公有 selected 仍可读取时返回 `selection_too_large`. 超限明确报错, 不截断选区或伪装成部分成功; 插件可 catch 后提示减少选中数量, 同一 session 仍可继续处理后续调用.

Item 字段映射为 `annotation <- note`, `url <- origin`, `tags <- 原生标签名称`, `filePath <- path`, `thumbnailPath <- thumbnail_path`, `size <- file_size`. `name` 不含扩展名, `ext` 为扩展名, 另含 id/width/height. ID 必须保留为正 signed 64-bit 十进制字符串, 不转换 Number.

`fileURL` / `thumbnailURL` 使用真实路径的 file URL. 仅缓存缺失但原图片文件确实可读时, thumbnailPath 可回退原 filePath 并设置 `noThumbnail:true`, 不复制或伪造缓存. 视频或缺失原文件不作该回退. 这些 getter 的存在不代表任何新格式已能解码或生成预览.

主页面在插件 scripts 之前获得公共 `i18next` 26.5.2, 用包内 `_locales`, languages/fallbackLanguage 与当前 locale 同步初始化, 以及 Electron 真实 `nativeImage` 全局. 这补齐原包初始化和图片处理入口, 不复制 Eagle 的私有实现.

`eagle.contextMenu.open` 支持字符串 id/label/type/role/accelerator/sublabel/toolTip, 布尔 enabled/visible/checked, submenu 数组和 click 函数; 最大嵌套深度 16. click 保留在页面, 只传回 callback ID, 不将函数 JSON 化. 未知字段返回 unsupported_api. 它返回 void, 点击结果以后续 callback 交付, 不应 await 一个被选菜单结果. `window.setReferer(url)` 同步设置本插件 session 的 HTTP(S) Referer, 不改 Billfish 或其他插件的网络设置.

`app.getPath(name)` Promise 转交 Electron 的标准 OS 路径语义, userData 为独立插件 profile, exe 指向 companion, 不是 Billfish core. `eagle.log` 写入该 profile 的 `logs/plugin.log`. 上述本地工具 API 不增加新的素材写权限.

`Item.save()` 在任何写入前检查所有 dirty 字段, 修改 annotation/url/tags 以外的字段返回 `unsupported_write`, 不会悄悄忽略. tags 必须是唯一非空名称数组; 原生存在同名多标签时返回 `ambiguous_tag_name`, 不任意选择. 新标签按根标签创建, 用真实标签 ID 添加或移除关联. metadata patch, 标签创建和标签关联可形成多次宿主操作, 不是整批事务; 已执行部分随后失败时返回 `partial_update`, 带 `completed_operations` 与 `cause_code`. 已完成修改不回滚, 读回后再决定补齐.

`addFromURL` 和 `addFromPath` 按 [Eagle 官方 Item API](https://developer.eagle.cool/plugin-api/api/item) 返回 `Promise<itemId:string>`. 当前 URL 输入支持 HTTP/HTTPS, 本地输入要求绝对文件路径. URL 下载, 暂存和文件导入由宿主执行, 经 Billfish 原生导入并确认 `completion:"native_import_persisted"` 后才返回真实 ID. `analysis_ready:false` 仍表示导入已持久化, 图像尺寸和预览等分析尚未就绪; SDK 不将该状态称为分析完成.

当前导入 options 只支持 `name`, `website`, `folders`. `website` 映射为来源 `origin`; name 未传, 为 undefined 或空白字符串时省略, 由宿主使用来源文件名作为 fallback. Eagle 的 name 按人类显示标题处理: SDK 将 Windows 禁用字符和控制符转换为 `_`, 清除末尾空格和句点, 为保留设备名加 `_` 前缀, 保留内部句点和其他有效 Unicode, 并将 stem 限为 240 个 UTF-16 单元且不截断代理对. 宿主只在插件 SDK 调用中, 等实际本地文件或下载结果已知后添加真实扩展并执行最终 255 单元全名预算. SDK 不根据 URL 猜扩展. 公共 CLI/API 的 name 仍保持严格文件 basename 与实际扩展匹配约束. folders 可省略, 为 `[]`, 或包含单个真实 Billfish 原生目录 ID 的数组, 不能用多目录逻辑分类替代物理目录. 根目录 ID 为字符串 `"0"`. tags, annotation 和其他未实现选项在任何导入前返回 `unsupported_import_option`, 不会先创建素材再忽略选项. data/base64 URL 当前返回 `unsupported_import_source`.

```javascript
const itemId = await eagle.item.addFromURL(imageURL, {
  name: imageName,       // undefined 或空白时由宿主回退文件名
  website: sourcePageURL
});
const item = await eagle.item.getById(itemId);
```

SDK 要求宿主成功结果中的 item_id, item_ids 和 hydrated item.id 一致, 并具有明确的 analysis_ready 布尔值. 仅任务 accepted 或不完整的成功结果不会被发布为导入成功. 导入错误的 `error.details` 保留宿主提供的真实 item_id 和已提交状态; 错误不代表回滚, 不应自动重复提交导入. 导入后另行调用 `Item.save()` 修改备注或标签属于独立写操作.

`Item.replaceFile(sourcePath)` 调用 `items.replace_file` 并显式传 `dry_run:false`. 公共 API 默认 `dry_run:true`, 只做 preflight. 该操作仅在线可用, source 必须为独立的绝对文件路径, 目标必须是当前库实际拥有且未回收的 PNG/JPG/JPEG/WebP, 不支持 linked 素材或跨扩展替换, `.jpg` 与 `.jpeg` 也不得互换. 宿主解码检查 source 与扩展一致, 保留素材 ID, 路径, 备注, 来源和标签.

实际替换前在库内 `.bf/automation-replacements/<transaction>/` 保留原文件 backup 和 `state.json`, 再原子替换目标内容. 备份持续保留供恢复检查, 不随成功自动删除. 数据库与预览由冻结 Billfish 的原生 file monitor/分析流程维护, 不直接写运行中的数据库. 只有目标 bytes/hash/尺寸, metadata 保留和原生分析结果均读回核验后才返回 `completion:"native_replace_persisted"`, `analysis_ready:true`, `source_safe_to_delete:true`. SDK 同时核对 matching Item, 更新当前 Item 的媒体字段, 保留尚未 save 的 annotation/url/tags 修改, 最后返回 `true`.

替换后超时或部分失败会保留真实 `file_replaced`, backup/recovery 信息和 `rollback:false`, 不将失败描述为撤销. 客户端须先查原任务与实际文件, 不自动重放替换; 仅收到可删除 source 的明确结果后才清理 source. 该范围不包含 GIF, 视频或其他格式, 不因 SDK 方法存在就承诺其它插件替换流程兼容.

`billfish.permissions` 可声明以下宿主操作:

| Operation | 用途 |
| --- | --- |
| `library.info` | 查看库信息 |
| `items.search` / `items.get` | 查询素材 |
| `items.selected` | 当前真实选区 |
| `folders.list` / `tags.list` | 查询目录和原生标签 |
| `items.update` | 修改备注和来源 |
| `items.import_path` / `items.import_url` | 由宿主执行本地文件或 HTTP(S) URL 的原生导入 |
| `items.replace_file` | 同扩展 owned PNG/JPG/JPEG/WebP 内容替换及原生分析核验 |
| `tags.create` / `tags.assign` | 创建标签, 添加或移除关联 |

为接入没有 Billfish 扩展的 Eagle window/service 包, 省略 permissions 时生效为上述 12 项 allowlist; 显式 `[]` 表示不允许任何宿主 operation. 未授权调用返回 `permission_denied`. 操作 `Item.save()` 所需权限取决于实际修改: note/url 需要 items.update; tags 可能还需 tags.list/create/assign. addFromPath, addFromURL 和 replaceFile 分别需要 items.import_path, items.import_url 和 items.replace_file. 不提供任意 SQL/shell 或递归管理其他插件的宿主 operation, `items.export` 暂不在权限列表中.

## 依赖资源包与 FFmpeg

无 main 且 `preview:{}` 的资源包识别为 `mode:"dependency"`, 不独立启动 Electron, 没有 run/open 命令. 新装资源包默认 disabled, 但安装成功后即可作为消费者的依赖来源, 无需为了“运行依赖”开启一个虚假进程. 更新的资源包仍受 `updated` 显式激活约束, 未激活前不给新消费者解析, 使用旧依赖的活 session 也会阻止替换.

消费者声明如 `"dependencies":["ffmpeg"]`. 宿主按 `ffmpeg-win-x64`, `ffmpeg-win`, `ffmpeg` 顺序寻找对应 dependency, 将实际安装根授予该 session. 当前唯一实现的 extraModule 是 FFmpeg:

```javascript
if (await eagle.extraModule.ffmpeg.isInstalled()) {
  const {ffmpeg, ffprobe} = await eagle.extraModule.ffmpeg.getPaths();
  // 将真实路径交给插件自己的 child_process 转换流程.
}
```

resolver 只读取已声明并已安装的真实根, 核对 ffmpeg.exe 和 ffprobe.exe, 不搜索或复用 Eagle 私人 profile. `install()` 始终抛 `dependency_install_required`, 指引在 Billfish 插件管理中安装依赖包并重新启动消费者, 不自行联网下载或假报安装成功. `ai-sdk` / `ai-search` 等 extraModule 尚未实现, 即使相应资源目录可安装也不代表其 SDK 可用.

## 生命周期, 库与故障边界

生命周期注册为 `eagle.onPluginCreate`, `onPluginRun`, `onPluginShow`, `onPluginHide`, `onPluginBeforeExit`, `onThemeChanged`. create 在本地页面 load 后执行: 普通 window 首次 run 时触发, service/commands 在 ready 前触发. `onPluginRun` 收到 `{args:arguments}`. themeChanged 更新操作系统深浅色状态. 标准 Eagle `onPluginRun` 是生命周期入口, 业务可能继续在页面里运行, 不将其回调完成等同任意 UI 工作完成.

普通 window 插件关闭时先触发 BeforeExit, 再走正常窗口 close, 页面仍有 `onbeforeunload` 取消关闭的机会; 真正关闭后下次 run 重建页面并重新 create. service 窗口关闭时仅隐藏, 继续后台会话. 用户关闭插件开关时强制终止整个 session, BeforeExit 或 onbeforeunload 无法取消停用. 尽快释放临时资源, 不依赖退出回调作为唯一保存时机; 超时或强制结束可能阻止回调完成. 单个协议消息上限 4 MiB, 启动初始化上限 15 秒.

插件 session 从启动时绑定真实库 owner. 初始无打开库时, 不访问素材库的工具仍可使用, 库 SDK 调用返回 `library_not_open`. 之后打开/切换/关闭库会废弃整个旧 session, 对已启用插件新建与当前库绑定的 session. 旧页面, Item 对象和异步调用不会漂移到新库. 未完成命令返回错误, 已提交原生写入可能已完成, 不提供自动撤销.

独立 Electron 进程用于隔离故障和保持 Qt GUI 响应, **不是 OS sandbox**. 可信本地主页面拥有 Node 能力和当前用户的一般文件/网络权限. 主页面不可导航到插件外页面; 远程内容使用 webview 时不获得宿主 preload/SDK, 宿主调用仅接受插件主 frame. 这些限制不将本地插件变为安全执行不可信代码的环境. `.node` 或第三方依赖需独立审查其实际运行要求.

宿主不向插件分发 HTTP bearer token. 每个 session 创建随机名称的私有 named pipe, Qt `QLocalServer::UserAccessOption` 限定当前用户访问, Electron 通过 Node builtin `net` 双向传输 JSONL. stdout/stderr 仅排空, 不承载宿主 API 协议, 不保留 stdio fallback. 单个完整协议消息仍受 4 MiB 限制, 连接实际结束会关闭会话. 所有在线修改继续遵循宿主 preflight, 当前库身份, 原生 action 结果及落盘验证语义.

## 例子与验收状态

`example.material-notes` 已迁移为 HTML/JavaScript, 使用 `billfish.registerCommand('annotate',...)`. 参数:

```json
{"item_ids":["123","124"],"note_prefix":"材质参考: "}
```

示例按 ID 读文件名, 保存前缀与文件名组成的备注并逐项回报进度, 会覆盖原备注. 可先用公开 `selected` / `items.selected` 获取真实选区 ID, 再明确提交示例参数. 目前示例不自动代用户选取素材, 不联网, 不读取凭据.

接入原 Eagle 包时, 依次验证 manifest, SDK 调用, webview/preload, 实际输入与结果, 素材副作用, 切库和停用. 新 JS fixture 已完成真实 getSelected, annotation/url/tags 三字段 save 与重开, 原生属性栏即时显示结果. Pinterest 1.1.9 原包已显示约 18 张实际相似图, 真实 UI 保存入库已通过; video2gif 1.0.2 原包另存 GIF 经 ffprobe 确认有效, GIF 一键替换超出当前图片替换格式范围. AI 图片放大 1.0.9 已安装并开启, 原 UI 的 4x 对比与另存 PNG 已实测从 192 x 192 得到 768 x 768; 替换原图的真实 GUI 验收也已通过, 同 ID 的 192 x 192 PNG 更新为 768 x 768, 标签/备注/来源保持, 新原生缩略图和大图已实际打开. 两个模型在 RTX 5090 上的 RGB/RGBA 四次 engine 运行通过. 3D preview/viewer 实际支持范围另见上节; 其它 AI extraModule 和未测原生 addon 仍不在已验证范围. 原以图找图的 Bing 自动跳转仍不等同该结果页已完成视觉核验. 历史 Eagle 包静态评估见 [eagle-plugin-compatibility.md](../docs/eagle-plugin-compatibility.md), 当前实现边界以本文为准.
