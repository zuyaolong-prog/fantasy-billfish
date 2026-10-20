# Billfish frozen native boundary

本报告用于维护当前 CLI/API 的真实库访问边界. 调查日期为 2026-10-18, Asia/Shanghai. 调查没有修改已安装 DLL, 没有对用户素材库执行写入, 没有直接启动 `Billfish.core.exe`.

## 结论与实现边界

当前冻结 Billfish 已恢复适合 GUI 在线整理的 `BFFileAction/BFTagAction` Qt 业务入口, 包括 note, origin, 标签新建及文件标签关联. 当前库身份来自运行中 native owner, 任务完成来自原生 result 信号. 这些已有业务入口的具体签名, 工厂, 数据布局和实现见后文的在线取证部分; 实际 GUI 验收由主任务记录.

没有从 vendor 包建立一个公开 HTTP/RPC 数据中心. `CreateBFLibraryManager` 返回 C++ 接口指针, `CreateClient` 返回带 Qt 元对象的客户端, 不能仅因 factory 存在就调用任意业务. 已恢复的在线入口是受限制的 Qt action surface, 不是对整个 C++ ABI 的完整重建.

早期离线调查另已通过实际执行验证原生 FTS5 注册函数. CLI 的内部数据库桥接使用冻结的 vendor SQLite, 在每个连接上注册原生 `bf_tokenizer`, 通过参数绑定读写普通 SQLite 与 FTS5 表. 它在专用隐藏进程中运行, 仅通过父进程的 stdin/stdout 管道通信, 没有网络监听或面向用户的任意 SQL 接口. GUI 在线操作走原生业务 action, 不直接在线写 SQLite.

这项桥接解决的是 tokenizer 与数据库连接的 ABI 一致性. 它不证明已经恢复完整 data-center 业务逻辑. 文件导入, 移动, 重命名, 回收, 在线 GUI 写入均不能仅凭本模块推导为可安全开放的能力. 新增写能力必须验证其完整业务副作用.

## 冻结身份

| 文件 | SHA-256 | 本次静态观察 |
| --- | --- | --- |
| `libBFDataCenter.dll` | `302c9b755b81ad97cd863ddfeb3caf58dded6dd662c9ea486c02e5d626459ce6` | PE AMD64, 195 个导出 |
| `sqlite3.dll` | `e845a94029d9fc238631950ca66439c62246573396747643a58ab665f19ba5d9` | PE AMD64, 实际运行版本 `3.38.5` |
| `bfclient.dll` | `7cf9540c64827e1baae20d1499b9fe8fb3f412ea74ec6002e6021a06ef61604c` | PE AMD64, 39 个导出 |

`automation/billfish_api/vendor_sqlite.py` 对两个实际加载的 DLL 校验完整 SHA-256 和 PE machine, 限定 Windows AMD64 Python, 再确认 SQLite 运行版本. 该约束用于固定当前二进制的私有 RVA 调用, 不构成通用版本兼容机制.

## 原生 tokenizer 的证据

以下地址以当前 `libBFDataCenter.dll` 的 preferred image base `0x180000000` 为参照. 调用时使用真实模块基址加 RVA, 支持 ASLR.

| 位置 | 观察 |
| --- | --- |
| 文件 offset `0x52e690` | 字符串 `SELECT fts5(?1)` |
| 文件 offset `0x52e6a8` | 字符串 `fts5_api_ptr` |
| 文件 offset `0x52e6c8` | 字符串 `bf_tokenizer` |
| RVA `0xE9FF0` | 接收 SQLite connection, prepare `SELECT fts5(?1)`, bind_pointer, step, finalize, 获取 `fts5_api` |
| RVA `0xEA110..0xEA238` | `.pdata` 声明的函数边界; RCX 保存 SQLite connection, 注册 SQLite 函数, 获取 `fts5_api`, 调用 API 的 tokenizer 注册函数及辅助函数注册函数 |
| RVA `0xEA1AD` | 将 `bf_tokenizer` 名称装入 RDX |
| RVA `0xEA1B7` | 调用 `[fts5_api + 8]`, 对应 FTS5 `xCreateTokenizer` |

该函数的单指针入口及返回值语义由最小内存 probe 和后续文件数据库测试共同验证: `int register(sqlite3*)`, 成功返回 `0`. 这属于固定二进制上的私有调用, 没有将它声称为 vendor 公开支持的 API.

`task-memory/task-billfish-cli-api/native-tokenizer-probe.py` 只创建 `:memory:` 数据库, 实测创建带 `bf_tokenizer` 的 FTS5, 插入中文, 查询中文词, 更新 note 后查询新词, 验证旧词命中为零. 未打开任何素材库.

