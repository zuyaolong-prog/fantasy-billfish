# Eagle 顶部工具栏静态证据与实施对照

取证日期: 2026-10-18 (Asia/Shanghai). 范围为 `E:\Software\Eagle\resources\app.asar` 内主窗口普通素材列表模式, Eagle 4.0.0 build 20260304/20. 本轮只读现有 ASAR 成员, 不启动 Eagle、不操作 GUI、不复制实现代码或美术资源. 沿用已有全包 REA Evidence `ev_31f1d058126116bc2f2e937de1e252549699d1708e7b0bea155f6769a20ce481`; 其图覆盖为 partial, 不能声称覆盖全部运行行为. 本节行为细节来自精确字节的 HTML、SCSS、JS 与中文词条互证, 新增逐成员哈希见 `task-memory/task-billfish-cli-api/eagle-toolbar-hashes.json`.

ASAR SHA-256: `1f43621a7cbef7f13d488fa1e248137abe18f7f6411f3fccffaf3894f3c2f6d0`. 下文 app.bundle.js 行号按 LF 分行; 文件原有 CRCRLF 不影响所列 LF 行序, anchor 可用于精确定位.

## 普通素材模式从左到右

| 顺序/分区 | 控件与外观 | 交互/状态 | 证据 |
| --- | --- | --- | --- |
| 左 0, 条件 | 应用菜单图标 | 仅侧栏隐藏时出现; 打开应用菜单 | `app/index.html:560` |
| 左 1 | 侧栏图标 | 左键同时切换左右栏; 右键出现独立栏位菜单 | `app/index.html:564`, `app/app.bundle.js:30840`, `:44975` |
| 左 2/3 | 后退/前进箭头 | 根据历史 canGoBack/canGoForward 显示 disabled; 点击走 webContents 历史 | `app/index.html:568`, `:572`, `app/app.bundle.js:38424` |
| 左 4 | 面包屑标题 | 全部/未分类/未标签/最近/目录/智能目录等当前上下文; 搜索或筛选时追加搜索结果与数量; 点击上下文重置搜索/筛选再进入 | `app/index.html:576`, `:592`, `:597` |
| 中间 | -、缩略图滑条、+ | 拖动改变素材显示尺寸并记忆, 不改变原文件; 整组弹性居中 | `app/index.html:602`, `app/style/components/_toolbar.scss:52` |
| 右 0, 条件 | 置顶插件图标 | 按置顶排序显示, 点击打开对应插件 | `app/index.html:618` |
| 右 1 | 插件图标 | 打开插件面板, tooltip P, 可更新时显示小圆点 | `app/index.html:622`, `app/app.bundle.js:37182` |
| 右 2, 条件 | 随机刷新 | 当前目录/智能目录排序为 RANDOM 时显示, tooltip R | `app/index.html:627` |
| 右 3 | 操作图标 | 打开操作面板, tooltip G | `app/index.html:630` |
| 右 4 | 布局/排序图标 | 单一按钮打开组合面板, 包含布局、排序、显示项和栏位开关 | `app/index.html:634`, `app/app.bundle.js:45118` |
| 右 5 | 筛选漏斗 | 切换下方筛选工具条, 开启显示 active, 有条件显示数量 badge | `app/index.html:637`, `app/app.bundle.js:30770` |
| 右 6 | 常驻搜索框 | 左边搜索范围箭头, 中间输入, 右边以图搜图入口/AI状态; focus 弹建议 | `app/index.html:658` |
| 右 7, 条件 | 窗口按钮 | 检查器隐藏或标签管理页时放在此处, 正常情况下在右侧检查器顶部 | `app/index.html:682`, `app/js/directives/inspector.html:6` |

**需要保持的行为事实**: 搜索输入默认常驻. 没有看到普通模式先点击一个独立放大镜按钮, 再弹出整块搜索框的实现. 排序与视图共用一个组合面板, 没有两个独立顶部按钮. “侧栏”按钮左键同时切换左侧导航和右侧检查器, 独立控制在右键菜单和布局面板.

## 搜索

