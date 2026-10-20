# Compact shell native evidence

本记录服务于移除资源云库, 酷雀工具箱, 创意灵感及回收其顶部空间的本地维护改动. 不涉及素材数据修改, 不修改冻结 core 字节.

## 取证边界和来源

- 冻结目标: `E:\Billfish\Billfish.core.exe`.
- SHA-256: `47abf9bf0464d88c1ca259965bbdd00165317fbf1fea8308c339c548c405c24f`.
- REA `open_binary` 确认目标为 x86-64 PE. REA 深度分析 provider 不可用: Ghidra 未配置, IDA 未配置, Hopper 不支持此 Windows host. 没有安装或变更这些环境.
- 因此本记录采用已有 Qt5 moc 解码结果, PE import/delay-import 表, x64 `.pdata` 函数边界和 Capstone 指令解码. 证据文件为 `task-memory/task-billfish-compact-shell/native-shell-evidence.json`, 可用同目录 `extract-native-evidence.py` 离线重建. 静态 Evidence ID 为 `compact-shell-static-core-v1`.
- 唯一新增运行时读取为当前进程已存在主窗口的 232 字节 `ReadProcessMemory`, 没有调用对象方法, GUI 操作或写入进程. Evidence ID 为 `compact-shell-main-titlebar-rpm-v1`. 对象指针对应 `runtime-shell-objects.json`, 只对读取时的进程有效.
- 静态指令不证明网络请求实际发生, 也不证明任意槽实际被调用. 下述未覆盖范围明确列出.

## 网页入口和重建路径

`BFTabManager` 启动初始化函数 RVA `0x9dab0` 依次创建本地素材页和三个网页外壳:

| 功能 | QObject class / objectName | 私有状态指针偏移 | 证据 |
|---|---|---|---|
| 本地素材库 | `BfLocalResWidget` | `+0x18` | `0x9db35` 调用 `QTabWidget::addTab` |
| 资源云库 | `BFWebWidget` | `+0x20` | `0x9ddc5` 调用构造函数 `0xab530`, `0x9df17` 调用 `addTab` |
| 酷雀工具箱 | `BFNormalWebWidget` / `KuQue` | `+0x28` | `kuque.html`, 构造函数 `0x938d0`, `0x9e03e` 设置 objectName, `0x9e089` 调用 `addTab` |
| 创意灵感 | `BFNormalWebWidget` / `YuanChuangAi` | `+0x30` | `yuanchuangai.html`, 构造函数 `0x938d0`, `0x9e14e` 设置 objectName, `0x9e11f` 调用 `addTab` |

三个页面外壳在启动时存在. 不应把页面外壳创建等同于 WebEngine 浏览器或网络请求启动. `BFNormalWebWidget::slotSwitchToWeb()` 的 Qt moc 入口 `0xf2cf0` 转到 `0x93fb0`, 再跳至 `0x93fc0`; 后者通过 plugin manager 查找网页接口, 仅在 `this+0x60` 尚未创建时调用接口虚函数创建 webview 并保存. 从现有构造函数指令不能证明该路径启动时必然执行.

额外入口存在于 `BFDockAreaAddButton` 关联的菜单. 创建代码位于 `0x9e970` 函数:

- 菜单对象名复用 `BFDockAreaRefreshMenu`.
- 酷雀 QAction 的 objectName 复用 `BFDockAreaRefreshAction`, 文字为 `酷雀工具箱`. `0xa00fb` 构造连接的 slot object 入口为 `0xa18d0`; 执行时转到 `0x9d780`.
- 创意 QAction 的 objectName 复用 `BFDockAreaForceRefreshAction`, 文字为 `创意灵感`. `0xa01f7` 构造连接的 slot object 入口为 `0xa0630`; 执行时转到 `0x9ca00`.
- `0x9d780` 和 `0x9ca00` 可在状态指针为空时重新创建对应页面, 再 `addTab` / 切回页面. 因此仅隐藏 tabbar 或删除当前 tab 项不能算移除入口.