## 必须隔离 SQLite 所有者

本轮首先实现同进程 ctypes 桥接. 当进程已经 `import sqlite3` 时, 首次注册 tokenizer 实际触发 Windows `integer divide by zero` 异常; 不加载 Python SQLite 的最小进程成功.

进一步读取 `libBFDataCenter.dll` 的已装载 IAT, 将函数地址归属到真实模块, 得到直接证据:

- `sqlite3_prepare_v2` 和 `sqlite3_create_function` 的 IAT 地址属于 Python 3.14 的 `DLLs/sqlite3.dll`, 该运行库版本为 `3.50.4`.
- IAT 地址与显式加载 `E:\Billfish\sqlite3.dll` 后取得的同名函数地址不同.
- 因而仅对 `ctypes.CDLL` 指定绝对路径, 不能保证 data-center DLL 的依赖也绑定到同一 SQLite. 将 vendor 创建的 connection 交给另一个 SQLite 实例属于错误 ABI 所有权.

实际地址随进程 ASLR 改变; 本次完整定位结果保存在 `task-memory/task-billfish-cli-api/native-dll-binding-conflict.json`. 这里的隔离由已观察故障及 IAT 归属证据驱动.

当前 `VendorDatabase` 每个实例创建一个专用进程, 用 `sys.executable -I` 启动准确的 `vendor_sqlite_worker.py` 文件, 通过 Windows `CREATE_NO_WINDOW` 隐藏窗口. helper 只加入它自己的 package root, 不导入 Python `sqlite3`, 不创建进程池, 不接受网络请求. 每个 statement, bind, step, column, finalize 与数据库 connection 均来自同一个 vendor SQLite. `os.add_dll_directory` 句柄持有至 connection 关闭.

## 主库和 summary 的真实字段

原生 DLL 的静态 SQL 字符串与生产库 schema 相互印证. 以下不是 `reimpl/proto/bf_datacenter.proto` 的推测合同.

```sql
CREATE VIRTUAL TABLE bf_summary USING fts5(
  iid UNINDEXED, name, type, origin, comment, note,
  tokenize=bf_tokenizer
);
```

`summary_v2.db` 的 `bf_summary.iid` 对应 `bf_file.id`. 原生 rebuild 使用的字符串模板, 文件 offset `0x4f73d0`:

```sql
INSERT INTO %q.%q(iid,name,type,origin,comment,note)
SELECT f.id,f.name,t.name,origin,comments_summary,note
FROM %q AS f
LEFT JOIN %q AS t ON f.tid=t.tid
LEFT JOIN %q AS u ON f.id=u.file_id;
```

对应当前 schema 为 `bf_file`, `bf_type`, `bf_material_userdata`. `type` 来自 `bf_type.name`, `comment` 来自 `comments_summary`. 标签和评分不在这组 FTS 字段里. note/origin 的写入需要同时维护该 iid 的 summary 行. 不可直接编辑 FTS shadow tables.

文件 offset `0x4f4cf0` 为 userdata 全字段替换模板. 文件 offset `0x4fbda0` 为创建 userdata 行的具体模板:

```sql
INSERT INTO bf_material_userdata(
  file_id,note,origin,score,cover_tid,
  comments_summary,comments_count,comments_detail,
  rotation,hflip,vflip
) VALUES(?,?,?,?,?,?,?,?,0,0,0);
```

旋转和翻转的三个默认值在该 SQL 中明确为 `0`. `cover_tid` 的现有生产样本为文本 `"0"`. 不应使用整行 `INSERT OR REPLACE` 实现局部更新: 生产 schema 的 `UNIQUE(file_id) ON CONFLICT REPLACE` 会替换整行, 丢失未指定字段. 使用 `UPDATE` 已存在行和针对缺行的明确默认值.

文件 offset `0x4f8280` 的读取 SQL 将 `bf_material_userdata AS ud` LEFT JOIN 到 `bf_file`. 本次没有恢复 `NULL` 到 C++ 字段的完整转换逻辑, 所以不声称给缺行插入任意 `NULL` 都与 native 创建行为等价.

原生标签证据:

- 文件 offset `0x4f6ee0` 的查询显式将 `bf_tag_join_file` JOIN 到 `bf_tag_v2`, 支持判定 v2 为当前业务标签表, 不需要同时写 legacy `bf_tag`.
- 文件 offset `0x4f4f90` 的标签新增模板是 `INSERT INTO %s(name,pid,seq,icon,color,born) VALUES(?,?,?,?,?,?)`.
- 文件 offset `0x4f51f0` 的文件关联模板是 `INSERT INTO %s(file_id,tag_id,born) VALUES(?,?,?)`.
- 文件 offset `0x4f52b0` 的文件解绑模板按 `file_id` 与 `tag_id` 集合删除.
- folder/group 使用各自的关联 SQL 和表. 仅文件标签请求不等于要求改变 folder/group 的关联.