- Ctrl+F 聚焦当前上下文搜索并全选文字. Ctrl+Alt+F 先进入全部素材再聚焦搜索. 来源: `default-preferences.js:87`, `:88`; `app.bundle.js:24771`, `:29072`.
- 输入框 maxlength=4096, type=search. 点击/获得焦点更新建议, 标签管理页不展示建议. 建议或历史两者都为空时不显示空弹层. 来源: `index.html:545`, `:663`; `app.bundle.js:29573`.
- 左侧范围菜单依次是: 文件名、文件夹名、文件夹描述、扩展名、标签、来源 URL、注释、笔记. 多选, 点击后保持菜单打开; 有 keyword 时立即重筛, 并逐项持久化. 无用户偏好时八项均 true. 来源: `app.bundle.js:21166`, `:21401`, `:44851`.
- 上/下方向键选择建议或历史, Enter 确认并失焦. Esc 第一次只关闭建议; 建议已关闭时, keyword 非空则清空并失焦, 空则只失焦. 来源: `app.bundle.js:29670`.
- 失焦后 200ms 隐藏建议, 非重复且非空的关键词加入历史, 最多保留 100 个; 建议列表只取匹配历史前 8 条. 来源: `app.bundle.js:29599`, `:29734`.
- 搜索框右侧的图片图标在无关键词、AI Search 不启动/同步时显示; 点击进入 image 筛选. AI Search 正在启动/同步时显示 loader, 点击打开该插件. 来源: `index.html:665`.
- 这里的“范围”指参与匹配的元数据字段, “当前页/全部素材”由快捷键和上下文决定, 不应混为一个选项.

## 缩略图滑条

- HTML 范围 75..900, step=5, 默认 height=150; max 来自 MAX_LIST_WIDTH=900. 来源: `index.html:609`, `app.bundle.js:21123`, `:21467`.
- 拖动经过 100ms throttle 触发布局变化、保存尺寸和数量检查; 尺寸按 5 对齐. 来源: `app.bundle.js:20947` (anchor `onListSizeChange`).
- -/+ 在瀑布流 GridLayout 与网格 SquareLayout 按列数改变尺寸; 其他布局依据当前尺寸以 25/50/100 阶梯改变, 最终夹到 75..900. 不应简单认为所有布局 +/- 恒为 5. 来源: `app.bundle.js:33682`, `:33726`, `:33742`.
- tooltip 显示 -/+; 默认快捷键映射中的 view.zoom.out/in 为 Ctrl+- / Ctrl+=. 来源: `index.html:604`, `:611`, `default-preferences.js:158`.
- 它调整卡片/缩略图尺寸, 不修改原文件、DPI 或视频编码.

## 布局/排序组合面板

来源 `app/js/directives/layout-panel.html`, JS `layout-panel.js:35` 会把面板移到光标位置, 加 open 状态并在 50ms 后聚焦内部键盘输入. overlay 点击或 Esc 关闭. 打开时更新当前排序和方向, 来源 `app.bundle.js:45118`.

| 面板顺序 | 选项 | 默认/边界 | 来源 |
| --- | --- | --- | --- |
| 布局方式 | 瀑布流 GridLayout, 自适应 JustifiedLayout, 网格 SquareLayout, 列表 ListLayout | 未保存布局时按库初始化到 JustifiedLayout. 中文名称不能仅按英文 Grid 推测 | `layout-panel.html:9`, `i18n/zh_CN.js:333`, `app.bundle.js:22867` |
| 填充方式 | Fit / Fill | 只在 SquareLayout 显示, 默认 Fit | `layout-panel.html:26`, `app.bundle.js:21177` |
| 缩略图 | 性能 / 质量 | 切换 showOriginalImageWhenLarge; 未保存为 false 时启用质量 | `layout-panel.html:36`, `app.bundle.js:21158`, `i18n/zh_CN.js:1008` |
| 排列方式 | 添加日期 IMPORT, 修改日期 MTIME, 创建日期 BTIME, 标题 NAME, 扩展名 EXT, 文件大小 FILESIZE, 尺寸 RESOLUTION, 评分 RATING, 时长 DURATION | 全局列表选项. 默认 orderBy IMPORT, sortIncrease true; 已保存的库偏好覆盖默认 | `layout-panel.html:108`, `app.bundle.js:21164`, `:22867` |
| 目录专属排序 | 使用全局设置、 自定义 MANUAL、以上字段、随机 RANDOM | 普通目录提供 MANUAL; 智能目录无 MANUAL, 有 RANDOM | `layout-panel.html:50`, `:81` |
| 方向 | 升序 / 降序 | 两个互斥图标按钮, 当前项 active | `layout-panel.html:133` |
| 展示开关 | 显示名称、显示简介、显示扩展名、显示扩展名标签、显示标注数、显示子目录内容 | 简介可选尺寸/大小/标签/评分/修改/创建日期. showAnnotation 中文是标注数, 不等于全文注释 | `layout-panel.html:140`, `i18n/zh_CN.js:999` |
| 栏位 | 显示侧栏、显示检查器 | 独立开关 | `layout-panel.html:214`, `:223` |
| 底部 | 刷新 | 调用宿主 reload | `layout-panel.html:233` |

