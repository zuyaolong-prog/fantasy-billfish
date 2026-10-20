# Eagle-style toolbar native evidence

本记录仅用于组合 Billfish 现有布局、排序、信息显示和左右栏动作, 以及对齐新顶栏宽度. 取证没有执行任意 QObject 方法、修改 vendor binary 或操作 GUI.

静态目标为冻结 `ui_plugins/toolmodule.dll`, SHA-256 `dd1018659189f191ea9203d3dca4e3b0ffde3064b291f4e78635f38fdb7d1923`. 已有 Qt moc 元数据来自 `task-memory/task-billfish-cli-api/native-qt-toolmodule.json`; 本轮聚焦指令记录为 `task-memory/task-billfish-eagle-toolbar/native-toolbar-code.json`, Evidence ID `billfish-eagle-toolbar-toolmodule-v1`. 可运行同目录 `extract-toolbar-evidence.py` 重建. 已知 native provider 未配置, 本轮使用 PE/import/delay-import 和 Capstone 离线解码, 没有重试安装分析器.

运行时证据为当前 Billfish 在线宿主 `diagnostics.objects`, 仅使用 `include_values` 读取现有对象. 快照保存于同任务目录 `native-toolbar-runtime.json`, `native-toolbar-qwidgets.json`, `native-toolbar-sides.json` 和 `native-toolbar-widgets.json`. 指针仅用于本次快照交叉对应, 不能跨重启持久化使用.

## 现有菜单与动作

| 用途 | 精确 QObject class | objectName | 当前菜单 action 内容 |
|---|---|---|---|
| 布局 | `ToolModule::BFNavLayoutMenu` | 空 | 网格, 瀑布流, 自适应, 列表, separator |
| 排序 | `ToolModule::BFNavSortMenu` | 空 | 升序, 降序, separator, 导入时间, 创建时间, 修改时间, 名称, 类型, 文件大小, 尺寸, 评分, 时长 |
| 信息显示 | `ToolModule::BFNavInfoMenu` | 空 | 名称, 类型, separator, 文件大小, 尺寸, 时长, separator, 显示隐藏文件/文件夹 |
| 侧栏和筛选栏 | `ToolModule::BFNavSideMenu` | `BFNavSideMenu` | `Left column`, `Right column`, `Filter bar` |

运行时所有上述 QAction 均以原菜单为 QObject parent, objectName 为空. 组合面板可以复用原动作及动作组, 保留原 QObject parent 和连接. 按 class 找到原菜单, 再从 `menu->actions()` 读取动作; 不能仅按菜单 objectName 找布局、排序、信息菜单.

## Show 方法与状态刷新

冻结实现不是简单 popup 转发. 以下方法在计算位置、activate、raise 之后, 先调用原菜单 vtable `+0x168` 的刷新函数, 再调用 `QMenu::popup`, 没有调用 `exec`:

| Qt 槽 | 函数 RVA | NavigationBar 中的菜单指针 | 调用的刷新函数 |
|---|---|---|---|
| `ShowArrangeInfoMenu()` | `0xb280` | `this+0x88` | `BFNavInfoMenu`, `0x76a0` |
| `ShowLayoutMenu()` | `0xb320` | `this+0x98` | `BFNavLayoutMenu`, `0x78a0` |
| `ShowSortMenu()` | `0xb3c0` | `this+0x80` | `BFNavSortMenu`, `0x79e0` |

刷新函数的静态调用约定为 x64 `void(QMenu* this)`, `rcx` 是原菜单对象, 未依赖额外调用参数. 它们还调用菜单虚函数 `+0x170`, 并从 `getConfProxyUser` 读取当前设置:

- Info `0x76a0`: 更新各显示字段和隐藏文件动作的 checked, 同步 `this+0xa8...+0xaf` 的内部缓存. 按当前 layout=3 禁用部分字段, 所以不能只复制 checked 状态而忽略 enabled.
- Layout `0x78a0`: 当前 layout 0/1/2/3 映射原四动作并设置 checked.
- Sort `0x79e0`: 读取当前排序枚举, 更新字段和升降序 checked, 同步 `this+0xb0` 的内部排序缓存.
- Side `0x7940`: 读取左右栏及筛选栏状态, 更新三个动作 checked, 并同步 `this+0x58/+0x59/+0x5a` 缓存.