`born` 的 native 单位, 新标签 seq/icon/color 的完整 native 默认规则, 评分有效范围未在本次证据中确定. 若维护实现将 born 或 seq 设置为 `0`, 这是明确的本地功能设计, 不能标为已恢复的原行为. 导入/移动的监控和分析副作用也仍未验证.

## protobuf 和导出边界

`native-inventory.py` 用已有 `pefile` 与 Python protobuf 解析静态文件, 不加载目标 DLL. 在 `libBFDataCenter.dll` 恢复了 17 个带 Billfish message 的 `FileDescriptorProto`, 包括 `bf_function.proto`, `bftag_function.proto`, `bfmove_function.proto`, `bfload_summaries_function.proto`; `bfclient.dll` 恢复了 9 个. 文件 offset, 长度, 导出表, 导入表和完整字段保存在 `native-inventory.json`.

真实 `BFFile` 字段为 `id=1`, `name=2`, `pid=3`, 并含多类 optional metadata. 现有 clean-room seed 的 package 与字段编号不同, 不能拿它编译出的 message 调用 vendor C++ ABI. 真实 `BFImportRequest` 是 `mode=1`, `folder_id=2`, `params=3`; import modes 包括 `ImportCopy=1`, `ImportCut=2`, `ImportIndex=3`. `BFMoveRequest` 与 tag request 的 wire schema 存在, 但这些描述符没有建立可用 RPC service 端点.

`bfclient.dll` 的 `CreateClient` 静态反汇编分配对象后进入构造器, `DestroyClient` 通过虚表释放. Qt metadata 和客户端 HTTP 字符串不能证明存在面向外部的本地 HTTP 服务.

REA 成功绑定 `libBFDataCenter.dll` 并确认身份, 但 native 深度分析被 provider 缺失阻止: 本机没有已配置 Ghidra/Java 或 IDA provider, Hopper 不支持 Windows. 没有安装或升级外部引擎. 本报告的 native 结论来自 PE/descriptor 静态解析, capstone 聚焦反汇编, 以及上述明确限定的运行 probe. 它们是普通工具取证, 没有伪称为 REA retained Evidence. 调查完成后已关闭 native REA session.

## 可复验检查

```powershell
python .\task-memory\task-billfish-cli-api\native-inventory.py
python .\task-memory\task-billfish-cli-api\native-tokenizer-probe.py
$env:PYTHONPATH = (Join-Path (Get-Location) 'automation')
python -m unittest discover -s .\automation\tests -p test_vendor_sqlite.py -v
```

实际通过的 3 个集成测试使用新建临时数据库, 不使用用户库:

1. 父进程先加载 Python SQLite, 再通过隔离 helper 打开 vendor connection. 验证 attach summary, 中文 FTS 命中, note 更新后新词命中且旧词消失, main 与 attached summary 一起回滚, 只读连接拒绝写入.
2. 验证 int64, 中文及嵌入 NUL 文本, BLOB, NULL 与 float 的参数绑定; 绑定错误和 SQL 错误后 statement 被释放, connection 仍可查询; close 后 Windows 可删除数据库文件.
3. 验证缺失文件不会被自动创建, 不匹配的 runtime 在加载前被拒绝.

打开主库与 attach 均使用 `mode=rw` 或 `mode=ro`, 不设置 SQLite `CREATE` flag. `transaction()` 在读写连接上使用 `BEGIN IMMEDIATE`, 出错回滚. 这种成功/异常回滚验证不等于证明任意 journal mode 组合下断电时的跨文件原子性; 新能力需按实际维护事务边界评估.

## 在线业务入口的后续取证

用户进一步要求 GUI 在线同步后, 取证转向 `ui_plugins/datacenter.dll` 与 `bfright.dll`. 以下内容更新前述生命周期尚未恢复的边界, 但均以静态证据为起点; 实际 QObject 指针, 线程, 进度和刷新效果仍须由进程内探针及 GUI 验收记录确认.

`native-qt-meta.py` 离线解析 Qt5 moc `QMetaObject` 和静态 `QByteArrayData`, 恢复方法名, 参数, 返回类型, access, method kind 和本类 method index. 它不加载 DLL, 不运行 target. `native-qt-meta.json` 保存 9 个已限定文件的完整结果和 SHA-256. 没有因为文件中出现字符串就将其当成 callable method.