默认快捷键: Alt+1 瀑布流 GridLayout, Alt+2 自适应 JustifiedLayout, Alt+3 网格 SquareLayout, Alt+4 列表 ListLayout. 源 `default-preferences.js:152` 与 `app.bundle.js:25351`/`:25372`/`:25393`. 第三项内部 shortcut key 叫 waterfall, 但实际 handler 与显示文案明确是 SquareLayout/网格, 已按 handler 核对.

## 筛选工具条

点击漏斗只展开/折叠下方工具条, 不自动清除筛选; 折叠会关闭已打开 filter-item 弹层并更新内容区高度. isOpen 持久化到 isOpenFilter, 无保存值默认关闭. 来源 `app.bundle.js:30770`, `app/js/lib/api/filter.js:4`, `:351`.

默认固定到条中的筛选项依次为颜色、标签、文件夹、形状、评分、类型. 其他可添加项目包括以图搜图、语义搜索、添加日期、修改日期、尺寸、时长、大小、注释、笔记、URL、字体状态、BPM、相机. 顺序和固定项可持久化. 源 `filter.js:184`, `:253`, `:316`; `index.html:725`.

筛选条右侧有已保存筛选、锁定、重置入口, 依据当前筛选/保存状态出现. 漏斗上 badge 是筛选条件数量, 不是结果数量. 来源 `index.html:637`, `:764`.

默认快捷键: Ctrl+Shift+F 展开/折叠; Ctrl+Shift+Alt+F 重置; Alt+F 文件夹, Alt+T 标签, Alt+C 颜色, Alt+S 形状, Alt+R 评分, Alt+D 日期, Alt+E 类型. 来源 `default-preferences.js:90`.

## 侧栏与窗口

- 侧栏图标左键: 若左侧隐藏, 两栏一起显示; 若左侧显示, 两栏一起隐藏. 然后重新布局. 右键菜单三项: 所有栏位 (Tab)、侧栏 (Ctrl+Alt+1)、检查器 (Ctrl+Alt+2), keepOpen=true. 来源 `app.bundle.js:30840`, `:44975`.
- 默认配置另有 Ctrl+Alt+3 切换全部. Tab tooltip/右键项与该可配置快捷键同时存在, 两者不能直接替换解释. 来源 `default-preferences.js:181`, `index.html:128`.
- Windows 窗口右上顺序是置顶图标、分隔、最小化、最大化/还原、关闭. 置顶 Shift+T, active 状态切换 pin/unpin 图标. 最大化在 fullscreen 时先退出全屏, 否则最大化/还原互换. 来源 `corner-btns.html:1`, `corner-btns.js:15`, `default-preferences.js:86`.
- 顶栏空白处声明为 drag 区且双击 maximize; 交互控件 no-drag 或停止双击冒泡. 来源 `index.html:555`, `_toolbar.scss:9`, `:276`.

## 视觉几何与响应式证据

SCSS `_toolbar.scss` 声明: 顶栏高/行高 48px, 水平 padding 8px, 单行 flex, overflow hidden. 面包屑靠左, slider 中间弹性居中, 功能组靠右. 图标按钮最小宽 24px, 左间隔 2px, 图像普通 opacity .8, hover/active 为 1. 搜索框高 32px, 字号 13px, 圆角 6px, focus 主色边框. 来源 `:1`, `:52`, `:215`, `:270`, `:393`, `:463`.

搜索宽度通过顶栏容器查询在 180/200/220px 间变化; 中间可用空间降至 130px 时隐藏 slider. 顶栏容器低于 400px 隐藏面包屑文字. 这表明紧凑状态优先保留功能/搜索, 不表示切换到独立搜索弹窗. 来源 `_toolbar.scss:20`, `:28`, `:158`, `:188`.

这些是已打包源码中的样式声明. 未做 GUI 截图或 computed style 检查, 不把声明尺寸称作本机当前屏幕实际像素, 也未验证不同 DPI 下渲染.

## 模式差异与实施边界