组合面板每次显示前应刷新原菜单状态, 再使用原 QAction, 避免首次打开或其他入口修改状态后出现过期的 checked/enabled/cache. 仅触发 `aboutToShow` 没有等价性的静态证据, 因为上述 Show 函数显式调用自定义刷新槽后才 popup. 不应调用 Show 函数再临时关闭 popup 来代替刷新.

用于 guard 的 24 字节 prologue:

```text
Info 0x76a0:
48 89 5c 24 08 57 48 83 ec 20 48 8b 01 48 8b f9 ff 90 70 01 00 00 ff 15
Layout 0x78a0:
40 53 48 83 ec 20 48 8b 01 48 8b d9 ff 90 70 01 00 00 ff 15 c8 57 04 00
Sort 0x79e0:
40 53 48 83 ec 20 48 8b 01 48 8b d9 ff 90 70 01 00 00 ff 15 88 56 04 00
Side 0x7940:
48 89 5c 24 08 48 89 74 24 10 48 89 7c 24 18 41 56 48 83 ec 20 48 8b 01
```

如使用直接绑定, 必须限定上述冻结模块 hash 与当前模块基址, 不复用快照绝对地址. 这些函数的调用执行属于集成实现, 本取证任务没有运行它们.

## 筛选栏与侧栏业务边界

Qt moc 可见以下精确槽, 但其中部分冻结实现为空:

- `BFNavigationBar::on_btn_show_filter_clicked()`, `ShowSideInfoMenu()`, `ShowScaledInfoMenu()` 在 metacall 中均转到 RVA `0x7930`, 其指令为 `ret 0`. 不能把这些名字当作有效操作.
- 筛选候选动作是 `ToolModule::BFNavSideMenu` 的第三个原 QAction `Filter bar`, 对应 `slotFilterActionClicked()`. 该槽经 `0x82f0` 转到 `0x3d9f0`, 发 `sigFilterWinStateChanged()`. 后续定向代码确认该信号仅连接到导航栏 lambda `0xebf0`, 再发 `sigChangeFilterState(true,false)`. 这不证明有接收者会显示/隐藏 filter widget. 主任务后续真实 GUI 已证明该动作只改变 checked, 没有切换实际 filter 可见性, 应视为不可用的现有业务入口.
- 左侧动作槽 `slotLeftActionClicked()` 在 `0x83a0` 读取原 left QAction 的 `isChecked()`, 写入 ConfProxyUser 对应设置, 再发 `sigLeftWinStateChanged()`.
- 右侧槽 `slotRightActionClicked()` 在 `0x83f0` 同样读取原 right QAction checked, 写配置并发相关业务通知.
- 顶部 native actions 另有 `BFDockAreaHideLeftAction`, `BFDockAreaHideRightAction`, `BFDockAreaHideAllAction`. 本轮未追踪 HideAll 的全部 toggle 规则. 需要组合行为时可先读取真实左右 pane 的 visible, 再有条件触发原 Left/Right 动作, 不猜测 HideAll 的方向.

Filter 业务最终显隐以及信号连接后的结果需要主任务的 GUI 验收. 本轮只确认原动作和 native 信号/配置边界, 未调用任何 toggle.

### 后续真实 GUI 失败后的修正

Root 的真实 GUI 验收揭示 BFNavSideMenu 的 left/right/filter 三个 action 均只改变动作 checked, 未改变对应实际 widget. 因此上述槽的信号与配置写入不能作为原业务已连通的证据. 已把这一边界直接同步给 UI 实施者, 不再复用 BFNavSideMenu 作为最终 toggle 接口.

真正工作的 sidebar 入口是 `BFDockAreaSideMenu` 中命名为 `BFDockAreaHideLeftAction`, `BFDockAreaHideRightAction`, `BFDockAreaHideAllAction` 的原动作. core `0x9e970` 构造并建立以下实际业务链:

| Dock action | manager 私有原 global action | 最终 lambda | local page 方法 | BFItemIconView 实现 |
|---|---|---|---|---|
| HideLeft | `manager+0x48 -> +0x128` | `0xa58c0` | `0x88eb0` | `0x6e9a0` |
| HideRight | `manager+0x48 -> +0x130` | `0xa42f0` | `0x88ec0` | `0x6ec90` |
| HideAll | `manager+0x48 -> +0x120` | `0xa5380` | `0x88ed0` | `0x6ef70` |

Dock triggered 经 slot object `0xa1e60` 把 bool 转发给原 global action 的 triggered signal. 三个最终业务 lambda 都忽略该 bool, 只读取捕获 manager 的当前 local page 并调用真正 toggle. 因此将原 Dock action 设为 checkable 并用实际 pane visible 同步 checked, 不会改变上述冻结业务路径的 toggle 方向. 完成后仍应回读 pane 的真实状态, 不以 checked 作为成功条件. 当前原 Dock actions 的只读快照位于 `native-live-dock-toggle-actions.json`.

没有发现可安全接入的 filter toggle Qt slot. core BFItemIconView 的已知实现会在切 provider 时直接 `setVisible(true/false)` 当前 `IFilterWidget` (`BFItemIconView+0x98 -> +0x50`), 并不把 BFNavSideMenu checked 作为权威. 最小本地修复可直接改变当前唯一 `CConditionFilterWidget` 的可见性并按 Show/Hide 回读, 使用有实际语义的自有 toolbar action, 不再转发失效 SideMenu signal.

已有持久化字段的边界也已定向确认: `bfconfproxy.dll` SHA-256 `d5dc736f215538b2096d06626859b665e826b6687c12f6378bfb289848e2d713`, `CBFConfProxyUser` vtable RVA `0x20500` 的 `+0x178` getter `0xe880` 读取 `this+0x15`, `+0x180` setter `0xe890` 仅写入该 byte 并返回 true. 加载函数 `0x101a0` 内 `0x1256f` 将 `QSettings.value("ShowFilter").toBool()` 保存到该字段; 保存函数 `0x17ab0` 内 `0x189e2` 读取字段, `0x18a1a` 调用 `QSettings::setValue("ShowFilter", ...)`.

该字段存在并不等于现有 UI 会遵循它. 单独调用 setter 不能保证切 provider 或重启后的实际 filter visibility. 本轮最小 toggle 修复可明确不新增持久化, 如要求跨重启保留则需另验证其保存、恢复和 provider 切换边界, 不能凭配置字段存在宣称已满足.

## 左右栏实际身份与顶栏宽度

当前在线主库的实际对象关系:

```text
BFItemIconView (key 2c77f1ae570)
  BFItemIconSplitter (key 2c77f1de3f0)
    first pane: QWidget, objectName empty, key 2c77f1de1f0
    center pane: material view area
    last pane: QWidget, objectName empty, key 2c70e48f7a0
```

直接 pane 的运行时状态:

| Pane | visible | geometry (x,y,w,h) | minimum/maximum width |
|---|---|---|---|
| Left | true | `(0,0,208,658)` | 208 / 308 |
| Right | true | `(957,0,203,658)` | 203 / 320 |

原主 `LeftMainTitleBar` 当前仍是 `(0,0,44,42)`, fixed width 44. 因此把 header 左区与左 sidebar 对齐时, 需要从主 `BFItemIconView` 的直属 splitter 读取 `widget(0)` 的真实 `isVisible()` 和 `width()`. 左 sidebar 隐藏时 header 左区宽度回到 44; 显示时跟随其实际宽度. 检测可观察对应 pane 的 Show/Hide/Resize 和 splitter 变化.

只改变 header 自身宽度, 不调用 `splitter->setSizes`, 不改变左右 pane 的 minimum/maximum width, 不写 ConfProxyUser 中保存的侧栏宽度. 这能在原侧栏展开/隐藏和拖动宽度时使用真实布局结果, 避免新顶栏覆盖原有保存宽度.