真正适合在线整理的 surface 是现有 GUI 的 action 类:

| QObject class | method/signal | 语义 |
| --- | --- | --- |
| `BFFileAction`, 继承 `IFileAction` | `qlonglong slotSetFilesNote(QVector<qint64>,QString)` | 返回 actionId, 创建原生 note 业务任务 |
| 同上 | `qlonglong slotSetFilesOrigin(QVector<qint64>,QString)` | 返回 actionId, 创建原生 origin 业务任务 |
| 同上 | `signalFileProgress(qlonglong,double,int,int)` | actionId, progress, total, already |
| 同上 | `signalFileResult(qlonglong,QSharedPointer<const IResponseInfo>)` | 原生结果, 不是仅提交成功 |
| `BFTagAction`, 继承 `ITagAction` | `qlonglong slotFilesAssociateTags(QVector<qint64>,QVector<DynamicTag>,bool)` | 按已有 tag id 或名称提交关联; bool 为 `check_conflict` |
| 同上 | `qlonglong slotFilesDisassociateTags(QVector<qint64>,QVector<qint64>)` | 按文件 id 与 tag id 提交解绑 |
| 同上 | `signalTagResult(qlonglong,QSharedPointer<const IResponseInfo>)` | 标签任务结果 |

`bfclient.dll` 的 `IBillfishClient/BillfishClient` 自有 moc 仅提供 stopped, space, banner 等信号, 没有上述文件整理 slots. 在线写应进入 `BFFileAction/BFTagAction`, 不应假设 client factory 是本地业务 RPC.

参数名需要与 moc 精确匹配: ids 类型字符串为 `QVector<qint64>`, 返回值类型字符串为 `qlonglong`. 在 `QGenericArgument` 或 `Q_ARG` 中使用任意 C++ alias 的名字可能造成 invoke 查找失败. 将 lambda 调度到 `QCoreApplication::instance()->thread()` 后再 `Qt::DirectConnection` 调用目标是当前 proxy 已有的可复用线程模式. 在跨线程直接传栈上参数或持有裸目标指针之前, 必须确认真实 QObject thread 和 lifetime.

原生反汇编已经对应到具体 GUI 调用:

- `bfright.dll` `BfFileInfoBase::EditNoteFinished(QString)` 位于 RVA `0xA8B0`, 调用内部 action getter `0xC640`, 再调用 action virtual offset `0x70`, 把返回 token 保存在对象 offset `0x60`.
- `EditUrlFinished(QString)` 位于 RVA `0xAA80`, 复用同一个 getter, action virtual offset 为 `0x78`.
- `0xC640` 返回 `this + 0x20` 处缓存的 `QSharedPointer<IFileAction>`. 若缓存尚空, 它从 bfright singleton 取 `IDataCenterManager`, 调用 manager virtual offset `0x80` 取得原生 action wrapper.
- bfright singleton 函数 `0x142B0` 返回 module base + `0x4C400`, 其中 `+0x18` 为现有 manager 指针槽, 即 module base + `0x4C418`. 使用此私有偏移前必须校验当前 bfright SHA: `645091e7584e3a317d51fc687aa673e068470da0173a5906762e50e53d0a224d`, 检查运行时对象确实继承 `IDataCenterManager`, 并确认其线程.

原生 action 工厂与 library owner 不同:

| manager vtable byte offset | datacenter.dll RVA | 行为 |
| --- | --- | --- |
| `0x80` | `0x1CF50` | 当 manager 状态字段 `+0x10 == 2` 时, 创建 `BFFileAction` wrapper, 返回 `QSharedPointer<IFileAction>` |
| `0x98` | `0x1D8A0` | 相同状态约束, 创建 `BFTagAction` wrapper, 返回 `QSharedPointer<ITagAction>` |

工厂复用现有 manager 的 data-center, action queue 与 update queue. 不是另开一份用户库. 当前 x64 机器码调用约定为 `this` 在 RCX, Qt shared-pointer 返回缓冲区在 RDX, 返回 RAX 指向该缓冲区. 不能将它错误声明为普通 free function 的默认结构体返回并猜测参数寄存器. 应优先使用已有 action 缓存; 必须经工厂时保留 native 返回的 `QSharedPointer` 生命周期至结果完成.