- 标签管理 alltags: 隐藏 slider、插件/操作/布局/漏斗, 使用标签网格/列表和标签组排序控件; 搜索仍在, 不显示素材搜索建议. `index.html:603`, `:622`, `:643`.
- 全局随机页: 顶栏右侧简化为刷新、筛选、搜索、条件窗口控件. `index.html:686`.
- 详情模式默认不显示本节列表工具栏, inline 模式另有状态. `index.html:555`.
- 本报告可指导 Billfish shell 排列、交互、菜单和快捷键. 比对时应注明未实现的业务能力, 不把可点击但无效果的按钮当作功能对齐. AI 插件状态、目录专属排序、历史导航和双侧栏行为需要以 Billfish 实际数据/窗口宿主实现.
- 未启动原 Eagle, 没有 runtime 观察. 没有读取用户 localStorage, 所列默认值是无已保存偏好时的源码默认. 资源图标仅记录语义与位置, 未复制 vendor SVG 或 CSS 实现代码.

## 关键成员 SHA-256

| 成员 | SHA-256 |
| --- | --- |
| `app/app.bundle.js` | `3da1746ef3ed67402a894f488ecae81d364ed09b550716f4b1438555fa84d2bc` |
| `app/index.html` | `60a2100823099b91da73b01868452583b058e94becdd657d17288ba373b62200` |
| `app/js/default-preferences.js` | `ea8b5a6d40d1e9e581ae92f6cc4ca4c007b0e83900f32eb0c407877cbf59a62b` |
| `app/js/directives/corner-btns.html` | `d35aa6220a750e8fe3c66be126f7e994586471d38846eea7f1a917ad8b6fa9ee` |
| `app/js/directives/corner-btns.js` | `7b1e3fb52627b02a1876c5f3c6e521be8595a23b466ac3e4cbe848cc97ad3a8a` |
| `app/js/directives/inspector.html` | `fb5274c1068695edf6d703257173f95654d9abb98e740e33e88e66e37eff1493` |
| `app/js/directives/layout-panel.html` | `c7bee10f992c6caf28f1a313989c46dc3657c13ba2a7a77b503ad759a1a1b103` |
| `app/js/directives/layout-panel.js` | `738ff6e7d8e5d8aaa3ac088fcbe17d2556bf73ff14fba23edc63f62d4b0ffb7c` |
| `app/js/lib/api/filter.js` | `362564f44eac8be964caf75fc8910206015cb915d6ccb7a9659f442392cb4a5f` |
| `app/style/components/_toolbar.scss` | `e64acc8e6b7017aa9e91f82e9387186d034844041c55e2c4524d96be5a5b5746` |
| `i18n/zh_CN.js` | `d7c4bc73fcbad1cf54a33d2aed55c84a1eccae49147fcbc5474494ca6a77605a` |


## 左侧库名行补充: +、双向箭头与标题点击

此行位于左 sidebar 顶部, 与中间素材 toolbar 是两块并排区域. `app/index.html:85` 声明 sidebar-header, 库名入口后接 sidebar-toolbar. 截图坐标只用于定位, 行为以下列静态 handler 为准.

| 从左到右 | 名称/入口 | 精确行为 | 来源 |
| --- | --- | --- | --- |
| 库 icon + 库名 + 下拉标记 | switchLibrary | 普通点击广播 OPEN_LIBRARY_PANEL, 是库切换入口; 默认 Ctrl+L. Ctrl/Alt/Meta 修饰点击另有修复工具分支 | `app/index.html:107`, `app/app.bundle.js:37171`, `default-preferences.js:84` |
| + | openNewContextMenu | 新增/导入的可搜索菜单, 默认 Ctrl+N. 不等同单一“新建文件夹”按钮 | `app/index.html:120`, `app/app.bundle.js:37296`, `default-preferences.js:41` |
| 双向箭头 | openQuickSearch | 广播 OPEN_QUICK_SEARCH_MODAL, 打开快速跳转框, 默认 J; 初始搜索模式 FOLDERS | `app/index.html:124`, `app/app.bundle.js:32355`, `quick-search-modal.js:13` |
| 侧栏图标 | toggleAll / openSidebarMenu | 左键同步显隐双栏, 右键分别控制栏位, Tab | `app/index.html:128`, 本文侧栏章节 |

### + 菜单的准确内容

来源为 `app/app.bundle.js:37296`, anchor `openNewContextMenu`. `ContextMenu.open` 声明 `showSearch:true`. Windows 普通环境按源码定义的顺序如下:

1. 新建文件夹: 调用 newFolder, 默认 Ctrl+Shift+N. 这是 Eagle 素材库业务目录, 本次未证明创建普通磁盘目录; Billfish 应保留自身目录语义.
2. 新建智能文件夹: 调用 newSmartFolder, 默认 Ctrl+Shift+Alt+N.
3. 分隔后, 导入本地文件夹: 调用 importFolders, 原生对话框允许 openDirectory + multiSelections. 这不是 openFile 多文件导入动作; handler 位于 `app.bundle.js:26781`.
4. 导入链接: importLinks.
5. 导入 Eagle 素材包: 原生对话框筛选 .eaglepack, 然后发送 open-eaglepack. Billfish 当前不具备该格式时不能把任意文件导入冒充为 Eagle 素材包支持.
6. 自动导入子菜单: 设置打开 preferences 的 autoImport 页; 已启用且配置路径时另显示该路径, 源码将该路径项标为 disabled.
7. 找重复子菜单: 全部 / 当前列表 / 已选择. 当前列表空时禁用对应项, 已选择数小于 2 时禁用已选择项.
8. 分隔后是文档模板: Photoshop、Illustrator、XD; 再分隔, 文本、Word、PowerPoint、Excel、XMind. 文本项 Alt+Shift+N. 不同平台/已安装应用还可条件显示 Sketch、Keynote、Pages、Numbers、MindNode.
9. 分隔后为 Pinterest、ArtStation; 中文环境还显示花瓣. 这是在线采集入口, 不等同普通导入.

file.import.folders/links/eaglepack 的默认快捷键为空, 不应凭常见软件习惯添加快捷键并称为 Eagle 默认. 来源 `default-preferences.js:45`.

### 双向箭头不是库切换

快速跳转框初始为 FOLDERS. 点击时清空主搜索 keyword, 打开 modal、聚焦并选中 quick-search-input; 这里没有 resetFilter 的直接调用. 初始目录模式可搜索业务目录, 无词且目录较多时可先展示最多 10 条最近使用目录. 来源 `quick-search-modal.js:15`, `:138`.

模态框可切换 FOLDERS/TAGS/SMARTFOLDERS/ITEMS 四类, Tab 正向循环、Shift+Tab 反向循环, 上下方向键切换选项, Enter 跳转, Esc 关闭. 各类结果分别调用 openFolder/openTag/openSmartFolder/openItemLocation, 然后保存历史并关闭. 来源 `quick-search-modal.js:279`, `:321`.

因此 Billfish 若只需对齐简单顶部动作, 双向箭头应接现有目录/标签导航选择器或清楚标注范围的快速跳转, 不能连接“导入”或“切换素材库”. 完整四模式跳转框属于更多业务范围, 不应该用无效果的菜单假装已实现.

### 素材标题/caption 的最小点击行为

这指素材 toolbar 的面包屑标题, 不是左侧库名. `app/index.html` 的 HTML handler 直接给出了调用顺序:

| 当前上下文 | handler 语义顺序 | 来源 |
| --- | --- | --- |
| 全部 | resetKeyword -> resetFilter -> filterContent -> openAll | `app/index.html:577` |
| 未分类/未标签/最近 | 同样先清关键词、清筛选、重筛, 再 openUnfiled/openUntagged/openRecent | `app/index.html:578` |
| 回收站 | 同样清理后 openTrash | `app/index.html:588` |
| 随机 | resetKeyword -> resetFilter -> filterContent; 保留随机上下文 | `app/index.html:587` |
| 当前普通文件夹 | resetKeyword -> resetFilter -> filterContent -> openFolder(currentFolder) -> changeSidebarIndex(currentFolder) | `app/index.html:592` |
| 当前智能文件夹 | resetKeyword -> resetFilter -> filterContent -> openSmartFolder(currentSmartFolder) | `app/index.html:594` |
| 标签管理/当前标签标题 | openAllTags; 不是通用清理序列 | `app/index.html:582` |

resetKeyword 本身只把 keyword 设为空, `app.bundle.js:27392`. resetFilter 解除筛选锁、清 badge 和规则、清当前 tag/folder 条件、清日期月份选择、关闭 filter-item 弹层、startCursor=0、广播 Reset_Filter、重算 counts, `app.bundle.js:27401`. 它没有把 filter toolbar 的 isOpen 设为 false. 因此简单实现应保持当前库和目录, 清搜索/过滤后刷新当前视图并对齐 sidebar 选中位置; 不应强制跳到全部素材或关闭筛选工具条. 这可作为 Billfish 自己的 UI handler 语义参考, 不需要复刻 Eagle 整个 workflow.