最小可靠 UI 边界应包含: 固定本地页为当前页, 移除非本地 tab 项, 移除或禁用 add-button 菜单及其上述 QAction, 清空这些 QAction 的快捷键并断开其触发连接, 禁用对应网页外壳及其出入业务连接. 页面不要立即销毁, 因为 vendor 私有状态仍持有它们的裸指针. 禁用页面不会自动断开程序化信号; 需要把相关业务 signal/slot 一并断开或在受控本地宿主入口拒绝调用.

与资源云库相关的额外业务槽在 Qt moc 中可见:

- `BFTabManager::slotCloudUpload(QSharedPointer<CloudUploadData>)`, `slotCloudImport(QSharedPointer<BFBrowserDragData>)`.
- `BfLocalResWidget::SlotMoveFolderToCloud(QVector<qint64>)`, `SlotMoveFilesToCloud(QVector<qint64>)`, `SlotMoveToCloudWithUploadData(QSharedPointer<CloudUploadData>)`, `slotCloudImport(QSharedPointer<BFBrowserDragData>)`, `slotCloudImportByRightMenuClick(QSharedPointer<BFBrowserDragData>)`, `InitCloudMenu()`.
- `BFWebWidget` 的 `signalCloudUpload`, `signalCloudImport`.
- 已有运行时元数据还包括素材视图的 `slotShortCutSendToCloud()` 和 `SlotMoveFilesToCloud()` 等. 云任务/传输页还存在 `BillfishApp::BFCloudView`, `BFCloudManager`, 与 `bfcloudclient.dll` 相关. 它们不能从顶部 tab 消失推断为已停止.

快捷键实际值, 动态右键菜单, 用户已登录时的云请求, 启动后的后台请求未在本静态任务中运行验证. 应由集成运行时检查确认隐藏菜单与快捷键不可达, 并独立决定云传输业务与资源云库网页的移除边界.

## 布局与生命周期约束

`DraggableTabWidget` vtable 位于 RVA `0x134f20`. `event` 槽 `+0x20` 指向 `QTabWidget::event`, `resizeEvent` 槽 `+0xe8` 指向 `QTabWidget::resizeEvent` thunk `0xf9c88`. 没有 vendor 自定义 `resizeEvent` 读取原 corner 指针的证据.

但 native 业务 lambda 存在明确非空假设:

- `0xa1bd0` 沿本地页的 `parent()->parent()` 转为 `QTabWidget`, 隐藏 tabbar 和两角控件.
- `0xa0da0` 通过相同父链显示两角控件和 tabbar.
- `0xa0df1`, `0xa0e32`, `0xa1c5d`, `0xa1c9e` 对 `cornerWidget(0)` 和 `cornerWidget(1)` 结果直接取 vtable 调用 `setVisible`, 没有 null 检查.
- 因此保留 `BfLocalResWidget -> qt_tabwidget_stackedwidget -> DraggableTabWidget` 父链. 两角不可设置为 null; 如迁移原角控件, 需保留非空占位控件. 更小的布局改动是保留两角原位, 隐藏 tabbar, 把现有导航栏置于原 tabbar 的中间区域.
- 这些 lambda 会重新显示 tabbar, 所以仅启动时 `hide()` 一次不足以约束沉浸模式来回切换后的布局. 应在自身组件事件处理内维持已移除导航状态.

## CFramelessWindow 原生拖动与交互

`CFramelessWindow::nativeEvent` 函数为 RVA `0xb4ad0`. Windows `WM_NCHITTEST` 分支从 `this+0x30` 标题控件读取 `childAt(mapFromGlobal(cursor))`. 顶部区域的命中处理依赖对象名称与注册对象, 不仅是 Qt 鼠标事件.

实际只读快照 (PID `73384`, main key `1b204a2b690`):