筛选控件为 `CConditionFilterWidget` (objectName 空), 当前 key `2c77f0262b0`, parent `2c70e57b330`, visible=true, geometry `(0,0,749,35)`. 它与 header 布局不同层, 不应把 header 是否显示某按钮当作筛选栏是否展开的状态来源.

## 验证范围

13 个聚焦静态函数提取成功; 菜单与 QAction、pane identity/visible/geometry 均通过在线只读诊断交叉确认, 查询结果没有截断. 当前 diagnostics 对 QAction 未输出 checked/checkable/data, 因此本轮不声称运行时选中项已被读取. 组合面板 checked/enabled 同步、实际 filter 切换、四布局和排序交互、左右栏组合显隐及拖动宽度需主任务最终 GUI 验收.

## 补充: 基础加号菜单、目录跳转与 caption 清条件

本轮只做基础顶栏和既有功能映射, 不增加业务后台. `native-toolbar-basic-actions.json` 保存现有 QAction 的只读快照.

全局主菜单已初始化的导入动作包括 `导入文件`, `导入本地文件夹`, `导入Billfish素材包`, `导入其他` 子菜单. 后者现有动作是 `导入Eagle资源库`, `导入Eagle素材包`, `导入花瓣画板`. 它们关联主菜单, 不是从文件/文件夹右键菜单中借来的上下文动作. `BfLocalResWidget` moc 还确认 `slotImportFile()` / `slotImportFolder()` 业务入口. 新加号菜单应复用已关联全局主菜单的原动作, 保留其原连接和 parent.

没有确认全局新建文件夹 QAction. 当前唯一显示 `新增子文件夹 (N)` / `Ctrl+Shift+N` 的 action 属于 `BFTreeFolderMenu`, 依赖右键上下文. 不应直接将它当成全局新建目录入口. 已有 `BfTreeViewControl::signAdd()`, `signAddSub()` 信号和 `AddFolder(QModelIndex)` 方法, 但此轮没有认证其 QML 连接和当前目标索引, 因此不建议仅凭方法名调用.

同样没有确认专用 J / 目录快速跳转对话框. 现有 tree 搜索候选为 `BfTreeViewControl::UpdateSearchTextChanged(QString)`, `signSearch()`, `BFSortFilterProxyModel::slotSearchTextChanged(QString)`. 它们不是专用目录跳转界面的等价证据. 未发现的入口保留为缺口, 没有扩大扫描或创建新后台.

caption 清空 keyword 和 filter 有下列已有语义证据:

1. 当前主搜索为 `ToolModule::NavSearchWid` / objectName `m_search_wid`, 其后代 `QLineEdit` objectName 为 `edit_search_input`. 清该输入框可使用原有 textChanged 链, 同步用户看到的输入和搜索业务. 单独调用 nav 的 empty-query slot 不会自行清除可见文本.
2. `BFNavigationBar::SlotSearchChange(QString)` 的 toolmodule RVA `0xb480` 对空字符串发 `INavBarWidget::sigSearchResult(false, {}, {})`, 并恢复普通导航显示; 非空分支才进入搜索. 既有静态记录见 `task-memory/task-billfish-cli-api/native-search-evidence.json`.
3. core `BFItemIconView::slotSearchResult` (RVA `0x6e540`) 的 false 分支 `0x6e83c` 使用该 view 已保存 provider 参数 (`this+0x98 -> +0x18/+0x20`) 恢复 provider, 没有指定 All provider 常量. 这是保留当前目录的基础, 不能替换为主目录/All 导航.
4. `CConditionFilterWidget::slotAllFilterBtnClear()` 为真实 moc slot local index 31, metacall 分支 `0x400e9` 调用 `0x2a550`. 该函数依次清除 8 类条件, 重建默认 FilterData、更新过滤业务和 UI, 没有直接 folder 导航参数. 可以在清 keyword 恢复 provider 后调用当前 filter widget 的该 slot.

组合行为建议先清主搜索输入, 再清当前条件栏, 避免把 `on_btn_search_back_clicked()` 当作清输入接口而引入额外历史导航. 本静态与只读任务没有执行清除动作; 必须在非 All 目录、同时具有 keyword 和 filter 的实际场景验收当前 folder/provider 保持不变.
