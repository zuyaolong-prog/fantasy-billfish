# 本地素材工作区

当前维护版移除资源云库, 酷雀工具箱和创意灵感的页面入口及关联命令. 本地素材导航, 搜索, 缩略图大小和视图控制复用原生控件, 移到原标签页所在的顶部区域. 原素材工具栏一行不再占用内容区. 左上角的 Billfish 图标和名称, 右上角头像及已观察到的主菜单, 账户菜单和托盘登录入口均已移除.

当前顶部采用 48 px 高的 Eagle 风格工具栏. 左侧包含主菜单, 新增/导入和侧栏控制; 中间保留当前目录, 前进/后退, 缩略图缩放, 插件, 布局/排序, 筛选和常驻搜索; 右侧提供置顶和原生窗口按钮. 左右区域跟随实际侧栏宽度, 侧栏关闭时按按钮所需的最小宽度收紧. 导入历史保留在主菜单的打开资源库之后, 复用原生动作和 `Alt+L` 快捷键.

## 运行方式

继续通过 `E:\Billfish\Billfish.exe` 启动. 紧凑布局随现有 `ui_plugins/preview.dll` 维护代理加载, 布局本身不增加独立进程或服务. 已启用的 JavaScript 插件使用独立 Electron 进程. 冻结的 `Billfish.core.exe` 不修改.

`CompactShell` 保留本地页面的原生父链和窗口两角控件, 维护原生拖动命中区域. 原标签栏保留零宽度的标题行布局约束, 页面只保留本地素材页. 移动的导航栏随原素材视图销毁, 并在视图重建时重新绑定.

`LegacyFeatureGate` 按已取证的页面类型, 菜单上下文及具体命令封锁已移除功能. 文件, 文件夹, 文件树和打开资源库菜单中的云入口隐藏并禁用, 关联快捷键清空. 旧页面对象保留以满足冻结 core 的裸指针和生命周期约束, 其云业务连接被断开, `destroyed` 清理连接保留. 页面和菜单构造, 显示及动态动作变化触发维护, 没有周期全树扫描.

该改动不修改素材文件和素材库记录. 本地合库, 左右侧栏控制, 导入历史, 在线 API, 本地插件及已安装的 DPI/HDR/AV1/VideoTrim 能力保留. 不以页面移除推断所有 vendor 网络活动已经停止; 没有删除共享 DLL 或终止通用后台线程.

标题区域继续使用原生角控件与父链, 只收紧已有布局并隐藏指定控件. 导入历史复用同一个 `QAction`, 保留其原 owner, handler 和主窗口快捷键关联. 登录动作隐藏, 禁用并清除快捷键; 已确认的托盘登录按钮还断开其业务信号. 未删除冻结 core 仍持有指针的对象.

## 维护依据

- 原生边界: [compact-shell-native-evidence.md](compact-shell-native-evidence.md).
- 实现: `_video-trim-patch/src/shell/compact_shell.*` 和 `legacy_feature_gate.*`.
- 安装前快照及运行验收: `task-memory/task-billfish-compact-shell/`.
- 标题区域清理的安装前快照及运行验收: `task-memory/task-billfish-clean-header/`.
- 部署遵循现有 preview 代理的 restore, prepare, install 事务, 并同步统一启动器的 portable runtime 指纹.

最终 Release 构建成功, 7 组 CTest 通过. 实机发现并补齐两个 preview 云动作后, 增量构建和对应 gate 测试也通过. 正常重启和切库后的只读检查确认 10 个已观察云/网页命令均隐藏, 禁用且无快捷键, 三个旧页面不可交互. 搜索, 素材框选, 图片预览和沉浸切换, 标题拖动/双击最大化, 窗口尺寸调整, 插件设置均完成 GUI 验收. 原素材库和 1160 x 700 窗口尺寸已恢复.

随后标题区域清理的 Release 增量构建和受影响的 2 组 CTest 通过. 实机验证主菜单和 `Alt+L` 均打开同一个原生导入历史窗口; 侧栏切换, 拖动, 最大化/恢复, 最小化, 关闭到托盘及沉浸预览返回均正常. 13 项最终运行断言通过, unified status 为 `Healthy=true`. 原素材库 20,779 条记录, 搜索状态和 1160 x 700 窗口尺寸已恢复.

