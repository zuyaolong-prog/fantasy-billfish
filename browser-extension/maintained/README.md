# Billfish 采集 - 维护版

可加载目录: `E:\Billfish\browser-extension\maintained`.

固定扩展 ID: `aloippladlgkfkcabiknfpokgcppcjkp`. Native Messaging host: `com.billfish.capture`. 版本: `4.2.0`.

该目录以本机已安装 Billfish Edge 采集器 4.0.5 的实际资源为图片功能基线. `provenance.json` 保存原始每文件 SHA-256 和精确补丁, `identity.json` 保存固定公共 RSA key 与 native host origin. `_metadata` 签名目录不属于 unpacked 维护版. 原用户 Edge 安装目录未修改. 没有引入第三方视频扩展源码或私有通信协议.

视频功能复用原网页采集框和原右键菜单. 拖入 YouTube/Bilibili 视频链接或缩略图, 或 HTML video, 会把显式选择的视频地址发送到本机 Billfish. Native Messaging host 接受后, Billfish 自身负责显示分辨率弹窗, 确认下载和入库. 视频结束状态由 Billfish 显示; 网页提示只表示请求已接受. 普通图片保持原始 HTTP 采集路径. 未识别的普通网页链接不会自动作为视频提交. 不采集 cookies、账号或浏览历史.

4.2.0 增加自动发现入口. 在顶层 Bilibili/YouTube 视频页, 或包含实际 HTML video/source 的普通网页中, 右侧显示中文 `采集视频` 小按钮. 同页多个真实视频会提供下拉选择. 自动发现只展示入口和查询本地禁用设置, 不创建采集任务, 不自动下载, 不自动弹出 Billfish 分辨率窗口. 点击按钮后, 才通过相同 Native Messaging host 进入 Billfish 的真实分辨率选择流程.

自动入口不扫描页面所有推荐视频 anchor. SPA 导航和 video/source 换源会使旧候选失效, 点击前及策略查询后都会重新核对当前候选, 后台还会复核当前标签页 URL. 用户禁用的网站、现有采集器的网站禁用列表及无法确认的策略回执均不会放行. 普通 iframe 不显示该顶层按钮. 没有新增权限、账号读取或网页分辨率接口.

Native host 已提交任务但响应超时时, 扩展保留 `job_id` 和结构化 `error.code/message`, 网页持续显示任务 ID 与先检查 Billfish 的提示, 不自动重发. 此时应先查询或查看该任务结果再决定是否重新采集.

视频拖拽开始时同步显示原采集框并注册结束事件, 域名排除查询并行进行; 提交前必须等同一次查询允许, 失败时保持拒绝. 因此快速拖拽不会因后台往返漏掉 dragend, 也不会绕过网站禁用设置. 视频框明确提示保存至浏览器采集文件夹, 隐藏不适用的视频目标选择; 图片框保留原有目录选择.

每次拖动各自保存候选与排除查询. 视频释放时立即锁定该次地址、是否落在框内及提交上下文, 清理该次框后等待排除结果, 最多提交一次. 迟到的旧拖动不会读取、清理或代替下一次拖动, 后续图片也会重新显示正常目录选择.

安装顺序由主任务执行, 本目录构建脚本不操作浏览器:

1. 先安装对应 native host, 允许 origin `chrome-extension://aloippladlgkfkcabiknfpokgcppcjkp/`.
2. 在 `edge://extensions` 中停用旧 Billfish 扩展, 开启开发人员模式, 点击 `加载解压缩的扩展`, 选择本目录.
3. 确认 ID 与上方一致, 已加载旧维护版时点击一次重新加载, 然后刷新要采集的网页. 可点击右侧 `采集视频`, 或继续使用原采集框拖拽及原右键采集菜单.

重新构建: `node build-fork.cjs [原始扩展目录]`. 脚本要求补丁锚点精确匹配, 否则报错. 不要以本维护目录自身作为源. 原始资源文件存在版本变动时应先重新分析和评审.

验证: `node --test tests/automatic-capture.test.cjs tests/video-capture.test.cjs`. 自动入口测试覆盖实际 content 启动和按钮事件、发现不提交、点击才提交、SPA/换源失效、禁用/未知回执拒绝、真实 background handler 和生成的策略适配器. 原测试继续验证视频分类、实际 Native Messaging 回执与失败处理、普通图片不误分类、原 image import 代码和资源保持, 以及 JS 语法. 真实 Edge 自动按钮、拖拽、右键、Billfish 分辨率和下载入库仍由主任务集成验收.