`DataCenterPlugin` 的 `qt_plugin_instance` 可以取得 plugin singleton, 但其 `IDataCenterPlugin` interface vtable index `0` 实际执行 `new BFDataCenterManager`. 该入口不是取得当前 manager 的 getter. 不得为了自动化再次调用它创建平行业务 owner. `BFFileAction` 与 `BFTagAction` 的 constructor 没有给 QObject 设置 parent, 所以 `qApp->findChildren` 不一定能发现它们, 需要结合上述明确所有权路径.

`DynamicTag` 的最小布局已由完整参数消费循环恢复:

```cpp
struct DynamicTag {
    qint64 id;
    QString name;
}; // Frozen Windows x64 Qt5: sizeof == 0x10.
```

datacenter helper RVA `0x767E0` 按 `0x10` stride 遍历 vector, 读取元素 offset `0` 的 int64. 大于 `0` 时写 protobuf `tag_ids`; 否则读取 offset `8` 的 QString, 非空时转 UTF-8 写 `tag_names`. 这使已有 id 与名称关联可以进入同一个原生 slot, 不必为该操作恢复复杂的 56-byte `ITagNodeInfo`. 名称不存在时最终如何创建/冲突处理, 仍以实际 native 任务结果和 GUI 验收为准.

`BFResponseInfo` RTTI 确认其 vtable 在 datacenter.dll RVA `0x13DBD0`. vtable byte offset `0x10` 调用函数 `0x5A7F0`, 返回 object offset `0xC` 的 int. 原生同步 tag 调用在该值非零时走失败路径, 因而支持将其识别为 error code, `0` 为成功. 返回正 actionId 只证明提交了任务; 应连接 signal, 按 actionId 匹配结果, 再报告完成. 不要把 `invokeMethod` 返回 `true` 当作业务成功.

`Preferences` 的原生 Qt class 和 slots 也已恢复, 可用于把插件设置放到实际偏好设置内: `slotButtonClicked(SettingMode)`, `slotSaveBtnClicked`, `slotCancelBtnClicked`, `slotDataChanged`. 各页分别为 `PreferencesCommonWidget`, `PreferencesLibraryWidget`, `PreferencesMediaWidget`, `PreferencesUserInterface`, `PreferencesAction`, `PreferencesNotification`; 它们提供本页 save/cancel slots 和 `sigDataChanged` 信号. 本报告不根据类名猜测 widget 几何或对象名, 实际布局由运行时探针确认.

完整聚焦汇编保存为 `task-memory/task-billfish-cli-api/native-online-code-evidence.json`, 含 DLL 身份, `.pdata` 边界和每条指令. 此次取证本身没有调用上述写 slots, 没有更改用户元数据或重启 GUI.

### 已实现的进程内 adapter

`_video-trim-patch/src/automation/native_adapter.hpp/.cpp` 实现了上述 action surface. 它由 QObject 线程检查限定在 GUI 线程, 通过三份 DLL 的 SHA-256, 当前 manager/CLibrary 的确切 vtable 和所属模块校验私有 ABI, 持有原生工厂返回的 QSharedPointer, 并接收真实 `signalFileResult/signalTagResult`.

后续对 PID `29168` 的限定只读 `ReadProcessMemory` 检查确认:

- bfright 静态槽 `+0x4C418` 的 manager vtable 是 datacenter `+0x139100`, state 为 `2`.
- manager `+0x18` 指向原生 `Billfish::CLibrary`, vtable 是 libBFDataCenter `+0x53FE08`.
- CLibrary `+0x68` 指向 `CBFFileManager`, 其首个 std::string 是当前 owner 的真实 library path. 精确导出 `?libraryPath@CBFFileManager@Billfish@@QEAAPEBDXZ` 返回该字符串, 而不是读取最近库配置. 导出 thunk RVA `0xB81B`, 实际 getter RVA `0x4FE90`.

adapter 的 `info()` 使用这条当前 owner 路径, 不依赖 `recent_lib.ini`, 窗口标题或进程启动环境. `native-live-manager.json` 保存有限对象指针与模块归属; `native-live-library-path.json` 保存当时真实 owner 路径, 该文件为本地调查状态而不是发布数据. adapter 执行前和每个后续 action 之前都复核同一 manager, 同一 CLibrary 及同一 root; 库关闭/切换后不再提交剩余步骤. 已完成的在线步骤不承诺回滚.

`tags.create` 的精确输入布局由 native 消费循环与 protobuf 参数字段交叉恢复:

| offset | C++ 字段 | protobuf 参数 |
| --- | --- | --- |
| `0x00` | `QString name` | `name=1` |
| `0x08` | `qint64 pid` | `pid=4` |
| `0x10` | `int color` | `color=2` |
| `0x18` | `qint64 groupId` | `group_id=3` |
| `0x20` | `int icon` | `icon=5` |
| `0x28` | `double sequence` | optional `seq=6` |
| `0x30` | `qint64 id` | optional `id=7` |