- `this+0x30` 为 `0x1b204366c60`, 对应 `DraggableTabBar`, 不是整个 `DraggableTabWidget`.
- 原 tabbar 几何为 `(205, 0, 688, 42)`.
- `+0x38`, `+0x40`, `+0x48`, `+0x50`, `+0x58`, `+0x60`, `+0x68` 在该快照中均为 null.
- 标题高度字段 `+0x7c` 为 `40`.

若导航栏变为 tabs 的直属子控件, 它不在原 tabbar 子树. 旧标题控件的 `childAt` 返回 null 后 nativeEvent 会走整个窗口的 `childAt`, 未注册的新控件可能在 `0xb56e9` 被判为 `HTCAPTION = 2`, 导致按钮/搜索变成拖动区. 不能仅假设 reparent 后普通 Qt 事件会收到点击.

已有真实 setter 位于 RVA `0xb5f40`, 调用约定为 x64 `void(CFramelessWindow* this, QWidget* titlebar)` 的静态推定. 它写入 `this+0x30`, 并在非空时连接 `destroyed(QObject*)` 至 `onTitleBarDestroyed()`. 该函数没有在本任务中执行. 集成代码若使用它应绑定上述 core SHA-256 和当前模块基址, 不保存历史运行时地址. 将标题控件改为包含导航栏的 `DraggableTabWidget` 或专用 header, 才使 native 的第一轮 `childAt` 正确覆盖新交互控件.

当第一轮标题 `childAt` 成功时, 名称规则包括:

- `spacerWidget`, `lab_view_name`, `wid_search` 可进入拖动区.
- `m_search_menu`, `m_search_wid` 保持 client.
- 原窗口按钮名称有单独的 client 保护.
- 其他交互 leaf 控件默认走 client; 仍需真实 GUI 鼠标点击与 Windows hit-test 验证.

## 交付与未覆盖项

离线提取脚本运行成功, 产出 15 个聚焦函数及 tab-widget vtable 证据. 只新增本记录和任务目录内取证脚本/JSON. 没有修改 production source, vendor binary, 素材库或 GUI 状态.

待集成验证: 实际按钮和搜索输入, 标题空白拖动/双击最大化, 缩放/最小化恢复, 沉浸模式切换, 原云相关快捷键和动态菜单, 受控宿主对已移除页面槽的访问策略. 静态元数据列出可达候选, 不等于完成这些运行时验收.

## 补充: QMenu actions, 主菜单云入口与账户控件

独立证据文件 `native-shell-entry-evidence.json` 的 Evidence ID 为 `compact-shell-entry-actions-v1`, 可通过 `extract-entry-evidence.py` 重建. 范围为上述冻结 core, `ui_plugins/viewmodule.dll`, `bfglobal.dll` 的聚焦指令切片; 另 `native-global-shell-meta.json` 保存 bfglobal 的相关 Qt moc 元数据.

Root 的真实 GUI 检查发现主汉堡菜单和文件右键菜单中的两个额外云入口. 静态指令解释了为什么只枚举 QObject children 会漏掉它们: QAction 构造时 parent 为 null, 通过 `QWidget::addAction` 或 `QMenu::addAction` 建立关联. 后续诊断和禁用必须检查 `QWidget::actions()` / `QMenu::actions()`.