以上结果属于此前的紧凑布局和标题清理阶段. 未执行账户登录或已登录云会话测试.

## Eagle 风格工具栏与 JavaScript 插件

本轮对齐布局, 排序, 筛选, 搜索和窗口交互. 动作组合, 库内以图搜图以及快速跳转等新业务不属于本轮. 工具栏实现位于 `eagle_toolbar.*`, 业务与原生边界见 [Eagle 参考证据](eagle-toolbar-evidence.md) 和 [Billfish 原生取证](billfish-eagle-toolbar-native-evidence.md).

布局和排序面板复用原生 QAction, 保留 enabled/checked 语义. 左右栏使用实机取证确认有效的 Dock actions. 筛选按钮控制当前条件条, 不清除筛选值; 当前用户 `Billfish/maintained-shell.ini` 的唯一 `filter-visible` 设置只由用户切换更新, 临时页面隐藏不写入偏好. 点击当前目录标题会清除搜索及筛选条件并刷新当前上下文. `P` 打开插件菜单, `Shift+T` 切换窗口置顶, `Alt+1..4` 切换布局; 文本编辑焦点保留输入行为.

JavaScript/Eagle 插件运行时已实现并安装, 当前能力和限制见 [插件开发说明](../automation/PLUGINS.md) 与 [兼容性报告](eagle-plugin-compatibility.md). 它提供有限的真实 Billfish SDK 桥接, 不代表 Eagle SDK 全量兼容. 本轮部署和 GUI 复验记录在 `task-memory/task-billfish-eagle-parity/`.

## 当前工具栏 GUI 验收

caption 修复已通过现有 restore/prepare/install 事务部署. 当前 preview DLL SHA-256 为 `68A02EDFC816493C7903BA4F99869FA28BD9BF6A6D20CC7FC8E36A2F7716223E`, 1,056,768 bytes, 事务 ID `b9fbe52e-4a81-43b2-99aa-9479c41dbb22`. 根 launcher 未随此 caption 修复改变. 构建成功, 受影响的 toolbar CTest 1/1 通过.

| 实际 GUI 项目 | 结果 |
| --- | --- |
| 四种布局 | `Alt+1..4` 切换四种真实布局, 每种选中测试素材后, 公有 selected 均回读正确 id 40 |
| 布局/排序弹层 | 背景不透明, 内容清晰, 升序/降序可用 |
| 侧栏 | 左右可独立开关, 整体收起可用, 收起后工具栏与窗口按钮仍可操作 |
| 插件入口 | `P` 打开已安装插件菜单 |
| 文本输入 | 搜索框中的 `P` 和 `Shift+T` 保持普通文本输入, 不触发全局动作 |
| caption | 点击清除当前搜索; 修复后按可用宽度恢复目录/搜索标题显示 |
| 置顶 | pin 按钮与素材焦点下的 `Shift+T` 均验证真实 Win32 topmost 状态变化 |
| 窗口交互 | 标题拖动, 双击最大化/恢复, 最小化, 关闭到托盘, resize 缩小均通过 |
| 沉浸往返 | `Space` 进入沉浸预览, `Esc` 恢复后工具栏仍可用 |

caption 预算包含最多 144 px 的最小宽度目标, 受当前可用宽度限制, 在 nativeResize 后恢复原生文本布局. 长搜索标题仍会按实际可用空间省略, 不承诺任意窗口大小都显示全部文字.

四种布局的 selected 结果位于 `task-memory/task-billfish-eagle-parity/final-layout1-selected.json` 至 `final-layout4-selected.json`. 实际布局截图及 `toolbar-*.png`, `search-caption-fixed.png`, `installed-plugins-menu.png` 位于本地 `task-memory/task-billfish-plugin-packages/`. 上述证据是实际窗口行为, 不以 QAction triggered 或 checked 值代替功能成功.

最终环境已恢复: 日常库仍为 20,779 项, 原目录与选中素材已恢复, 无搜索, 两侧栏与筛选条显示, 五列瀑布流. 窗口恢复为 1160 x 700, 截图边框范围为 1160 x 699 的差异已保留在证据中. `final-personal-restored.png` 记录实际恢复结果; 最终 unified status 六项均健康, effective association 仍指向根 launcher, 只有一个 core 进程.