`ITagNodeInfo` 总大小为 `56` bytes, 以 `static_assert` 固定. 新建请求将 name/pid 和只读 preflight 得到的 sibling `MAX(seq)+1` 显式传入, color/groupId/icon/id 明确使用 `0`. 这些显式默认值是本地接口设计, 没有声称恢复所有原生 UI 默认. 实际新建通过 `slotCreateTags(QVector<ITagNodeInfo>)` 完成, 不附加临时素材, 不借助 tag_names 绕路制造副作用, 不直接写 SQLite. native 自己决定 born 及业务刷新.

每次实际写请求需要与 operation, arguments, library root 对应的整体 preflight. `tags.assign` 按 preflight 中真正变化的 item/tag pairs 提交, 避免重复关系被错误当成修改. 任务按 item/field 串行调度, 每个 completion 后用零延迟 QTimer 让事件循环继续处理; native 的耗时队列不迁移到任意自建线程. 返回 token 与 invoke 成功不作为完成结果.

所支持的写 slots 都调用统一 enqueue helper `0x2BD10`, 后者进入 `BFActionQueueImpl` 的 `0xD4D0`: 状态不允许时返回 `0`; 接受时构造命令, 在 native 队列中存入 command, 分配 actionId, 调 `QSemaphore::release(1)` 唤醒 worker, 随后返回 token. 这段 enqueue 没有同步 emit result 的路径. GUI action 接收 worker 完成信号, 当前 Direct invoke 栈中没有处理事件循环, 因此在这条已取证路径中可以在 slot 返回后记录 token 再匹配结果.

中途提交失败, 结果身份不可验证或切库时, adapter 的四参 `failed` 信号保留 `details`: 已完成的每项/字段结果, completed/failed/remaining 计数, 当前未知 actionId 及其 `unknown` 状态, 剩余 `not_submitted` 状态. 不清空已提交前缀, 不宣称回滚, 不自动重试未知状态的写入.

这部分的构建与真实 GUI 验收由主任务统一执行. 下述验收记录区分已经实测的行为与静态证据, 不将局部通过外推为所有界面刷新均已验证.

后续首个真实在线 `items.update` 已由主任务执行: note 和 origin 两个原生结果均为 code `0`, 当前选中素材的 GUI 随即刷新. 结果保存在 `gui-online-first-write.json`. 本轮只读复核另确认该文件的 `library_root` 与当前 PID `38084` 的 `host.info` 路径完全一致, 末尾中文为码点 `U+9A8C U+6536`, UTF-8 为 `E9 AA 8C E6 94 B6`. JSON 保存内容没有乱码, 控制台编码显示不能替代对实际 JSON 字符的判断. 对应证据为 `native-encoding-check.json`; 没有为终端显示问题改动 native 路径编码或触发重建.

### 当前搜索结果的成员刷新

主任务随后提供了一个真实 GUI 缺陷案例: 当前按 alpha 搜索到一项素材, 在线将 note/origin 改为 beta 后, 详情立即更新, alpha 结果列表仍保留该素材. 静态调用链解释了该边界:

- `ToolModule::BFNavigationBar::SlotSearchChange(QString)` 位于 toolmodule.dll `0xB480`, 非空文字进入 `0xA930` 调用 `ISearchAction::slotSearchSync`.
- 搜索结果只把 folder/file ID vectors 交给 `BFItemIconView::slotSearchResult(bool,ids,ids)`. core 实现 `0x6E540` 创建固定 ID 结果 provider, 没有向该 provider 传递 query 文字或搜索条件.
- 因此普通 metadata 的 model update 可以刷新某行的值, 但没有建立按当前 query 重新计算列表成员的行为. 对旧结果简单 repaint 不能修复成员关系.

最小修复入口为当前 navigation 的 `SlotSummaryFlagChange(int32_t)`: 该原生 slot 在 toolmodule `0xB530` 读取现有 `NavSearchWid` 的文字, 保持传入的搜索范围 mask, 进入同一搜索和原生结果展示链. 与之相比, `SlotSearchChange(QString)` 固定使用 mask `0x1FF`, 不适合拿来无条件刷新已缩小搜索范围的查询.

现有 query 与 mask 的精确读取证据:

- navigation `+0xB0` 是 `NavSearchWid`, `+0xB8` 是 `BFSearchOptionMenu`.
- `NavSearchWid::text`, RVA `0x355F0`, 读取其 `+0x30` UI 对象的 `+0x18` QLineEdit, 调 Qt `QLineEdit::text()`.
- `BFSearchOptionMenu` 在真实 QObject class chain 中继承 `QListWidget`. 原生 delegate 函数 `0x14400` 读取 model rows `1..9` 的 role `0x104` bool, 依次以 `1,2,4,8,16,32,64,128,256` 合并 mask. row `0` 为菜单标题.
- options model 初始化 `0x14E90` 由 navigation 初始化代码 `0x947E` 调用, 不要求用户先显示该菜单.

`NativeAdapter::refreshCurrentSearch()` 据此提供一个窄方法. 它限定 toolmodule SHA-256 `dd1018659189f191ea9203d3dca4e3b0ffde3064b291f4e78635f38fdb7d1923`, 只接受唯一可见的已确认 navigation context, 核对上述 widget 指针确实出现在 QApplication 的现存 widget 列表并具有精确 class. 没有非空 query 时直接返回 `no_active_query`, 不触发搜索. 有 query 时读取 mask, 调原生 slot, 不设置或清空搜索文字, 保留光标/选择区和焦点.

controller 在整个 native 批次和最终持久化验证都成功后, 复核当前库仍是 job 的目标库, 再调用一次该方法. 不逐素材刷新, 不重提写操作. 返回值保留 `refreshed`, `elapsed_ms`, `mask`, `query_unchanged`, `focus_preserved` 及必要原因, 供真实 GUI 验收和耗时测量.

原生搜索入口本身是同步搜索. 把调用排入 GUI 事件循环不能据此声称搜索工作异步; 本次先保留原生搜索语义并记录实际耗时, 不在没有测量基线的情况下引入另一套搜索执行机制. 聚焦汇编保存在 `native-search-evidence.json`, 对应只读运行时对象证据在 `native-live-search-objects.json` 和 `native-live-search-menu.json`.

统一构建后的真实 GUI 验收已通过: 当前查询 `GUIQueryAlpha42` 命中素材 id `40`, API 将该素材的 note/origin 改为 `GUIQueryBeta42` 后, 原查询无需重新输入即自动显示空结果. 完成响应同时确认两个字段的 native code 均为 `0`, 最终持久化校验 `verified: true`, `verification_count: 3`; `search_refresh` 返回 `elapsed_ms: 1`, `mask: 511`, `query_unchanged: true`, `focus_preserved: true`, `refreshed: true`. 响应证据为 [gui-live-search-refresh.json](../task-memory/task-billfish-cli-api/gui-live-search-refresh.json), 对应 GUI 空结果及保留查询的截图为 [gui-live-search-refreshed.png](../task-memory/task-billfish-cli-api/gui-live-search-refreshed.png).

该次耗时来自仅有 40 项素材的验收库, 不能外推为约 20,000 项生产库的搜索耗时; 实测 mask 为全部字段 `511`, 其他 mask 的运行时保留行为仍未独立进行 GUI 验收. 主任务另通过公开 `acceptance_online.py` 验证所有已支持写类型: 每类成功返回后立即读回均通过, 且每类完成响应的 `verification_count` 均为 `3`.

### 当前 GUI 已选素材的只读边界

`NativeAdapter::selectedItems()` 为 `items.selected` 及 Eagle SDK `getSelected()` 提供当前 selection. 它直接读取当前 material view 的 Qt selection model; 搜索命中 ID vectors, 当前行, 详情预览 ID 及历史选择均不作为替代数据源. 成功返回 `{available:true,item_ids:["40"],library_root:"...",source:"native_current_view"}`, ID 使用十进制字符串. 空选成功返回 `item_ids:[]`; 上下文或身份无法验证时返回 `available:false` 和 `code/message/library_root`, 不把不可用伪装为空选.

这条边界的冻结实现证据:

- 当前可见的唯一 `BFItemIconView` 在 `+0x30` 持有 `BFStackedView`. 实际四个布局为 `GridView`, `TabulationView`, `BFFlexibleView` 及 `BFWaterFallView`, 均继承 `BFBaseItemView`, `QListView`, `QAbstractItemView`. 读取 `QStackedWidget::currentWidget()` 确认当前布局, 不合并隐藏布局的 selection.
- `BFItemIconView +0x98` 是 native private context, `context +8` 是 data-center manager. core `0x6C032..0x6C04A` 通过该 manager 调业务虚函数. 对 PID `82624` 的被动运行时读取确认此指针为 `0x2c71cb77050`, 与 `bfright +0x4C418` 当前 owner 完全相同, vtable 为 datacenter `0x139100`, state 为 `2`. 方法要求当前视图绑定相同 manager, 并在读取前后核对 library 对象和 root 一致.
- viewmodule `BFBaseItemView::slotSelectionChanged`, RVA `0x32CB0`, 调 `selectionModel()->selectedIndexes()`. 它及 helper `0x1DB40` 使用 model role `0x164` 提取 `IDataItem*`, 调 `getItemType`; type `2` 为文件素材, type `1` 为文件夹.
- interfaces_plugin 导出 `IDataItem::getItemType`, RVA `0x3140`, 读取 `item +8`; `IDataItem::getFileInfo`, RVA `0x3110`, 返回 `item +0x20` 的 shared pointer. native selection 在 viewmodule `0x33184` 读取 file info `+8` 的素材 ID, 随后通过 `sigFileSelected(QVector<qint64>)` 提交. 实现按相同数据语义只读文件 ID, 跳过文件夹并去重.
- 入口必须在 GUI thread, 只接受现存 widget 及精确视图 class, 核对 selection/model 归属, `IDataItem*` metatype, 指针可读性和 runtime SHA-256. 新增冻结身份为 core `47abf9bf0464d88c1ca259965bbdd00165317fbf1fea8308c339c548c405c24f`, viewmodule `ad1e8e407433f8028ad7418afa485ff3a80dfabded2e4f65b4aece283ced57db`, interfaces_plugin `be89bad69ce69eb955f35de1759e2e6c934b0f082d0fda4ebe01b0524b08b408`, datamodel `4d47008537fd72ccbe63f84c87b1b822f661e1f83186d56acbae37cf3af3d244`. 文件要求 type `2` 且 vtable 精确为 datamodel `0x15A80` 的 `FileItem`; 文件夹要求 type `1` 且 vtable 精确为 `0x15A70` 的 `FolderItem`.

库切换通知 `activeLibraryChanged(QString)` 使用持久连接订阅已取证的 `IDataCenterManager::signalLibraryClosed()` 及 `signalLoadFinished(int,QString)`, 不新增轮询. 关闭时发空 root; load 完成时重读 native owner, 仅发经过验证的新 root. datacenter `slotLoadFinished`, RVA `0x1F170`, 在成功分支先把 manager state 设为 `2`, 再 emit `signalLoadFinished`; 实现不将 signal 的字符串参数猜作库路径. 该订阅独立于单个写任务的连接和 reset, 供插件 session 在切库时停止使用旧库身份.

定向静态反汇编及模块身份保存在 [native-selection-evidence.json](../task-memory/task-billfish-cli-api/native-selection-evidence.json), 可用同名 `.py` 只读重新生成; 这不是 REA provider 输出. 本机 REA 深度 provider 仍缺 Ghidra/Java 等依赖, 没有为本轮安装分析引擎. 实际 widget class/可见性及字段绑定分别保存在 [native-live-selection-objects.json](../task-memory/task-billfish-cli-api/native-live-selection-objects.json) 和 [native-live-selection-context.json](../task-memory/task-billfish-cli-api/native-live-selection-context.json).

首次统一部署的 GUI 实测确认空选返回成功, 但 `BFFlexibleView` 单选素材 `40` 被错误拒绝为 `selection_item_unavailable`. 当前进程 PID `68040` 的定向被动读取随后确认: 该 view 的 model 是 datamodel 的 `BFSortFilterProxyModel`, proxy 选中 row `5` 经 `source_rows[5]` 映射到 `BFDataModelFolder` source row `7`; 此行的 role `0x164` 返回 `FileItem` 地址 `0x1e20f057c70`, type 为 `2`, file info 的 id 为 `40`. 其 vtable 为 datamodel `0x15A80`, PE RTTI 为 `.?AVFileItem@@`; `FolderItem` 对应 `0x15A70` 及 `.?AVFolderItem@@`. 首版仅接受 interfaces_plugin/viewmodule 内的 vtable, 遗漏了实际 data model 自己的具体类, 这是已取证的拒绝原因.

修正后的 source 加入上述冻结 datamodel 身份和 type/vtable 精确配对, 并将 index 归属, metatype, 指针可读性及具体类验证拆分为可区分的失败信息. 没有移除验证或改用 proxy index 的 internal pointer 作为素材对象. 对应证据为 [native-selection-single-mapped-item.json](../task-memory/task-billfish-cli-api/native-selection-single-mapped-item.json), [native-selection-single-source-items.json](../task-memory/task-billfish-cli-api/native-selection-single-source-items.json) 及上述静态证据内的 RTTI 记录. 该修正仍待主任务下一轮统一部署后进行单选, 多选, 空选, 四布局及切库验收; 上述被动读取不替代实际 API 验收.