- 主菜单云项: core `0xa63d3` 开始构造 `QAction(tr("Open Cloud Library"), nullptr)`, 保存至 `BFTabManager+0x48 -> +0x28`. 未观察到该 QAction 的 objectName 设置. `triggered` 连向 lambda `0xa4c00`. 执行时取当前 tabs, 若 `count()==1` 则调用 `0x9e660` 重新打开云页, 否则 `setCurrentIndex(1)`. 应对已知主菜单中该 action 移除/隐藏、禁用、清空快捷键并断开触发连接. 无需关闭整个打开资源库子菜单.
- 文件菜单云发送项: `BFFileMenu` 对应 `ui_plugins/viewmodule.dll` 的构造函数 `0x2e570`. `0x2e8c4` 创建 `QAction(tr("send to cloud(U)"), nullptr)`, 指针保存至 `BFFileMenu+0x68 -> +0xf8`. `0x2e980` 加入菜单 actions. `0x2e96c` 设置 `Qt::WidgetWithChildrenShortcut = 3`, shortcut config type 为 `0x40006`. moc 入口 `0x41750`, local index 20 的 `SlotMoveToCloud()` 转到 `0xf430`, 该函数记录操作后跳至 `SigMoveToCloud` 的 signal 函数 `0x40fd0`.
- `bfconfproxy.dll` 的离线配置字符串确认 `SendCloudLibrary` (RVA `0x20190`) 的默认快捷键为 `Ctrl+Shift+U` (RVA `0x201a8`). 应优先按已知云 action / shortcut type 禁用, 避免全局删除用户其他动作上相同的自定义键.
- `BFFolderMenu`, `BFTreeFolderMenu` 均在 moc 元数据中存在 `SlotMoveToCloud()` / `SigMoveToCloud()`. `BFTreeFolderMenu` 创建项原文为中文 `发送至资源云库 (U)` (treemodule RVA `0x43528`, 库后有空格). 本次没有确认 BFFolderMenu 独立的 action 字面常量, 不把猜测列为事实. 可在已知菜单实例的 actions 中精确识别当前显示项.
- 右侧 `BFDockAreaRefreshButton` 的 clicked lambda 为 core `0xa0cc0`, 转到当前 `BFWebWidget` 的 `0xabc40`, 检查网页接口状态后调用 webview 刷新. 这是云页面控件, 应随云功能禁用/隐藏.
- 右侧 `BFDockAreaHeadButton` 继承 `BFAvatarWidget`. `signAvatarClick()` 连向 core lambda `0xa0fd0`, 它弹出 `BFTabManager+0x48 -> +0x138` 的账户菜单. 它不是直接打开资源云库的动作. 如保留账户能力, 应仅移除账户菜单中的具体云入口, 不应以 class 名称含 Avatar 为由删除整个账户业务.
- `BFAvatarWidget::setCloudMovieLabShow(bool)` (bfglobal RVA `0x2a450`) 只切换 `this+0x38` 的子标签可见性, 同时 start/stop `this+0x40` 的 movie, 不是业务上传/云页导航. 将其设为 false 可移除云活动标记; 禁用图标不能替代禁用云动作.
- 字符串 `merge_button` 对应 `BFLibraryInfoWidget` 的本地资源库操作. clicked lambda `0x70520` 按状态发 `signalMergeLibrary(QString)` 或 `signalReselectLibrary(QString)`. 不属于这三个功能, 不应按 merge 关键词禁用.

主菜单精确原文用于中英语言切换识别: 创建资源库的源字面量就是中文 `创建资源库` (core `0x133338`), 子项为 `Create Empty Library` / `Create Library From Import`; 打开资源库为 `Open Library`, 云子项 `Open Cloud Library`, 本地子项 `Open Local Library`; 退出为 `Exit`. PreferencesShortcut 没有现成的直接云行标签证据, 推荐以 `SendCloudLibrary` / `0x40006` 的配置身份识别该项, 而非猜测英文行文字.

标题指针补充限制: `nativeEvent` 第一轮标题 `childAt` 前没有限制标题控件自身高度. 若标题指针由 tabbar 改为整个 tabs, `spacerWidget`, `lab_view_name`, `wid_search` 的 caption 规则会扩展到素材区域相同名字的后代. `this+0x7c` 的标题高度仅在后续 fallback 分支生效. 使用全高 tabs 作为标题时必须验收素材点击、框选、拖动; 专用 42px header 可缩小该原生名称规则的适用范围.
