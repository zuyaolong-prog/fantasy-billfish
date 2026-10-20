# Billfish 3D animation browser: requirements and independent review

本文包含功能要求及带日期的本机验收记录. `task-memory` 链接指向未随源码发布的本地 QA 材料, 公开仓库中不可访问. 当前功能概览见 [README](../../README.md#3d-models-and-animation), 当前接口约定见 [协议](protocol.md); 历史轮次中的 pending 或完成状态仅描述当时的验证范围.

## Temple大型场景, Solid/Material与自由漫游, 2026-10-20

本轮既有性能/质量测量, navigation run04及安装证据保持有效. 安装后34985 `12_Skyawlk_Bridge_UMB.glb`跳变的真人trace已确认10/10大trusted delta精确造成旋转, position/projection不变, 并非所观察事件中的culling/卸载. 最小Raw Input请求实现已安装, 真人Raw lock和效果待新gesture/实际trace复验, 不视为整体解决. 见[stability状态](../../task-memory/task-billfish-navigation-stability/task_state.md). 用户接受默认先显示Solid, 需要时切换完整Material. 普通Three viewer先加载真实几何并在首帧后ready, UI明确显示Solid; Material按需加载全部原材质和原分辨率图像, 真实解码/渲染完成才ready, 错误不伪装成功. 自动thumbnail与手动当前姿态封面必须等待完整Material, 用户切回Solid或旧viewport失效时不发布Solid封面. Core未提供该模式能力时不伪造模式状态.

UT_ART_Main.glb为1,471,271,056B, geometry-first compact BIN61,512,804B. 同源同120步相机路径/1342x819 buffer的单次受控runtime.render/render对照(不含完整native准备队列, 不是双击全链路延迟): 原完整Material打开29.0935s, p50 frame interval129.7ms(~7.7fps); 候选默认Solid打开4.5584s, interval11.7ms(~85.5fps); 候选完整Material从打开累计9.895s, interval122.9ms(~8.1fps). Solid不是完整材质同画质FPS. 完整Material移动interval仅改善约5.24%, 不宣称大幅运行提速或Blender GUI parity. 计时前source hash使OS file cache已热, 不外推cold disk或任意模型SLA.

原400图像尺寸和233材质保留, 16,496项payload checks/0diff及61,512,412 geometry bytes逐byte一致. 同相机三组RGBA: first336 pixels差异/0.03057%(仅4 pixels delta>1), path46为167/0.01519%(仅3 pixels delta>1), path89逐pixel/PNG hash一致. Baseline最终loaded-fidelity数组因harness末窗过早退出缺失, 不补造; 原始opening/movement/PNG和post-lock保持有效. Blender同源import11.215s仅是import边界, offscreen draw+sync不是GUI FPS, 不直接与Billfish计算速度比. 保守culling跳过skin/morph/动态等不安全对象, 保留原alpha/DoubleSide/depthWrite.

漫游通过可见按钮或视口内Shift+Backquote进入真Pointer Lock, WASD移动/EQ升降/Shift5x/滚轮调速/鼠标转向, Enter或左键确认, Esc或右键取消恢复. 失焦/失锁清键退出, 表单不劫持入口. 最终run04为13/13 PASS, exit0/errors[]/sourceUnchanged, 真Pointer Lock与trusted relative mouse30/20, Shift约4.93x, Enter无跳变, Esc/右键取消, Material切换保持Walk, native blur/input/dispose均通过. Run01/02的task catalog和输入时序失败为历史记录, 最终证据取代其未验证限制.

13个runtime与11个test sources已复制live, 229项manifest及独立installed hash readback PASS, 前后unified Healthy=true, native DLL仍为6E9387版本. 唯一root入口core72308上的公开preview34995实际completed/plugin_viewer_rendered/solid/idle; 安装态未再重复Material/Walk全部步骤, 以最终hash匹配候选证据覆盖. 原生准备队列计时没有精确before/after, 不把4.5584s称为native双击SLA. Normal QtQuit后重新root启动core47648正常, Maps保留; 恢复期间观察到新的原生选择34985和preview提示后停止输入, 尊重后续导航, 不声称强制恢复initial34992. 上述安装及审查属于Raw变更前的历史证据. 后续Raw请求只增加`unadjustedMovement:true`与明确错误, 无fallback/大delta过滤/敏感度或Shift/geometry/texture/culling/depth变更. 8项focused通过, Raw安装readback与229manifest/Healthy通过, 不等于真人Raw效果通过. 用户已切换并关闭全部viewer, 两次RAM热更因身份变化拒绝, 未实际热更/刷新/移动camera, 后续新窗口自然load. 第一camera snapshot与实际trace保留于stability任务, 不把run04或旧独立审查扩展为Raw完整验收.
详见[本轮task_state](../../task-memory/task-billfish-temple-performance-navigation/task_state.md), [完整Material结果](../../task-memory/task-billfish-temple-performance-navigation/benchmark-after-material/result.json), [Solid结果](../../task-memory/task-billfish-temple-performance-navigation/benchmark-after-solid-v2/result.json), [payload proof](../../task-memory/task-billfish-temple-performance-navigation/temple-progressive-payload-fidelity.json), [RGBA](../../task-memory/task-billfish-temple-performance-navigation/principal-material-pixel-comparison.json), [Blender validity](../../task-memory/task-billfish-temple-performance-navigation/blender-reference-validity.json). 本节覆盖此前普通Three viewer默认立即完整材质的行为, 其他source/动画/封面身份边界保持.

## 当前3D打开行为与验收, latest-wins大GLB反馈, 2026-10-19

本轮已安装验收: builtin viewer以最新选择为准, 旧queued丢弃、旧active viewer取消, source hash之前及late-window/show/ready均检查current intent, 避免快速反复点击后旧模型FIFO闪回. 已完成same-source复用仍验证真实fingerprint, stale request不得show/focus. 非viewer work不被抢占, 同步parse不承诺立即可中断.

Ready前可显示真实file/library/directory及checking-source / preparing-model / loading. 专用early-only gate验证343ms checking DOM, fingerprint后preparing, 839ms task gate, 890ms paint, 954ms capture; 图片显示正确Kitchen/GameExport/Cellar与准备overlay, initialize仍pending. 这是身份与阶段证据, 不是954ms首画面SLA. Resource errors拒绝ready, self/data/blob fetch支持完整embedded与data图像, HTTP/HTTPS不开放.

Kitchen原件196,088,564B, 77images/194,108,730image bytes/116,523,008pixels; 派生payload196,097,660B不是原件大小. 原4.67s geometry-only运行因231 texture errors失败, 不能作质量baseline. Corrected完整贴图before3,118.34ms, after3,819.96ms更慢, 不宣称模型自身提速. 两者完整质量, after51geometries/77textures/6programs及source/15hash/error0/drain0已核对. 移除约196MB临时buffer且old/new编码逐byte保持加9cases, 不把该收益冒充时间改善.

原生burst A34694 preparing/B35005 rendering被superseded, 仅C34402省pluginId仍正确handler/renderedwindow2, selection不变, 随后Kitchen独立公开preview rendered/window3成功. Burst2.468s不是SLA. 最终DLL `6E9387A429AD063E1B089DD82642ED67203B9592D1B134B9343ABDE740F87679` /1,662,976B, 11个运行文件/226manifest, root入口core53804, Healthy=true. GameExport原SkinStealer34676/folder1377 selected data与before完全同, 本轮Kitchen preview已真实rendered成功, 不宣称当前viewer仍打开; 不声称像素或全部treeexpansions完全相同.

[本轮 task_state](../../task-memory/task-billfish-3d-open-followup/task_state.md), [native burst](../../task-memory/task-billfish-3d-open-followup/native-burst-latest.json), [early gate](../../task-memory/task-billfish-3d-open-followup/kitchen-early-only-latest.json), [full-quality after](../../task-memory/task-billfish-3d-open-followup/kitchen-after-latest.json), [final health](../../task-memory/task-billfish-3d-open-followup/final-install-health.json), [principal final](../../task-memory/task-billfish-3d-open-followup/principal-open-review.md). Native29/UI6/runtime34/latest5/abortcover12均过, 有覆盖子集不相加, 无剩余验收项. 以下旧轮次记录保留其时点含义.

## 长动作, 时间轴与侧栏导航新请求, 2026-10-19

本轮实现, 安装与验收完成, 下方已完成轮次记录保持原时点含义. 用户新增五项: 优化长动作切换3-4s停顿; 在原时间下方增加所选速度对应的实际 current/total; 拖动时间轴暂停并保持暂停, 直到点击播放或Space; 左侧栏始终提示当前目录; 侧栏填满/overflow时从中间子目录导航避免整列跳动. 最终性能after, transport/sidebar验收, 组合安装及Healthy均已完成, 文档与生产source已freeze.

实际时间显示为 source current/speed 与 source total/speed, 保留原始时间和帧语义. 2x显示一半, 0.5x显示两倍. 拖动开始即pause, seek后及释放/取消仍保持paused, 显式播放或时间轴焦点下Space才继续; 时间轴保持焦点不应阻止thumb跟随. 不劫持搜索/数值输入框中的Space.

侧栏当前项提示使用真实model index selection/currentIndex, 避免复用delegate的stale selected状态. Overflow导航候选在expand后映射真实flattened row并用 `ListView.Contain`, 已可见目标不移动, 屏外目标最小揭示; 不是重排目录顺序, 不改普通scrollbar handlers. Sidebar final normal-cache实机验收, 最后组合安装及health均通过.

[本轮 task_state](../../task-memory/task-billfish-long-clips-transport-sidebar/task_state.md), [长动作before](../../task-memory/task-billfish-long-clips-transport-sidebar/long-before-latest.json), [transport candidate](../../task-memory/task-billfish-long-clips-transport-sidebar/transport-candidate.json), [sidebar实现](../../task-memory/task-billfish-long-clips-transport-sidebar/sidebar-evidence/implementation.md) 记录当前进展. 既有短动作性能结果不能替代此次长动作before/after.

Before真实DarkAbbess parent范围69候选/68动作: long Idle41.533s/726tracks/3,619,110 keyframes, first-frame cold13.882s, warm3.936s, A-B-A重返4.402s, sequence-long4.038s. 各次clip请求无新增scan, 不以scan解释此停顿. 这些是受控单次before, after已取得下方最终测量, 不将单样本外推为普适提速.

### 本轮最终测量与验收

相同41.533s Idle首pose: cold13.882->4.325s, warm3.936->0.405s, A-B-A4.402->0.394s, sequence-long4.038->0.346s; medium2.890->0.937s, short0.780->0.581s. 首次cold仍需4.325s, 不宣称所有首次小于1s. 这是相同三clips的单次before/after, 不是均值或任意资产上限. Final12 production start/end hashes相同, source unchanged, host/renderer errors=0, nativeWrites=0, drained=0. 两次scan仅current/parent, clips均0rescan.

最终parser3 SHA `F515154FC4F1480136AFEFB7ECCDCAF30B2E2861B24A8AC105D9EB97B374DE3B`, source hash prefix90A8, BFCLIP1 Float64与small JSON response替代重复数值JSON, 18,105,230 doubles逐项全等. 真实production app/ClipPlayer长after与transport DOM证据分开: DOM Three/Core使用实际transport handlers及test-only viewport shim, 两者均PASS, 包括2x实际6/15, 0.5x重置后60, pointercancel/Space/焦点跟随.

Sidebar最终normal-cache候选569c已实机验证, QML resource mtime修复旧缓存问题; 此前命中old cached QML的现象不能判候选失败. No-cache真实展开保持40目录y600/动画y645锚点不动; normal-cache恢复、center素材选中仍保持正确hint, root清理旧hint通过. Offscreen AI绘图从底部外导航后仅显露到视口底部, 不置顶. Tag区域GUI未测试, 仅既有静态互斥review PASS, 不宣称tag GUI验收. 用户材质folder8716/item75986已恢复. 最后组合安装与Healthy均已验证, 详见下节.

### 最终安装, 数据与现场恢复

本轮完成, source与文档冻结, 剩余工作无. Root通过Qt正常退出后从唯一 `E:/Billfish/Billfish.exe` 启动core69660. Parser3 `F515154FC4F1480136AFEFB7ECCDCAF30B2E2861B24A8AC105D9EB97B374DE3B` 与source/provenance `90A89F11D31147FAEF45A9BE466501F40F2B672EB7C713F90A2A02563E29424D` 已安装工具目录; sidebar `569C3DFD752961BF69269FADB491569F445B86D1E585511DDF0EDC13F36DAC1A` 已安装, 实际loaded module path确认, normal QML cache无生产override. 226项manifest及最终Healthy=true, installed plugins.list actual ok, native preview保持418F版本.

严格BFCLIP1真实pose gate通过两条旧真实动作, 每条318 tracks / 530 joint matrices / 3,070 vertices / 5采样时刻含off-key, main源码和exe起止hash保持. 18,105,230 doubles完整相等与长clip渲染/transport证据共同验证, 不使用降采样/丢轨换速度.

最终恢复 `X:/BaiduSyncdisk/ZZ/游戏设计资料`, 材质folder8716/item75986, selected data与initial逐JSON完全一致; Waterfall, 空query, 双sidebars/filter, slider332, 显示子文件夹素材=true及四个原树expansions均恢复. Final截图已view_image检查, 当前材质灰hint可见. 本轮用户原生3D viewer未打开, 实际production Three渲染由hidden Electron长before/after验收, transport native输入由真实handlers加viewport shim验收; 不声称原生viewer UI又复测一遍. Tag GUI仍未测, 静态互斥review边界保留.

最终证据: [installed receipt](../../task-memory/task-billfish-long-clips-transport-sidebar/installed-receipt.json), [Healthy](../../task-memory/task-billfish-long-clips-transport-sidebar/final-install-health.json), [final UI](../../task-memory/task-billfish-long-clips-transport-sidebar/final-ui.png), [strict real pose](../../task-memory/task-billfish-long-clips-transport-sidebar/parser3-real-gate/correctness.json).

## 动画延迟与多组 skin weights 候选, 2026-10-19

本轮实现与验收完成, 下方旧轮次记录保持原时点含义. 用户报告动画双击反复显示 scan/进入播放慢, 以及 DarkAbbess 超过四权重时报错. Final after, GPU run2及两份principal review已通过, parser2/renderer/performance已统一安装, native33726与最后实际外部动作检查均通过, 本轮已freeze.

候选 skin 边界: static 和 <=4 influences 保持原路径; 多组 `JOINTS_n` / `WEIGHTS_n` 要完整保留并按全部 weights 做 LBS, 不截为 top four. Renderer 须一致覆盖 GPU position/normal/tangent 与 CPU skin/bounds, 防止共享 WEIGHTS accessor 的首组归一化破坏比例. 依据实际 context 的 vertex attribute budget 决定可加载性, 超限明确报错, 不默默丢组. 非零 DQ 仍拒绝, 不将 BLENDED_DQ_LINEAR 的 method enum 本身当作实际 DQ 权重为零的证据.

DarkAbbess 实测 68,167 control points, 213 skin clusters, 最大5权重, 3 control points / 4 exported corners 有第五个正权重, 丢 top-four 之外的最大质量约0.00784314. 实际 per-vertex 与 raw sparse DQ 均为0, 因而此源可按 LBS 候选处理. Parser `billfish-ufbx-2` 已 build/run PASS并正式安装tools; 真实 GPU run2与shared accessor修复已取得下方最终候选证据, 不仅以 parser 输出成功证明可正确显示.

可更新证据入口: [本轮 task_state](../../task-memory/task-billfish-animation-latency-skin/task_state.md), [skin/DQ measurement](../../task-memory/task-billfish-animation-latency-skin/skin-influence-dq-measurement.json), [skin design review](../../task-memory/task-billfish-animation-latency-skin/skin-review/principal-review.md). 历史文档中大于四 influences 或 secondary sets 一律 unsupported 的描述仅属于旧 deployed parser/renderer 组合, 本轮候选解除该限制仍以 paired parser+renderer 验证及正式部署为准.

Before baseline 为71候选的受控真实 Putrid 流程: model ready 1.014s, 列表可选9.522s, 首clip1.149s, 同clip重复0.549s, 双击0.643s/2 IPC, warm open2.735s; 真实 scan 共3次(cold/30s/warm), 独立同clip无scan. 这些是 measured baseline, 不推断所有卡顿都由每次动作全库scan导致. Final after与synthetic seek/morph/overlay已有下方候选证据, 安装后native33726打开与最后外部动作实件检查均已PASS.

### 本轮最终候选验证与已安装状态

Parser CLI 11 tests PASS; verify-real两条旧<=4真实动作各318 tracks, 在5时刻/530 matrices/3,070 vertices对照PASS, source unchanged. Pinned GLTFLoader Node 6 cases PASS. Shared WEIGHTS accessor错误35/65已修为全组rawsnapshot并复验50/50.

Skin GPU final run2与principal最终review PASS. 真DarkAbbess四个五权重corners完整匹配独立ufbx probe全部weights, rest position最大误差1.0571868e-5, test-only bone180 perturbation最大1.6447371e-5, normal/tangent误差<1e-7. Synthetic实际ClipPlayer seek0.7直接验证morph=0.7及第五joint quaternion.y=sin(0.35), overlay-off pose PNG发生变化. Resources=0/0/0, context lost及canvas detach, harness/production hashes保持. 真Dark源embedded clips=0, 不声称其原素材动画播放/seek, 17个缺失贴图未还原.

Performance final after clean PASS: 首可选0.799s, 完整coldscan9.515s, 首clip0.869s, repeat0.298s, switch0.818s, double0.267s/1IPC, warm2.131s. Baseline首可选9.522s、完整scan约9.522s, 本轮把可选结果提前并避免无变更重扫, 完整coldscan耗时基本持平, 不称全扫描大幅提速. 实际仅1次coldscan, unchanged periodic/warm均0scan;7个production inputs start/end同hash, source unchanged, nativeWrites=0, drain残留0, stderr为空, exit0. 早期after-run1因TypeError且mid-run源码变化, 仅历史候选观察, 不作为最终结果.

性能对照是单次样本, baseline首可选由first rows推得, final首可选为真实DOM可选且不含clipdecode, 不报告均值或普适提速比例. 两个chosen IDs相同且priorClipRequests均0. 对照固定旧parser1以隔离skin变更, 不代表部署后仍使用parser1. 同viewer重复open14.4ms; warm rows803.5->593.7ms, warm same549->271ms, warm double650->297ms. Performance源码冻结且principal latency review无代码阻断, focused26 PASS. Service SHA `6822DB4B4975E2DFA0B34A1E47674CC59FA1D6F37E9892D3D0BD486734F18699`, app SHA `27A0A71E9F1F3C8B713D30F3112AA0AE16081D7D589E54718C5CB89D57558255`. Parser2/renderer/performance已安装, native33726真实打开与外部动作实件检查均通过, 详见下方安装与最终实证.

候选最终证据: [performance after-final](../../task-memory/task-billfish-animation-latency-skin/after-final-latest.json), [skin receipt](../../task-memory/task-billfish-animation-latency-skin/skin-implementation-receipt.json), [GPU final](../../task-memory/task-billfish-animation-latency-skin/gpu-proof-final/result.json), [skin principal](../../task-memory/task-billfish-animation-latency-skin/skin-review/principal-review.md), [latency principal](../../task-memory/task-billfish-animation-latency-skin/principal-latency-review.md).

### 安装与原生打开已验证

已通过Qt正常退出旧app, old core及全部Billfish children为0, 再安装parser2及同步provenance并刷新224项manifest. 安装parser SHA `84DB7565F2C65B9D16EBEE3DC780AECC01B6012E97F5B4D008416EBE71A5E820`, native preview DLL保持 `418F04B8DD796F6158E5BC4DA25F8986FA7F0D92820C887D5FFE132560AB63D2`. 启动前Healthy=true, 唯一Billfish.exe启动后core66024.

GameExport原DarkAbbess/FBX目录树, 双侧栏Waterfall与空搜索保留, Qt真实双击33726成功dispatch. Native job `91d9c9d8-39fb-450f-a4d7-10e8642de5f1` completed / plugin_viewer_rendered / window2. 原生截图已看到Dark白模自动封面, cover_tid=10, `.bf/.preview/be/33726.cover.png` 150,233 bytes. 源SHA `D3E430D6C8FD73B0867F8EF5D2C750466A68D4E906501730E82FDF85628FC11A` 保持, protected metadata无变化, 224 manifest无漂移. 这不表示缺失17贴图已还原.

主应用与Dark viewer保持打开, 原生选择已恢复33726, 无modal. Hidden实际Dark父层SKL_DarkAbbess外部动作play/seek/skeleton最终验收已PASS, 详见下节, 本轮验收已freeze.

安装证据: [receipt](../../task-memory/task-billfish-animation-latency-skin/install-receipt.json), [native job](../../task-memory/task-billfish-animation-latency-skin/native-dark-viewer-job.json), [verification](../../task-memory/task-billfish-animation-latency-skin/installed-runtime-verification.json).

### 安装后 DarkAbbess 外部动作实件验收完成

[Dark installed result索引](../../task-memory/task-billfish-animation-latency-skin/dark-installed-latest.json) 指向 `measurements/dark-installed-da5ca896-19bc-4300-b787-39a198de71b5/result.json`. Hidden实际production UI与installed parser2初始current范围1 candidate / 0 clips; actual DOM勾选parent后仅取DarkAbbess范围, 69 candidates / 68 full外部动作. 实际选item33739 `AS_AbbessB_FlinchLeft`, duration1.033s, play time0.067->0.15, seek0.34444, skeleton on/off实际pixels不同. `03-clip-skeleton-on.png` 已由root和awaiter检查.

当前姿态capture为202,891 bytes, clip/time准确, 仅task callback存PNG, nativePublished=false, 不声称此次发布了原生动画封面. 本实件共2 scans, 分别为current与显式parent范围; repeat/double各1IPC且0 new scans, host/renderer/IPC errors为空, source与7 production hashes保持, nativeWrites=0, normal drain=0, exit0, 用时20.42s. 这是实际外部配套动作验证, 原Mesh仍0 embedded clips.

本轮实现/候选验证/正式安装/native打开与最后实件验收完成, 无剩余实施或验收项. 最终用户现场: core66024主应用保持GameExport/DarkAbbess/FBX, 原生选择33726, viewer window2保持打开, 空query, 无modal. Parent checkbox只用于hidden QA, 未更改用户当前viewer范围. 全部生产source已冻结.

## 2026-10-19 候选目录范围修正

本节取代下方旧的全库候选范围说明. 默认只读取模型文件所在目录及其全部子目录; UI 的 `搜索上一级目录` 选项只扩大一层, 覆盖平级动作目录, 不继续向祖先上溯, 不越过库根. 每个新模型 session 默认 current, 同一已打开 viewer 复用保留本窗口选择, 不写 global 范围偏好. 不要求目录名是 FBX/Anim, 不按模型与动作 basename 或 SK_/AS_ 前后缀过滤; 范围内候选继续用实际骨架兼容性确认. 自身 embedded clips 保留, 无匹配动作时显示空列表, 无全库 fallback.

范围在 Python scoped SQL 枚举前限定, 不是 UI 搜索过滤. Helper schema 2 必须带 `scope_root`; backend 使用 `sessionSchema:5` / `billfish-model-subtree-1`, cache key 含 current/parent 及具体目录. 旧全库关联不恢复, scope 外 rows 不能进入解析、播放、隐藏或 scan counts. Parent 仅是当前窗口的明确扩展, 不在新 current session 或无 viewer 启动时恢复兄弟目录隐藏状态. Metadata/model/clip 内容缓存仍可按有效 fingerprint 复用, 既有用户坐标和关联 profile history 保留.

源码与 focused 验证已完成; 本增量的运行中 session 重启和最终实机验收由主线程完成. 证据见 [范围验证记录](../../task-memory/task-billfish-preview-capture-followup/animation-scope/implementation.md), 最新接口见 [protocol](protocol.md). 下方跨全库文件夹的旧实机截图仅是历史 baseline, 不证明新的目录范围.

## 显示骨架补充, 2026-10-19

有真实 skin skeleton 的模型加载后显示 `显示骨架` checkbox, 每次加载默认关闭. 只有实际 `SkinnedMesh.skeleton.bones` 才使开关可用, 静态模型和未被 skin 使用的 Bone-only hierarchy 不显示该控制. FBX/GLB 的 Three viewport 与 Core 实际 skinned 模型使用同一开关语义.

开启后显示当前骨骼连线与关节点, 随播放和 seek 更新; 多根与单关节 skin 保留实际关节点, 不编造父子连线. 关闭即从 scene 移除显示, 不改原文件, skin, 材质或动画. Helper 独立于模型层级, viewport dispose 释放其资源.

骨架显示 renderer/host 实现已冻结. [Renderer implementation](../../task-memory/task-billfish-gameexport-skeleton/renderer-implementation.md), [Core host contract](../../task-memory/task-billfish-gameexport-skeleton/host-core-skeleton-implementation.json), [实际 Core production proof](../../task-memory/task-billfish-gameexport-skeleton/core-skeleton-poc/production/result.json). Focused 13 tests 与 Core facade/UI 15 tests PASS; 实际两骨骼 skinned GLTF 的 Core POC 和 production renderer smoke PASS. Production proof 使用 task-only synthetic source, 不等同于 native imported item 的整体验收. 本轮 GameExport 漏 FBX 原因与修复仍由 root 调查, 此处不提前给结论或宣称整体任务完成.

## 当前需求与验收, library-only refinement

更新日期: 2026-10-19. 本节是当前行为的验收依据, 优先于下方旧轮次记录. 旧的 F:/TempFBX 125 动作, 目录 chooser, 多 viewer 和三 viewer 验收仅保留为历史 benchmark; 它们不是当前主要用户流程, 也不证明本轮已完成. 本轮状态: FBX/GLB library-only 候选已安装并通过针对性验证; 原 Core 格式已接入同一维护插件, 旧普通 3D 插件已禁用并归档移出扫描目录. 注册验证为单个 enabled `billfish.animation-browser`, 名称 `3D 预览`, 保留原全部 25 viewer / 23 thumbnail extensions, HDR/EXR 仅 viewer. OBJ/GLTF production smoke run 4, 3dm WASM targeted run 5 和 reference pose run 2 PASS; 不宣称逐一实测了全部 25 格式. 原生 OBJ orbit/封面, GLTF 动画/seek/当前帧封面, FBX/GLB 单窗口和真实切库返回均已通过. 最终 source/protected metadata 与最新 cover 保持, 主应用保持打开于 QA 库, viewer 已关闭, 原生搜索为空.

| 当前要求 | 当前证据与边界 |
| --- | --- |
| 保持插件弹窗入口 | Billfish 原生素材双击继续打开 Billfish-owned Electron 插件窗口, 不嵌入主窗页面. 真实 FBX item 60 与 GLB item 51 已双击打开 |
| 候选只来自当前 Billfish 库 | `animation.catalog_snapshot` 在单个 SQLite read-only transaction 内取得完整非回收 FBX/GLB 成员, 跨原生文件夹, 包含隐藏成员; 不递归枚举磁盘邻居, 不用旧 external scope fallback. 15 项 Python catalog tests PASS, 34 项 backend tests PASS |
| 当前库内自动关联 | item 60 的骨架 Mesh 自动匹配另一文件夹 item 61 的 B_AtkBeta01_2 并播放, 实机 9 rows / 12 indexed candidates, 坏 item 57 保留诊断. [实机截图](../../task-memory/task-billfish-animation-browser/library-mode-cross-folder-viewer.png), [快照](../../task-memory/task-billfish-animation-browser/host/library-mode-cross-folder.snapshot.json) |
| 统一常驻侧栏与空列表 | 普通模型和带动画模型使用同一 UI. 静态 GLB item 51 显示真实蓝色 cube, 动画列表为空, 播放/seek/frame controls disabled, 相机与封面能力保留. [静态模型](../../task-memory/task-billfish-animation-browser/library-mode-static-empty.png). 最新 UI 专项 7 tests PASS |
| 一个可见 3D 预览窗口 | 同一 item 在 path/hash/bytes/mtime 不变且已真实完成时复用; 不同 item 或 source 变化先关闭旧 viewer/session 再打开. item 60 重开复用同 HWND, 切 item 51 后仅余新窗. [同项重开](../../task-memory/task-billfish-animation-browser/library-mode-same-item-reopen.json). thumbnail 不占用或关闭 viewer slot |
| 保留已有用户配置 | Maya/Max/custom, joint basis, facing, manual associations, file overrides 和 immutable profile history 保留. 旧 `profile.scope` 从有效配置排除, 不再用于发现; schema 3 的旧衍生关联不得 warm restore. 当前 `sessionSchema:4`, `candidatePolicy:billfish-catalog-1`, rows 带 `sourceItemId` 和 `origin` |
| 当前库身份必须持续有效 | 每次恢复/scan/load 重验 catalog item ID 与 owned path; source 移出/回收/不可用时剪除旧 rows, 不继续依据旧 scope 播放或隐藏. host catalog/scope/single-viewer 专项与独立审查 PASS |
| 保留原 Core 格式并移除旧普通 3D 插件 | 原 Core 格式已接入同一维护插件, 旧普通 3D 插件已禁用并归档移出扫描目录. 注册验证为单个 enabled `billfish.animation-browser`, 名称 `3D 预览`, 保留原全部 25 viewer / 23 thumbnail extensions, HDR/EXR 仅 viewer. OBJ/GLTF production smoke run 4, 3dm WASM targeted run 5 和 reference pose run 2 PASS; 不宣称逐一实测了全部 25 格式. 原生 OBJ orbit/封面, GLTF 动画/seek/当前帧封面, FBX/GLB 单窗口和真实切库返回均已通过. 最终 source/protected metadata 与最新 cover 保持, 主应用保持打开于 QA 库, viewer 已关闭, 原生搜索为空. [Core freeze](../../task-memory/task-billfish-library-animation-mode/host/core-freeze.json), [旧插件移除](../../task-memory/task-billfish-library-animation-mode/old-plugin-removal.json), [注册验证](../../task-memory/task-billfish-library-animation-mode/unified-registration-verification.json) |
| 默认只展示完全兼容动画 | 每次打开默认 full, 手动 all/partial filters 仍可使用. 最新 UI 专项 7 tests PASS |
| 最终生命周期和数据保护 | 已完成本轮原生 E2E, 切库返回和偏好保留, preserved-data-verification ok=true. 主应用保持打开于 QA 库, 搜索为空, viewer 已关闭. 本轮最终实证如下, 下方 production5 数据保持旧轮次含义 |

本轮不把直接复制到磁盘的 QA 邻居当作未索引负例: Billfish 自身 watcher 已把它导入为 item 62. 该观察证明 catalog 增量, 不是未注册邻居负例 PASS. 未注册邻居排除由受控 catalog 组件用例覆盖. 本轮证据入口: [library-only task_state](../../task-memory/task-billfish-library-animation-mode/task_state.md).

当前库隐藏成员也已完成本轮实机往返: 精确搜索 `Library-Cross-Folder-Action.fbx`, 默认 enabled=true/showAll=false 时 native enabled modelitems=0; viewer settings 启用 showAll 后为 1, `items.selected` 读回准确 item 61; 取消 showAll 后恢复 0. 动画 61 在 item 60 viewer 仍可播放, 证明隐藏成员参与候选而非成员删除. [隐藏](../../task-memory/task-billfish-library-animation-mode/native-hidden-61.json), [显示全部](../../task-memory/task-billfish-library-animation-mode/native-show-all-61.json), [实际选区](../../task-memory/task-billfish-library-animation-mode/library-showall-selected-result.json), [恢复隐藏](../../task-memory/task-billfish-library-animation-mode/native-rehidden-61.json). 最终开关已恢复 enabled=true/showAll=false.

### 本轮最终原生验收与现场

- 原生双击 OBJ item 50 使用统一 UI, 0 动画/full filter; GUI orbit 显示完整立方体, 封面 UI 成功且 native cover_tid=10. [Orbit](../../task-memory/task-billfish-animation-browser/final-unified-obj-orbit.png), [封面](../../task-memory/task-billfish-animation-browser/final-unified-obj-cover.png), [快照](../../task-memory/task-billfish-animation-browser/host/final-unified-obj-covered.snapshot.json).
- GLTF item 63 有 4 动画, autoplay 后切 Clip2 旋转, pause/seek 至 0.605s 并成功写当前帧 native cover. [Seek](../../task-memory/task-billfish-animation-browser/final-unified-gltf-seek.png), [封面](../../task-memory/task-billfish-animation-browser/final-unified-gltf-cover.png), [快照](../../task-memory/task-billfish-animation-browser/host/final-unified-gltf-covered.snapshot.json).
- FBX item 60 默认仅显示 full 的 4/9 rows, 跨文件夹 item 61 播放; 重开同 item 复用 HWND 104534950, 切 GLB 51 后仅余 HWND 304944444 且 0 动画. [FBX](../../task-memory/task-billfish-animation-browser/final-unified-fbx.png), [复用快照](../../task-memory/task-billfish-animation-browser/host/final-unified-fbx-reused.snapshot.json), [静态 GLB](../../task-memory/task-billfish-animation-browser/final-unified-static-glb.png).
- 实际切至 `E:/Temp/Billfish-Video-New-Folder-QA`, 旧 host 50188 与其窗口全部退出; 返回 QA 后 item 55 由新 host 15648 正常打开, 4/9 rows / 12 catalog, 唯一 1 error 为故意 bad 57 fixture. Maya / +Y up / +Z front / +X primary / +Y secondary 和 global hide=true/showAll=false 保留. [另库](../../task-memory/task-billfish-animation-browser/host/final-unified-other-ready.snapshot.json), [返回角色](../../task-memory/task-billfish-animation-browser/final-unified-return-character.png), [设置](../../task-memory/task-billfish-animation-browser/final-unified-return-settings.png).
- [保护数据核对](../../task-memory/task-billfish-library-animation-mode/preserved-data-verification.json) ok=true: items 55/56 的 source, protected metadata 和 latest cover 均保持. Item 55 先前在用户活跃期间已有 cover 变化, 本轮保留最终重启前最新 baseline, 不宣称相对旧轮次全程 cover 未变. 当前主应用保持打开于 QA 库, 原生搜索为空. Viewer 在用户活动期间已无该 HWND, 不归因为 root 正常关闭, 也不宣称主应用已正常退出.

本轮验收完成. Runtime fingerprints 为 222 项, native DLL 未改; [最终统一健康](../../task-memory/task-billfish-library-animation-mode/final-health.json) 为 `Healthy=true`, manifest 222 项且 native DLL unchanged. 全部格式的注册保持与代表格式实测分开记录.

## 历史记录边界

以下章节为此前完整动画浏览器交付的审查和验收记录. 当其中出现外部动画目录, `scope.set`, chooser, 同时多个 viewer 或其他格式仍走旧插件时, 仅描述旧轮次行为, 以本节及 [protocol.md](protocol.md) 当前协议为准.

审查日期: 2026-10-19, Asia/Shanghai. 本文记录需求, 独立审查及最终安装版验收. 当前结论以第 2 节矩阵和第 9 节最终证据为准; 早期审查保留用于解释问题与修复, 不代表仍有相同阻塞. 依据用户完整粘贴计划, 交互示意图, 崩溃修复要求和后续 Maya/Max 轴向持久化要求. 用户允许更正技术方案, 不等于允许静默减少最终功能. 阶段提交建议服从当前工作流, 本任务不自动 stage/commit/push.

## 1. 初始审计证据与结论

本节记录实施前基线. 后续 backend 的当前实证见第 7 节, 不以本节的早期缺口否认已发生的实现进展.

- 本次统一状态检查 `Healthy=true`. 这只证明已安装 launcher/DPI/HDR/AV1/VideoTrim 等基线, 不证明 3D 崩溃已修复.
- `automation/plugin_runtime/preview-runtime.cjs` 已有 Electron BrowserWindow, 真实源文件加载, renderer 退出/加载错误处理和 native owner 接入. `preview-preload.cjs` 通过现有 3D Core 的 `preview.loadFile()` 观察结果, 并验证截图. 这可复用为窗口与完成语义, 不能证明骨架关联正确.
- `animation-controller.cjs` 接受当前 preview 的 `animations/animationActions/animationMixer`, 只控制该文件已加载的 clips. `animation-controls.cjs` 的 select 列表同样来自这个数组. 当前没有外部数百文件发现, 持久关联和按需解码的证据.
- `preview-cover.cjs` 已有 source fingerprint, pose, PNG staging 和 native cover 确认边界. 外部动画封面还需记录动画文件/clip 身份及轴向配置版本, 仅 `clip_index` 无法长期定位外部动作.
- `docs/eagle-plugin-compatibility.md` 记录现有 Electron runtime 和若干格式/封面历史实测; 历史通过不能覆盖本轮用户报告的当前崩溃, 也不能覆盖新外部动画系统.
- 用户示意图表达角色 Mesh 卡片和独立动画列表, 明确不是现有功能截图. 卡片下动画数量可作为呈现参考, 不能以静态示意图替代功能.

当前推荐: 保留 Electron 宿主, native preview/cover 入口和已验证的视口能力. 新增可维护的动画发现/规范化/匹配/缓存层, 使用统一播放接口. ufbx 是 FBX 正确求值的优先候选, 但解析器和实际 renderer 必须验证同一变换语义, 不能把两个 importer 的骨架对象按名字拼接就认定正确. 原生可恢复隐藏是最终目标的一部分, 必须完成真实 model/filter 边界调查与集成.

## 2. 需求与验收矩阵

矩阵状态于 2026-10-19 03:14 按已读回的 production4/production5 证据更新. "组件通过", "诊断通过" 和 "安装版通过" 区分各自证据范围. 原生四布局, 首开, 封面, 重启恢复, 增量, cache 重建, 真实切库与 host 去重修复后的三 viewer 单项显示/恢复均已通过. 功能验收与最终环境恢复已闭合, 最终保护数据/正常退出/Healthy 见第 9 节; root 统一 task_state 与交付文档. 已通过输入不重复测试, 未覆盖格式和环境边界继续保留.

| ID | 最终要求 | 证明完成所需证据 | 当前审查状态 |
| --- | --- | --- | --- |
| C01 | 打开 3D 模型不再崩溃 | 当前失败资产/入口的复现, 根因证据, 修复后同入口反复打开关闭; 失败文件返回错误且宿主可继续使用 | 安装版通过真实 FBX/GLB 打开, 坏 GLB 受控报 Invalid GLB header 后宿主与已有窗口可继续, 正常退出重启与切库后重开正常; [坏文件实机](../../task-memory/task-billfish-animation-browser/native/production-corrupt-error.png) / [重开](../../task-memory/task-billfish-animation-browser/production5-library-return-viewer.png). 缺图误判 fatal 的具体原因已修; 不宣称证明未知 native 异常根因 |
| A01 | 正式改核心索引/显示前架构审计 | `architecture-audit.md` 明确 native/Qt, 数据库, 索引, 双击, 缩略图, watcher, 任务队列, 扩展边界和不可知部分 | 已交付并读回 [architecture-audit.md](architecture-audit.md); 最终 BFDataModelFilter 与 cover 路线由第 9 节 production 实证闭合, 早期候选失败保留为历史 |
| A02 | 实施方案和变更/迁移方案 | `implementation-plan.md` 列实际文件, 模块, 存储, 阶段 gate, 风险, 回滚; 不假定官方 API | 已交付并读回 [implementation-plan.md](implementation-plan.md); 已安装 binary 与 Healthy 见第 9 节, root 最终文档统一部署/恢复记录 |
| D01 | FBX 为主, GLB 兼容, 按实际内容分类 | 真实 Mesh+skin, 动画-only, Mesh+animation, 多 clip, 无动画, 坏文件的解析结果; 不按扩展名/名字判断 | 组件通过: 126 真实 FBX 分类/空 take/错误输入, GLB Mesh/skin/动画-only fixtures; [parser](../../task-memory/task-billfish-animation-browser/parser/implementation-state.md) / [backend](../../task-memory/task-billfish-animation-browser/backend/implementation-notes.md); 按已声明格式限制交付 |
| D02 | 父目录递归范围, 后台和取消 | 嵌套目录发现正确, 不向上跨角色搜索, 不可访问目录报告, 取消后不再提交新结果, UI 可操作; copy 导入丢失源邻接时可选择动画目录并持久化 | 组件递归/取消/生命周期专项通过; 安装版实际 chooser 选择 F:/TempFBX 后列 125 动作, 重启及切库恢复同 scope; [warm scope](../../task-memory/task-billfish-animation-browser/production4-fbx-warm.png) / [切库返回](../../task-memory/task-billfish-animation-browser/production5-library-return-viewer.png) |
| D03 | 任意骨骼拓扑, 多根/大骨架 | 人形, 四足, 多足/非标准, 多根, 大骨架的真实或标明来源的文件播放; 不设 Humanoid 或 200 骨限制 | 样例覆盖通过: 人形/四足/多足/多根/256-bone 的真实 WebGL smoke, 标明合成; [viewer](../../task-memory/task-billfish-animation-browser/viewer/implementation-and-validation.md); 不再要求重复同一测试 |
| D04 | 同一文件多 clip 可选 | FBX takes/GLB animations 数量与名称完整, 重名/空名通过稳定 ID 区分, 不只列文件名 | 已实现稳定 clip ID, GLB 重名/空名/多 clip 已实际列出切换; [quadruped 9 rows](../../task-memory/task-billfish-animation-browser/viewer/quadruped-offscreen/result.json); FBX takes 枚举由 parser 实现, 不宣称额外多-take FBX 实测 |
| M01 | 名称+拓扑+轨道+参考变换匹配 | 同名异父, namespace 碰撞, 缺失轨道, 无歧义 namespace 归一, 不同 bind/local basis 的正反例; 报告给出原因 | 组件通过: 名称歧义/父级/basis/已知 bind 差异反例, 125 真实 FBX 关联; [backend](../../task-memory/task-billfish-animation-browser/backend/implementation-notes.md) / [audit regressions](../../task-memory/task-billfish-animation-browser/backend/audit-regressions.log) |
| M02 | 完全/部分/不兼容三态 | 兼容允许播放; 部分明确缺失/差异并可预览; 不兼容不自动关联/隐藏, 可查看报告 | 组件三态/partial 确认与反例通过; 安装版 GLB 自带 clips 为 full, FBX 不兼容项列明且不能自动播放; [真实三态展示范围](../../task-memory/task-billfish-animation-browser/production4-glb-first-open.png) / [匹配报告复制](../../task-memory/task-billfish-animation-browser/host/production5-copied-report.json). partial 专项仍按组件证据, 不冒称另做 production partial 样本 |
| M03 | 多 Mesh 明确归属, 人工指定 | 同目录不同骨架不抢占; 同骨架多个 mesh 可关联多个目标或按用户选择, 歧义可见且持久化 | 每 Mesh 独立 profile/映射, 无 first-Mesh 分配; 55 保持 F scope, 58 保持 QA scope 并独立增量播放, 切库不串 profiles; [58 独立目标](../../task-memory/task-billfish-animation-browser/production5-increment-playing.png) / [库隔离](../../task-memory/task-billfish-animation-browser/host/production5-library-lifecycle-verification.json). 单项显示/恢复也已在三 viewer 场景通过, 见 H02 |
| X01 | Maya 默认与 Max/自定义轴向 | UI 明确区分源场景坐标与骨骼局部朝向; 可选择, 检查和恢复, 缺数据默认 Maya; 已有文件信息不会被无提示覆盖 | 组件/UI smoke 通过: auto 检测与 Maya 默认偏好分开, Max/custom/单位契约及设置回显; [CLI axes](../../task-memory/task-billfish-animation-browser/parser/cli-contract/summary.json) / [settings](../../task-memory/task-billfish-animation-browser/viewer/settings-gate/result.json) |
| X02 | 选择按骨架及动画持久保存 | 同角色重开/重启恢复; 切换其他角色不串配置; 动画可继承角色来源预设并单独覆盖; 新 revision 使受影响匹配/转换缓存失效 | 安装版实际重启及切库返回恢复 F scope, Maya, joint primary +X/secondary +Y; [重启恢复](../../task-memory/task-billfish-animation-browser/production4-settings-restored.png) / [切库恢复](../../task-memory/task-billfish-animation-browser/production5-return-settings.png). 文件 override/远端 revision/冲突保持既有组件证据; [缓存重建保留 3 组配置](../../task-memory/task-billfish-animation-browser/host/production5-cache-profile-verification.json) |
| X03 | 正确变换与单位, 无重复转轴 | Maya/Max 等价导出对比; Mesh, inverse bind, root motion 同步; current Y-up + OriginalUpAxis Z-up + root pre-rotation 样本无二次旋转 | 组件通过: auto/Maya 与 Max/custom 对照, current/original 轴分离, 两真实动作含 off-key 序列化动态 skin gate; [正确性](../../task-memory/task-billfish-animation-browser/parser/real-gate/correctness.json); 这是采样误差证明 |
| P01 | 双击 Mesh 进入真实内置动画浏览器 | 从 Billfish 原生网格双击实际素材, owner/关闭/库切换行为正确; 无外部独立演示入口替代 | 安装版通过原生双击 FBX/GLB, native owner, 多窗口单独关闭与真实库切换; [GLB 首开](../../task-memory/task-billfish-animation-browser/production4-glb-first-open.png) / [库切换生命周期](../../task-memory/task-billfish-animation-browser/host/production5-library-lifecycle-verification.json). 不是独立 demo 入口 |
| P02 | Mesh/材质/贴图与相机 | 实际完整模型, 可解析材质贴图, 合理构图, 旋转/平移/缩放/重置相机; 缺图不崩溃并有诊断 | 组件材质/缺图诊断与构图通过; 安装版实际完整 FBX 和 GLB 画面通过; [FBX](../../task-memory/task-billfish-animation-browser/production4-fbx-warm.png) / [GLB](../../task-memory/task-billfish-animation-browser/production4-glb-first-open.png). 相机能力按 viewer 实现/smoke, 不冒称每个手势都重复实机点击 |
| P03 | 控制与播放结束 | 播放/暂停/循环/seek/0.25,0.5,1,2x/前后动作/键盘切换; 单次结束按明确策略回初始状态或用户选择循环 | 组件 transport/速度/once/seek 等语义通过; 安装版切换真实外部动作, pause/seek 至 1.504 seconds 并生成该姿态封面; [当前帧实证](../../task-memory/task-billfish-animation-browser/host/production-pose-cover-verification.json) / [新增动作播放](../../task-memory/task-billfish-animation-browser/production5-increment-playing.png) |
| P04 | 动画列表完整交互 | 名称, 相对目录, duration, 可获 FPS, 匹配和播放状态; 文本/模糊搜索; 名称/时长/目录排序; 兼容/目录/时长/格式筛选; 子目录分组; 条件保留到窗口关闭 | 列表字段/搜索/筛选/排序/分组/快捷键按既有组件与真实 smoke 通过; 安装版 125 rows, 相对目录/FPS/状态正确, 报告打开复制通过; [实际列表](../../task-memory/task-billfish-animation-browser/production4-fbx-warm.png) / [报告 UI](../../task-memory/task-billfish-animation-browser/production5-report.png). 不扩成每个组合的重复点击测试 |
| P05 | 单 Mesh 常驻, clips 按需 | 真实切换多个外部 FBX/GLB, 记录 mesh parse/upload 次数不随切换增加, 活跃 action 唯一, 未播放 clips 不全部创建 action | 通过: 真实 FBX 仅选定 clip bake, renderer 320 动作 modelLoads=1/action=1/GPU 对象稳定; [resource gate](../../task-memory/task-billfish-animation-browser/viewer/resources-320/result.json) |
| P06 | 正确动画语义 | 本地 TRS, 实际时间, 插值, root motion, 非循环, 缺轨道, 多根, scale, A-B-A 来回无累积变换; 旧异步加载不能覆盖新选择 | 通过所列语义 gate: actual Three playback A-B-A/defaults/唯一 action/乱序/STEP/cubic/root/scale, 两真实 FBX 动态 skin 采样; [viewer](../../task-memory/task-billfish-animation-browser/viewer/implementation-and-validation.md) / [parser](../../task-memory/task-billfish-animation-browser/parser/implementation-state.md) |
| P07 | GLB morph target | 当前实现支持时实测权重曲线; 不支持则逐 clip 明确标记缺失/不支持, 不静默显示完全兼容 | GLB embedded morph 实际 WebGL smoke 和权重恢复通过; 外部未证 morph identity 标 partial/缺目标 incompatible; [morph](../../task-memory/task-billfish-animation-browser/viewer/morph-gate/result.json) / [backend](../../task-memory/task-billfish-animation-browser/backend/implementation-notes.md) |
| H01 | 普通网格默认隐藏已确认的纯动画 | 真实 native model/filter 结果: Mesh+animation 保持显示; 未识别/坏/不兼容/歧义保持显示; 原记录完整 | 安装版通过: production4 四布局隐藏 pure-animation 56, 每布局 Ctrl+A 的 items.selected=[]; Mesh/GLB 仍能原生双击打开. [Grid](../../task-memory/task-billfish-animation-browser/native/production4-hide-grid.json) / [List](../../task-memory/task-billfish-animation-browser/native/production4-hide-list.json) / [Flexible](../../task-memory/task-billfish-animation-browser/native/production4-hide-flexible.json) / [Waterfall](../../task-memory/task-billfish-animation-browser/native/production4-hide-waterfall.json). 同 source 地址重绑丢 filter 的 production 缺陷已闭合 |
| H02 | 恢复与例外 | 全局隐藏开关, 显示全部动画方式, 单项取消隐藏; 无有效关联时自动恢复可见; 可定位路径/打开目录 | 安装版通过全局 off/on, 三 viewer 下单项 show/re-hide 及无错误反馈. 相同完整集合的在途真实 ACK 共享修复已由专项/只读审查及新 host 实机闭合; [单项 show 原生效果](../../task-memory/task-billfish-animation-browser/native/production5-single-show-retry.json) / [item 56 原数据](../../task-memory/task-billfish-animation-browser/production5-single-show-final-selected-result.json) / [恢复隐藏](../../task-memory/task-billfish-animation-browser/native/production5-single-rehide.json). 无需禁用内置组件即可恢复原文件展示 |
| H03 | 保留宿主数据与搜索管理 | 前后 item ID, 标签, 备注, 目录, 排序/检索行为核对; 切换网格/列表/搜索/文件夹都采用定义一致的过滤状态 | 安装版四布局/搜索/Ctrl+A, global show 后 item 56 的 tag 1/note/native_is_hide=0 保留; 切库旧 host/viewers 退出, 新库无旧 profiles/associations. [原生数据](../../task-memory/task-billfish-animation-browser/production4-global-show-selected-result.json) / [切库](../../task-memory/task-billfish-animation-browser/host/production5-library-lifecycle-verification.json). 早期可见 file/folder 选择保留专项仍有效 |
| T01 | Mesh 原生卡片缩略图 | 首次可见异步生成, 原生 grid 显示, 失败默认图标且不阻塞; 重开直接复用, 不无条件渲染不可见模型 | 安装版通过: 新导入 Mesh 58 首次可见即生成真实卡片图像, 未靠先打开 viewer; 已存 cover 跨正常重启复用. [首次卡片](../../task-memory/task-billfish-animation-browser/native/production-fresh-mesh-card.png) / [首次快照](../../task-memory/task-billfish-animation-browser/host/production-fresh-mesh-card.snapshot.json) / [重启快照](../../task-memory/task-billfish-animation-browser/host/production4-startup-no-viewer.snapshot.json). invalid-height cache key 与 int32 ABI 已在生产修复 |
| T02 | 指定动画帧封面 | 当前项目已有能力应保持; 外部 clip 正确姿态写入当前 Mesh, 重启持久, 源文件/metadata 不变 | 安装版通过: 外部 B_AtkBeta01_1 在 1.504 seconds 写入当前 Mesh cover, source/metadata 不变; 参考姿态重建另产生不同 cover, 后者跨重启保持. [当前帧](../../task-memory/task-billfish-animation-browser/host/production-pose-cover-verification.json) / [参考姿态](../../task-memory/task-billfish-animation-browser/host/production-reference-cover-verification.json) / [重启存储](../../task-memory/task-billfish-animation-browser/host/production4-startup-no-viewer.snapshot.json). 区分两次封面内容, 不声称重启时仍为先前动作帧 |
| K01 | 持久元数据/关联/缩略图/版本 | 独立索引含稳定身份, 相对路径, size/mtime, 骨架指纹, clip metadata, 轨道摘要, 映射, 诊断, 缩略图, parser/schema/profile revision | metadata/clip/关联/profile/schema/cache 持久结构已实现并验证; [backend](../../task-memory/task-billfish-animation-browser/backend/implementation-notes.md); native thumbnail 存储依现有宿主, 见 T01/T02 |
| K02 | 正常启动无全量解析 | 冷建/第二次启动/新增一个文件分别记录 parse count, 耗时, RSS; 未变更 warm 启动模型/动画 parse=0, 不预载全部轨道 | 组件 warm inspect/model/hash=0, mapping=0 与无 viewer startup 专项通过; 安装版无 viewer 即恢复隐藏/cover, 首开 GLB 6 rows 无需刷新, FBX 恢复 125 rows. [启动](../../task-memory/task-billfish-animation-browser/host/production4-startup-no-viewer.snapshot.json) / [GLB 首开](../../task-memory/task-billfish-animation-browser/production4-glb-first-open.png) / [FBX warm](../../task-memory/task-billfish-animation-browser/production4-fbx-warm.png). 安装版画面不替代组件 parse counter 证据 |
| K03 | 增量与漏事件恢复 | 新增/修改/删除/rename/move/覆盖/重复导入; 只影响相关关联, watcher 丢失后目录校验可恢复; 手动完整校验能检出同 size/mtime 替换 | 组件新内容 inspect delta=1, mapping 0/1 与源变化/rename/删除专项通过; 安装版 native copy 不同 F 动作为 item 59, 58 自动从 6 增至 7 rows 并实际播放. [真实导入](../../task-memory/task-billfish-animation-browser/production5-import-new-action-result.json) / [自动增量播放](../../task-memory/task-billfish-animation-browser/production5-increment-playing.png). 内容已在缓存时允许 inspect=0, 未修改 F 原件 |
| K04 | 有界资源和取消 | 数百 clips 连续浏览内存/对象/GPU 资源曲线, clip cache 按字节/成本有界且淘汰释放; 大文件失败和取消后可继续 | 样例通过: 40 真实动作 backend cache 淘汰; 320 合成动作无 forced GC, LRU12/actions1/GPU 对象2/1/2, dispose全部0; [resource gate](../../task-memory/task-billfish-animation-browser/viewer/resources-320/result.json); 非任意时长/显存字节保证 |
| K05 | 维护操作 | 刷新关联, 重做当前缩略图, 重解析当前动画, 校验所选目录, 重建全部扩展缓存均可操作; 进度, 取消和错误计数真实 | 安装版实际参考姿态缩略图重建与重建扩展缓存成功, 后者 3 组 profiles 完全相同并恢复 125 rows; [参考姿态](../../task-memory/task-billfish-animation-browser/host/production-reference-cover-verification.json) / [缓存 UI](../../task-memory/task-billfish-animation-browser/production5-cache-rebuilt.png) / [配置保留](../../task-memory/task-billfish-animation-browser/host/production5-cache-profile-verification.json). verify/reparse/cancel 保持既有组件专项, 不重复套件 |
| S01 | 本地可再生缓存隔离 | 多设备不写同一 runtime SQLite; cache 路径在本机非共享位置, 无隐式共用同步库 WAL | 通过架构/专项: 本机 JSON cache 与每库 revision profiles 分离, sessionSchema3 两库同 ID 不覆盖; [audit regressions](../../task-memory/task-billfish-animation-browser/backend/audit-regressions.log); 不修改共享原生 SQLite |
| S02 | 手动关联/轴向共享配置安全 | 可版本化配置, atomic write, revision/parent 检查; 两设备离线分叉合并后冲突被保留并展示, 不静默后写覆盖 | 通过等价双 service 分叉/atomic/merge/远端坐标应用及冲突阻止加载; [backend](../../task-memory/task-billfish-animation-browser/backend/implementation-notes.md) / [audit regressions](../../task-memory/task-billfish-animation-browser/backend/audit-regressions.log); 未运行真实百度云同步, 交付明确此环境边界 |
| S03 | 损坏/中断恢复与回滚 | 中断索引写入/不支持新 schema/坏 cache 可重建; 用户选择不随清缓存丢失; 禁用功能恢复原网格, 无主库迁移破坏 | 组件坏/旧 cache 与 init/shutdown 专项通过; 安装版 cache 重建保留 3 组 profiles, 全局关闭隐藏恢复原动画, 真实切库关闭旧 host/viewers 且新库 0 profiles/0 associations; [缓存保留](../../task-memory/task-billfish-animation-browser/host/production5-cache-profile-verification.json) / [库隔离](../../task-memory/task-billfish-animation-browser/host/production5-library-lifecycle-verification.json). 内置组件不能按外部插件禁用, 恢复显示使用 visibility 开关; binary 回滚由最终交付说明记录 |
| R01 | 匹配报告 | 来源, 目标, 总轨道数, 匹配骨骼轨道数, 未匹配项, 必要骨骼, 可疑姿势/变换, 最终状态, 可复制, 不反复弹窗 | 安装版通过实际报告打开和复制; [报告 UI](../../task-memory/task-billfish-animation-browser/production5-report.png) 显示当前 source/target, [clipboard 读回](../../task-memory/task-billfish-animation-browser/host/production5-copied-report.json) 为 106/106 tracks/full, 无 unmatched/ambiguity. clipboard 合同只复制 match report 字段, 不要求重复 UI 身份字段 |
| F01 | 完整交付流程 | Billfish Mesh 卡片 -> 双击 -> 自动列表 -> 多动作 -> 重启/cache -> 新动画增量播放 -> 关闭隐藏见原文件, 实机记录 | 安装版完整流程通过: 原生双击/125 列表/多动作/cover/正常重启/真实增量播放/cache 重建/真实切库, 全局与三 viewer 单项显示/恢复. 第 9 节逐项引用实际 UI/原生结果, 本轮无剩余功能验收阻断, [正常退出](../../task-memory/task-billfish-animation-browser/host/production5-after-normal-exit.snapshot.json) 与 [最终 Healthy](../../task-memory/task-billfish-animation-browser/native/production5-final-health.json) 亦通过; root 统一交付记录 |
| F02 | 文档与验收交付 | 架构/技术, 数据库变更或无变更依据, 自动/手工测试, cache 清理/重建/恢复, 格式限制, 每阶段变更记录 | 已完成: 架构/计划/协议/组件证据及本矩阵已更新至最终实机范围; [task_state](../../task-memory/task-billfish-animation-browser/task_state.md) 统一记录部署身份, 恢复/cache 操作, 无 native schema 迁移, 格式限制及最终环境 |

## 3. 不能静默缩小的范围

1. 修 crash 之后仍必须完成角色+独立动画文件浏览, 不能以既有单文件动画控制栏宣布完成.
2. 最终必须接入 Billfish 普通素材网格隐藏与恢复. 独立 Electron 列表可以作为中间阶段, 不可替代原生网格集成. 若 native filter 无可靠入口, 这是需要继续调查或向用户说明的架构阻碍.
3. `F:/TempFBX` 的 125 个纯动画是重要真实集成样本, 但不能单独证明数百文件规模, GLB, 多 clip, 多拓扑, Max/Maya 导出兼容和多设备冲突.
4. 原文件不合并, 不移动, 不修改. `fbx-anim-merger` 可借鉴, 不要求照搬其导出合并交互或内部 retarget. 中间转换只允许写入可重建本地缓存.
5. 不用重命名/删索引/隐藏文件属性/一串 GUI 点击来实现动画隐藏. 数据保留和宿主搜索管理仍是验收条件.
6. "使用 Maya 默认"不能变成所有文件强制 Maya, 也不能覆盖 Max 文件已声明的坐标信息. 必须提供选择及持久化.
7. 跨角色自动 retarget 不在目标内. 轴单位标准化或已验证的同骨架坐标基变换不是新增 retarget; 依据身高自动缩放 root translation, 重定向不同参考姿势则改变语义.
8. 不为方便而把全部曲线/几百个 AnimationAction 常驻. 不把目录文件复制到几百份的压力测试称为几百种真实动作兼容性.
9. 没有测量数据时不承诺秒数, FPS, 内存或"全部格式兼容". 合成 fixtures 可以覆盖特殊拓扑和错误, 必须标明其性质.

## 4. 关键架构选择

### 4.1 ThreeCore 与 ufbx 的分工

最小可验证路线是复用已有视口/renderer, 在独立 worker/process 中完成文件发现, metadata 提取和选定 clip 解码, 向播放器交付明确规范化的 clip. 这保留材质/相机/封面入口, 但前提是能通过可维护接口得到目标 joint hierarchy, bind/rest transforms, skin bindings 和当前 mixer. 现有 `preview-preload.cjs` 只证明 loadFile 和当前 animations 可访问, 不证明所有这些内部接口可用.

若现有 Core 只能提供不透明模型对象, 或 parser 输出与其 bind/inverseBind 不一致, 不应增加基于 minified 名称的脆弱 patch. 可以在同一 Electron 宿主内提供自有角色 viewer, 统一 FBX/GLB 的内部表示和播放语义, 并保留旧广格式 viewer 为其他格式入口. 这是实现路线取舍, 不应扩大为重写 Billfish 主程序.

对 root 提出的具体方案, 本次独立判断倾向于专用维护的 Three viewer: 独立 ufbx helper 读取 FBX, 将 base Mesh 生成本地 GLB cache, 按需输出规范化 clip; 原始 GLB 由同版本 GLTFLoader 读取; 同一 renderer/mixer/clip 控制接口负责两者. 它比继续依附 Core 私有对象多承担材质/相机接线, 但获得明确 joint/skin/animation ownership, 避免两套 Three 类对象和 importer 的坐标解释交叉. 旧 Core 可继续服务其他已支持格式, 不需要为本目标重写整个通用模型预览器.

| 路线 | 优点 | 本目标的主要风险 | 选择条件 |
| --- | --- | --- | --- |
| 复用 Core 内部 scene/mixer | 沿用已有材质和视口, 初始 UI 工作少 | 私有模型接口未证明; ufbx clip 与 Core bind 可能不一致; bundled Three 版本/类型难控制 | 明确导出稳定骨架/skin接口, 并以真实矩阵证明转换一致时可选 |
| 维护小型 Three viewer | parser输出/skin/mixer完全可控, FBX/GLB统一; 方便有界clip cache | 需要实现材质贴图/构图/清理, 保证baseGLB转换保真 | 当前更推荐, 先真实Mesh+两动作gate后并行UI |
| 再增加独立C++ renderer | FBX原生数据路径直接 | 增加窗口/渲染/材质/截图整套系统, 现有Electron入口收益丢失 | 目前没有必要证据, 不推荐 |

base GLB 导出必须保留完整 skin inverseBind, 多个 skins, 非 joint ancestors, mesh transforms 和可解析材质/texture 引用. 用 node extras 或独立映射保留 source node ID/path, 不假定 GLB node index/name 与 FBX 一致. helper 和 viewer 的规范化版本进入 cache key. 如 FBX 特性在 GLB 中不可准确表达, 应用可验证的 bake/补偿或明确失败, 不能因为生成的 GLB 能打开就认定转换正确.

低并发独立 parser 进程可把 FBX 解析/烘焙与 renderer UI 分离. 不把 125 个动画 inspect 变成 125 次完整 Mesh 导出或所有 clips bake. 先用小并发完成测量, 再决定是否需要常驻 worker; 不在未测量时增加复杂进程池/锁. GLB metadata 枚举不应触发 GPU 上传. 数据传输格式先保证单 clip 有界并测量, 不先引入大型压缩/数据库依赖.

ufbx 官方建议使用 evaluate/bake 接口处理 FBX 动画, 避免手写 Euler 曲线, pivots, pre/post rotation 和 layers. 使用按 clip bake 后的 TRS 或保真插值表示, 不在每帧读取完整 FBX. 选定采样策略需要与原求值结果做误差验证, 不固定 30 FPS 丢掉高频运动. [ufbx animation](https://ufbx.github.io/elements/animation/)

ufbx 提供 source axes/units 和目标坐标转换, 并支持不同 transform/geometry 处理策略; FBX inherit scale 等不等于简单 parent TRS. 统一内部格式必须显式约定已处理项, 对不可表达 shear/inherit 情况不能默默丢弃. [ufbx nodes](https://ufbx.github.io/elements/nodes/), [FBX node transforms](https://ufbx.github.io/fbx/node-transforms/)

GLB 应保留 node 索引, skin joints/inverseBind, animation sampler 插值和 morph 语义, 不能只提取骨骼名字. UI 使用相同 clip 描述与控制接口, 不需要为了统一 UI 强迫两种格式共享同一原始 parser. [glTF 2.0 specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc)

### 4.2 轴向, 参考姿势和非 retarget 匹配

至少分三层: (1) 文件场景 basis/handedness/unit, (2) 每个 joint 的 local/rest/bind basis, (3) 视口展示朝向. 相机或外层旋转只能解决第三层; 它不证明动画已正确绑定.

Maya 默认预设可用 Y-up, 同时保留用户选择的关节 primary/secondary axis 偏好. Maya 本身允许 Y/Z world-up 和多种 joint orientation; 没有一个名称叫"Maya"的选项能完整描述任意骨架的每根 bone. 必须以实际文件的矩阵和用户覆盖为准, primary/secondary 的一个默认值仅是 UI 预设, 不应写成检测事实. [Maya world coordinates](https://help.autodesk.com/cloudhelp/2026/ENU/Maya-Customizing/files/GUID-4D653DC9-57AA-4D8B-987A-5B7A9735CAF0.htm), [Maya joint command](https://help.autodesk.com/cloudhelp/2026/ENU/Maya-Tech-Docs/Commands/joint.html)

建议持久字段: `profileId/schemaVersion/revision`, `sourceKind` (detected/explicit/default), `sourceAxes`, `sourceUnitMeters`, `sourceSkeletonFingerprint`, `jointBasisPolicy`, `jointBasisOverrides`, `renderFacing`, `targetMeshId`, `animationScope` (角色继承/文件覆盖). 只存有语义的字段, 无法检测的值明确 unknown, 不靠 exporter 名称猜测已知.

选择优先级建议为显式文件覆盖 > 已确认角色/骨架配置 > 文件实际元数据 > Maya fallback. 角色配置与文件元数据矛盾时显示差异, 不悄悄覆盖. `OriginalUpAxis` 是导出来源历史, current `UpAxis` 与 root transforms 才是当前数据解释的一部分, 不可两者分别转换一次.

匹配先用原始名称和完整 parent path, namespace 清理只在一一对应时启用. 清理后重名不 first-wins. 不把非骨骼 transform helper, joint ancestor, 多个 roots 丢掉. Skeleton fingerprint 应包括结构和规范化 bind/rest 信息, 拓扑指纹与参考姿势指纹分开, 便于解释为什么"名字一样"仍有差异.

同骨架的已知坐标基变化可以换基. 定义 `C_i` 为 source joint-local 坐标到 target joint-local 坐标的映射, 则局部变换需要满足 `L_target(i,t) = C_parent(i) * L_source(i,t) * inverse(C_i)`. 同时处理根, mesh/bind/inverseBind, unit; 不能只修改 quaternion 分量或把每根骨骼都乘相同旋转. 这只是数学约束, 不是能从任意不同姿势自动推断 `C_i` 的承诺. 若本质是不同角色/不同 rest pose, 明确报告并停止自动绑定.

"完全兼容"检查有意义的 source 动画轨道都可正确映射和求值, 不要求目标每根 bone 都必须在每个动作中有轨道. 无轨道骨骼保持目标参考状态. 缺 root/关键父链, 歧义映射, 未经验证的 basis 差异不能靠高名称匹配百分比升级为完全兼容. 部分兼容可播放并警告, 但默认不隐藏未经明确确认的部分关联.

### 4.3 数百 clips 的载入和缓存

列表先读取 metadata: file/clip ID, 名称, duration, 可获 FPS, 目录, 兼容/诊断摘要. 扫描结束不应同时创建全部 mixer actions. 选择动作才 decode/bake 所需 clip, 仅保留当前与近期使用的有界缓存. 统计 cache bytes, decode 次数和 target mesh parse/upload 次数, 避免靠"只存 N 个"掩盖单个超大 clip.

切换动作先停止旧 action 并恢复确定参考姿势, 再应用新 clip. A-B-A 应复现相同时间的世界矩阵/抽样顶点. 目标 joint 用稳定 ID/path 映射, 不依赖全 scene `getObjectByName()` 的第一个结果. 多个异步加载乱序的行为应通过可控延迟用例验证, 旧请求不可把当前选项换回去; 不为此引入无证据的全局锁.

帧率显示区分 source FPS, 求值采样率, 逐帧步进 FPS. GLB 未显式给出固定帧率时显示未知或实际推断标签, 不能沿用控制栏默认 30 当作原文件 FPS. 时间轴以 seconds 为准. Root motion 保留真实位移, 可选 in-place 是显式播放选项且不修改源数据.

cache key 至少包含文件内容版本/快速状态, clip 身份, parser 版本, 规范化策略, 目标 skeleton/bind fingerprint 和用户 profile revision. 文件改名可复用内容缓存, 但不能把路径当成跨设备唯一身份. 全量 hash 应用于需要复核的文件/显式校验, 不在正常启动无条件读取所有源字节.

### 4.4 展示隐藏与多设备配置

隐藏应为 native item 查询/展示层上的可恢复 predicate. 建议条件是 extension enabled, 全局开关开, 已识别 pure animation, 存在有效确认关联, 没有单项 show override. 删除最后一个有效关联或 cache 不可判定时应恢复显示. 同一动画可被多个 mesh 复用, 不应因某一个关联失效就忽略其他仍有效关系. 标签, notes 和 source item 仍由原生库持有.

[Native 审计](../../task-memory/task-billfish-animation-browser/native-audit.md) 的初始 public `setRowHidden` 候选已被真实 Flexible/Waterfall 行为否定. 最终维护方向是认证当前实例的 `BFDataModelFilter`, 仅在其原生 row predicate 前排除有效隐藏 IDs, 保留原 model/proxy/selection ownership 和原 vendor predicates, 使用原生 no-write refresh. [新诊断](../../task-memory/task-billfish-animation-browser/native/filter-probe.jsonl) 已通过四布局/选区/搜索, [数据保留](../../task-memory/task-billfish-animation-browser/native/filter-data-preserved.json) 为真. 此前 public API 失败只解释路线更换, 不再代表新方案失败. 后续 production4/production5 已完成四布局, 重启和真实切库验收, 见第 9 节; 本段诊断本身不单独承担生产通过结论.

性能 cache 放本机, 与原生标签/notes 数据库分离. 用户手动关系, 轴向选择和显示例外是不可再生配置, 不能随"清缓存"删除. 若需要随素材同步, 用 library/角色相对路径和稳定 ID, 不持久化另一机器不可用的盘符作为唯一定位.

新 parser/index/clip/中间渲染缓存可以放本机. 现有 native cover 发布路径是素材库 `.bf/.preview`, 可能被外部云盘同步. 复用该机制不能同时宣称所有封面都位于本地非同步路径; 必须明确中间 cache 与原生展示封面的不同所有权, 避免多个设备无条件重复发布同一 cover. 当前无法据此证明百度云实际同步规则.

同步盘上的 temp+rename 仅解决单机部分写入, 不解决两台离线设备后写覆盖. 共享配置需要保留 `revision/parentRevision/writerId` 和冲突分支; 可用不可变 revision 文件加 parent heads 检测, 也可采用有等价保证的结构. 发现分叉后保留两份, 显示冲突, 自动关联不覆盖人工选择. 缓存构建, UI 选择和同步冲突是不同问题, 不要用 SQLite file lock 声称解决云同步.

## 5. 资产覆盖与验证顺序

资产 probe worker 已报告 `F:/TempFBX` 有 126 个 FBX, 总计约 476 MB: `EB2010_00` 为 1 mesh / 140272 vertices / 106 bones / 76 skin clusters / 1 take; 子目录 125 个文件为 0 mesh / 106 bones / 1 clip, 名称和父级 topology 一致, 单 root `Trans`. 当前声明 Y-up/X-coordinate/Z-front, OriginalUpAxis Z-up, cm, 30 FPS, root PreRotation 为 -90 degrees X. 此结论来自其 binary metadata probe, 不是本审查独立播放验收. 见 [结构证据](../../task-memory/task-billfish-animation-browser/assets/fbx-structure.json) 和 [资产/参考仓库报告](../../task-memory/task-billfish-animation-browser/reference/asset-reference-findings.md).

该 probe 还报告全 125 与 base 的显式 Pre/PostRotation, RotationOrder, InheritType 一致; Lcl TRS 差异可能是各文件初始 pose, 不能直接当成不同 bind pose. 其 stack 时段约 0.667-8.0 seconds, 原始曲线常多延伸两帧, duration 应优先使用 stack span, 不直接取所有 key 的最大时间. 应分别保留 authored rest, skin bind 和动画首帧, 三者不能互代.

同一 worker 对 `fbx-anim-merger` 的审查报告指出: MIT revision `0a577e1d7b5af59b615b8eed104e165f821192b7`, 使用 ufbx 求值; 匹配依赖清理名称和比例, 重名 first-wins, 默认舍弃非 root translation/scale 并应用 hip-height retarget; GLB 只有 writer, shader 有 200 bone 上限. 这些不满足当前无 retarget/任意拓扑/GLB 输入目标, 不能照搬. 具体来源见上方资产/参考仓库报告, 本段转述不是独立源代码复核.

Native 审计发现现有 `items.import_path` 只支持 copy, 无原始 source path 持久字段, 现有 linked item 的 path resolution/preview 仍拒绝. 单独导入 Mesh 后不能假定 `F:/TempFBX` 兄弟动画仍在其新父目录. 因此 viewer 需要显式选择动画目录并持久 scope, 默认仍只扫描真实 Mesh 父目录. 外部未入库动画没有 native card, 只能隐藏真正存在且身份核对的 native IDs, 不应为了"隐藏"而先批量导入外部动画.

建议 gate 顺序:

1. 当前 crash 根因和旧静态/带动画 FBX, GLB 正常打开关闭回归.
2. 同一真实怪物 125 动画 metadata+匹配, 再选择几个动作实际显示, 与原求值器核对多个时间点. 轴向样例不能只看 bind pose.
3. GLB 多 clip, 人形/四足/多根/大骨架与反例 fixture. 复核受支持插值, root, scale, 缺轨道和 morph 标记.
4. 首次/缓存命中/增量三种性能测量. 加入至少数百文件的规模集, 标明真实独立动作与复制压力文件的区别. 不用固定总数量上限代替有界工作队列.
5. Native 隐藏与缩略图, 用已存在标签/备注的素材验证切换, 搜索, 恢复, 删除关联和库切换.
6. 轴向/归属配置持久化, 缓存坏文件/中断恢复, 双设备或等价双工作副本的真实配置分叉测试.
7. 按 F01 完成用户指定的完整实机流程, 再逐项把矩阵改成证据引用. 空白, 弱证据和未覆盖仍算未完成.

## 6. 需要 root 保持的判断边界

- 已修的是缺失 texture 被错误当作整模型 fatal 的具体失败. 完整安装版开关窗口/重启/坏文件受控失败与 Native filter 部署验收已由第 9 节补齐, 不再列为待办; 仍不推断未知 native 异常根因.
- 最终采用独立维护的 Three viewer + ufbx helper; 已有序列化 bind/dynamic-skin 数学对照. 不再把早期 ThreeCore 混合适配的不确定性列为待办, 也不以视觉印象替代数学证据.
- 默认 joint primary/secondary 是用户偏好预设, Maya 不提供唯一通用骨骼朝向. 可采用可逆默认继续, 但交付必须说明它不覆盖文件检测事实.
- 模型单位, 根变换和 FBX pre/post rotation 都会影响同拓扑动画. 文件 metadata 一致仅是有利证据, 不是 pose 正确的充分条件.
- 本文只新增需求审查文档, 未修改 production, 未构建/部署, 未 GUI 操作, 未更改主库或素材文件.

## 7. Backend 实施复审与实证边界

复审时间: 2026-10-19 02:05, 验修更新于 02:10, Asia/Shanghai. 已读回 `automation/animation_browser/backend/{service,glb,matching,profiles,parser-jobs,coordinates}.cjs`, 相关 focused tests 和以下证据. 本次未重复执行已经通过的 suite, 仅复用原独立反例验修. 本节当时不承担 Native UI 和原生缩略图通过结论; 后续安装版证据已在第 9 节闭合.

| 证据 | 能证明的范围 | 不可扩大为 |
| --- | --- | --- |
| [14 项测试日志](../../task-memory/task-billfish-animation-browser/backend/tests-2.log) | GLB 读取/错误, 匹配反例, 320 个合成 clip metadata, warm metadata 复用, 单 clip cache, profile 分叉, GLB basis/单位处理, 源变化 metadata 安全, native ID rename 配置保留 | 真实数百 FBX 完整播放, Native UI 或全部格式兼容 |
| [2 项专项日志](../../task-memory/task-billfish-animation-browser/backend/additional-regressions.log) | 缺失 GLB optional texture 使用有效 placeholder; 两个 service 共享 visibility revision 分叉可见并显式 merge | 真实云盘完整同步, 所有坐标配置活跃更新正确 |
| [真实 125 动作 benchmark](../../task-memory/task-billfish-animation-browser/backend/real-benchmark/results.json) | 首建 3816.8 ms, 126 inspect, model=1, clip=0; 选定 3 个动作仅 bake 3 次, 重复选择命中; warm 151.4 ms, 立即恢复 125 rows, inspect/model/hash=0 | GUI 首帧耗时, matcher 复用次数, FPS 或跨机器性能保证 |
| [新内容增量证据](../../task-memory/task-billfish-animation-browser/backend/new-content-incremental/results.json) | 添加一个从未解析的不同 FBX 内容, inspect/metadata parse delta=1, 两个动作 full, 52.8 ms | 全部旧关联没有重新匹配, 或 rename/move/delete 全覆盖 |
| [40 个真实动作 cache 压力](../../task-memory/task-billfish-animation-browser/backend/cache-pressure/results.json) | 24 MiB cache 上限, 已发生淘汰, 最后 24846729 bytes/5 entries, model/inspect/metadata parse=0; 每 5 次强制 GC 的 RSS/heap 观测 | 无强制 GC 的长期内存上限, renderer/GPU 释放, 320 个动作完整播放 |
| [4 项审查回归](../../task-memory/task-billfish-animation-browser/backend/audit-regressions.log) | 已知 FBX bind 差异拒绝, mapping warm=0/new=1, 共用 local cache 的两库相同 item ID snapshot 不覆盖, 远端 Max 配置真正改变 GLB wrapper | 所有格式数学正确, 实际云盘时序全覆盖 |
| [320 合成动作实际浏览](../../task-memory/task-billfish-animation-browser/viewer/hundreds-gate-clean/result.json) | ok=true, rowCount=320, loadedIds=320 且全部唯一, errors=[], ready.meshCount=1; root 已查看实际播放窗口截图 | 320 个真实 FBX 格式覆盖, renderer LRU 实际条数, RSS/GPU 峰值 |
| [320 合成动作 renderer 资源](../../task-memory/task-billfish-animation-browser/viewer/resources-320/result.json) | 16 次观测, 无 forced GC; switching 时 modelLoads=1 且同 model UUID, renderer LRU=12, mixer actions/bindings=1; GPU geometries/textures/programs=2/1/2 稳定; dispose 后 GPU/action/binding/cache=0, canvas detach, context lost | 任意资产/任意时长内存上限, GPU 显存字节峰值; 此处 GPU 是 renderer.info 对象计数 |
| [Native public row-hide gate](../../task-memory/task-billfish-animation-browser/native/visibility-public-api-gate.md) | Grid/List 在该试验通过; adaptive Shift 选区和 waterfall 几何/hit-test 失败; 单靠 setRowHidden 不是完整实现 | H01-H03 完成或生产隐藏可部署 |

已读回关闭的前轮具体缺陷: metadata 在源 stat/hash 重检后才发布; LRU 只保留 clip 并每次返回当前 target mapping/source identity; `startScan` 的错误处理绑定 generation; 每个 service 明确拒绝混用不同 libraryRoot, host 的 profileRoot 位于每库 `.bf/animation-browser/profiles`; revision 使用 temp+rename; `cachedRows` 增加 parser/normalization/policy gate; GLB 外部相对 texture 嵌入 derived GLB; 多 skin bind 不再 last-wins; 未知 skinless basis 和外部 morph identity 不再静默 full. 这些修复不自动关闭表中涉及完整 UI/数学/迁移的总要求.

本次独立小反例发现的 5 项问题已交 backend owner 修复, 02:10 验修后均在本次具体范围内关闭. 保留触发条件与修复证据:

1. Clip cache 原先在源重检前发布. 现在 helper 写唯一 staging, validate/SHA/stat/policy 验证后才 atomic publish, key 增加 `validated-clip-cache-3` 避免旧污染复用. 独立原反例验修: 第一次 `SOURCE_CHANGED`, 最终 clip cache 文件数 0; 恢复 old bytes 后 decodeJobs=2, 输出原值 `[1,0,0,1,0,0]` 而非 replacement 的 999, staging 剩余 0.
2. Visibility 原先提前消耗远端坐标 revision. 现在 visibility 只读新配置计算隐藏, 实际应用由 profile refresh 与 model/rows policy gate 承担. 独立真实 GLB fixture 验修: profile=max, modelChanged=true, wrapperPresent=true, unit=.01, rows=2. 额外制造两个坐标 revision heads 后 `clip.load` 返回 `PROFILE_CONFLICT`, hiddenCount=0, 不静默用默认坐标加载.
3. 已知 FBX bind-to-world 差异原先被忽略. Matcher 现对同 format 的 bindMatrices 比较矩阵值而非 skin ID, 报告明确空间. 4 项专项中的原 100 平移反例返回 incompatible. 不以此声称跨格式任意 bind 数据可直接比较.
4. 两个库相同 native item ID 原先覆盖 derived session. 现 `sessionSchema=3`, snapshot key 包含本机 libraryRoot namespace 与库内 profileKey. 4 项专项实际创建两库相同 ID, 共用 local cache 后分别 warm 恢复成功. 共享 profile 保持每库 item ID 身份不变.
5. Warm scan 原先全量重算匹配. 现按相同 base/profile/source hash 复用 mapping/report, 并记录 mappingComputations. 4 项专项证明 warm=0, 真正新内容增加后=1; 目录 stat reconciliation 保留.

源变化/共享配置的独立实验只使用隔离临时文件, 已清理. 其余为已读源码/host 路径和审查专项日志证据. 本次没有更改 production 或测试代码. 当前不能据 backend 或 320 个合成动作播放结果把 H01-H03, T01-T02, F01 或任意资产/任意时长资源上限标为最终通过. 新 native filter 已解决旧 public hide 的诊断失败. 本节当时保留的最终集成部署和真实用户流程已由第 9 节 production 实证闭合, 单项显示的最终三 viewer 反馈复验也已通过, 见矩阵 H02.

320 renderer 资源测量的已读样本 RSS 范围为 98844-107820 KiB, 每个样本 `forcedGC=false`; 约 41 秒内完成 320 个合成动作的切换/seek. 这些是本次输入下的过程观测, 不是可承诺给其他设备或大模型的固定资源预算.

## 8. 无 viewer 启动恢复的定点审查

审查时间: 2026-10-19 02:26, 验修更新于 02:27, Asia/Shanghai. 本节只覆盖新增 `initializeLibrary`/已存关联监听/关闭生命周期, 不扩展为重跑 backend 套件.

Host 现在在启动 microtask 调用 `initializeLibrary(libraryRoot)`, 通过完整路径集合回调请求 native visibility 更新, 包括空集合. `applied` 依赖 native ACK, 失败报告 `failed_open`, 不把 backend 计算完成称为原生显示已经完成. 该调用不要求创建 viewer. Backend 仅恢复本库 schema/policy 有效的 association snapshots, 复核 source stat 和 profile heads, 不解析 Mesh/动画. 已保存 scope 与 Mesh 父目录有 watcher, 30 秒 stat reconciliation 用于漏事件恢复; 没有 session 时 profile watcher 仍能发布可见性变化.

[初始 3 项专项](../../task-memory/task-billfish-animation-browser/backend/startup-visibility.log) 证明无 viewer 启动恢复 2 个确认隐藏路径, 修改/删除动画或 Mesh 会清除对应隐藏, 离线 profile 分叉使全部可见, 显式合并后自动恢复. 全部 metadata parse/hash/mapping/parser 调用数为 0. 这证明 backend 路径集合计算与事件, 不证明 Native 四布局应用.

独立生命周期反例曾发现: 启动 `initializeLibrary(lib)` 后立即 `shutdown`, 初始化仍 resolved 并在 shutdown 后创建 profile watcher/timer, 发布一次. 修复将初始化 completion 纳入 drain, 在异步继续点检查停止状态. [更新 4 项日志](../../task-memory/task-billfish-animation-browser/backend/startup-visibility-2.log) 包含此回归. 独立原样验修结果为 `SERVICE_STOPPED`, `profileWatcherCreated=false`, `timerCreated=false`, `publicationsAfterShutdown=0`, 进程自然退出.

另一个库边界反例曾发现并行 `initializeLibrary(A)` 与 `initializeLibrary(B)` 两个不同真实目录均 fulfilled, 并创建重复 profile watchers 使 shutdown 后进程保活. 现入口在首次 await 前保留 trusted root, 同 root 复用 in-flight 初始化, watcher 在 mkdir 后重新检查已有实例. 独立验修并发 `[A,B,A]` 的结果为 `[fulfilled(A), rejected(INVALID_LIBRARY), fulfilled(A)]`, 总发布次数为 1, shutdown 完成后进程自然 exit 0. 此反例已关闭. 测试只使用隔离临时目录, 已清理.

[最终 6 项生命周期日志](../../task-memory/task-billfish-animation-browser/backend/startup-visibility-3.log) 全部通过, 另覆盖无 viewer 时共享 animation scope 改动会解除旧 scope 的隐藏, 不解析 Mesh. 本次定点审查未保留未关闭的 init/watch/shutdown 或库边界问题. 新 Native filter 四布局诊断也已通过. 后续 production4 无 viewer 启动恢复及四布局实机结果已完成串接, 见第 9 节.

## 9. 最终安装版证据与剩余收尾

更新于 2026-10-19 03:14, Asia/Shanghai. 本节以已读回 JSON/快照和已查看截图对齐 root 实际安装版验收, 不新增 parser/backend/viewer suite. 既有输入和代码未变化时不重跑通过的 gate. 下列早期组件/诊断证据保持自身范围, 最终状态由随后 production 证据闭合.

### 已有证据可关闭的组件范围

- Parser: [126 文件 corpus](../../task-memory/task-billfish-animation-browser/parser/corpus/summary.json) 已证明实际分类和 takes 摘要; [11 项 CLI contract](../../task-memory/task-billfish-animation-browser/parser/cli-contract/summary.json) 覆盖 auto/Maya, Max/custom, unit/axis 错误, Unicode, 坏文件和源保护. [serialized-bind](../../task-memory/task-billfish-animation-browser/parser/serialized-bind.json) 独立读 690789 个导出 vertices, 位置集合和 skin/bind 通过. [两动作动态序列化 gate](../../task-memory/task-billfish-animation-browser/parser/real-gate/correctness.json) 各检查 530 joint matrices/3070 skinned vertices, 最大 sampled vertex error 为 2.2292 mm 与 0.3256 mm, 小于既定 5 mm gate, 包括 off-key 时间. 不需要为了矩阵空白再重复这些输入.
- Viewer: [真实 FBX warm](../../task-memory/task-billfish-animation-browser/viewer/fbx-real-gate/result.json) 有 125 rows, 实际外部动作, 0.35 seconds 姿态, errors=[], warm parser/hash 全 0. [四足多 clip](../../task-memory/task-billfish-animation-browser/viewer/quadruped-offscreen/result.json), [人形](../../task-memory/task-billfish-animation-browser/viewer/humanoid-relative-check/result.json), [多足](../../task-memory/task-billfish-animation-browser/viewer/multipod-gate/result.json), [多根](../../task-memory/task-billfish-animation-browser/viewer/multi-root-gate/result.json), [256 bones](../../task-memory/task-billfish-animation-browser/viewer/large-256-bones-gate/result.json), [morph](../../task-memory/task-billfish-animation-browser/viewer/morph-gate/result.json) 的真实 WebGL smoke 均 ok=true/errors=[], fixtures 为合成而非用户真实模型. [设置 smoke](../../task-memory/task-billfish-animation-browser/viewer/settings-gate/result.json) 已记录 Maya 默认偏好, 角色 joint primary/secondary/display yaw 与独立 current-file override.
- 播放语义: [playback tests](../../automation/animation_browser/tests/ui/playback.test.mjs) 与 [viewer 实施记录](../../task-memory/task-billfish-animation-browser/viewer/implementation-and-validation.md) 对应 10 项通过检查, 包括稳定 ID/重名, 非 root translation/scale, A-B-A 默认恢复和唯一 action, 异步乱序, STEP/cubic, morph 恢复, once/end seeking 和 LRU/过滤. 本轮读回源码与既存记录, 不重新执行.
- Native: [BFDataModelFilter probe](../../task-memory/task-billfish-animation-browser/native/filter-probe.jsonl) 的四种布局排除 ID 56 后, `hidden_current_failure/hidden_hit_test_failure/hidden_proxy_mapping_failure/hidden_selection_failure` 均为 false; 可见 file 55 和 folder selection 在 refresh 后保留, 搜索命中从 1 减为 0, detach/恢复有记录. [保留检查](../../task-memory/task-billfish-animation-browser/native/filter-data-preserved.json) 确认 source/metadata 不变, native_is_hide=0, note/tag 仍在. 这些关闭诊断可行性 gate, 不把 task-local hook 当 production 部署.

### 已完成的安装版流程

- **部署和真实入口.** `Billfish.exe` 唯一入口启动, 最终 native DLL SHA256 为 `622F058F8FD4B76E5B45FF65EC2DB9A321DB178FDA4E5EA1A402C4F8F262551F`, 1,603,072 bytes; [构建身份](../../task-memory/task-billfish-animation-browser/native/production-final-build/preview-dll-4.log), [部署健康](../../task-memory/task-billfish-animation-browser/native/production-final-build/deploy4-health.json) 为 Healthy=true. 原生双击进入实际内置 viewer. [GLB 首开](../../task-memory/task-billfish-animation-browser/production4-glb-first-open.png) 已有 6 rows/8 of 8, 不再依赖手动刷新; [FBX warm](../../task-memory/task-billfish-animation-browser/production4-fbx-warm.png) 恢复 F:/TempFBX 与 125 rows/127 of 127. 不把 scanned files 数等同 clips 数. 坏 GLB 的 [受控错误](../../task-memory/task-billfish-animation-browser/native/production-corrupt-error.png) 后宿主及已有窗口可继续. DLL 身份与 host JavaScript 修复分别记录, 不把相同 DLL hash 当作 host 未变化的证明.
- **原生 card 和两种 cover.** 新 copy Mesh 58 未先打开 viewer 即在 [原生卡片](../../task-memory/task-billfish-animation-browser/native/production-fresh-mesh-card.png) 显示真实参考姿态缩略图, 有 [只读快照](../../task-memory/task-billfish-animation-browser/host/production-fresh-mesh-card.snapshot.json). B_AtkBeta01_1 的 1.504 seconds 当前帧封面和随后的参考姿态重建是两次不同操作: [当前帧 verification](../../task-memory/task-billfish-animation-browser/host/production-pose-cover-verification.json), [参考姿态 verification](../../task-memory/task-billfish-animation-browser/host/production-reference-cover-verification.json) 均确认 source/metadata 不变且 cover 内容变化. 后一次参考姿态 cover SHA `1b56fdaf3c7ce256f99c9454d51c1f435eaf365b738068c6115bebe73079f292` 在正常重启后的 [无 viewer 快照](../../task-memory/task-billfish-animation-browser/host/production4-startup-no-viewer.snapshot.json) 保持. 不声称重启时仍保留已经被用户操作替换的先前动作帧.
- **生产过滤与恢复.** 最终构建实际经过 [Grid](../../task-memory/task-billfish-animation-browser/native/production4-hide-grid.json), [List](../../task-memory/task-billfish-animation-browser/native/production4-hide-list.json), [Flexible](../../task-memory/task-billfish-animation-browser/native/production4-hide-flexible.json), [Waterfall](../../task-memory/task-billfish-animation-browser/native/production4-hide-waterfall.json), 隐藏目标搜索无 enabled material rows; 对应 `production4-hide-*-selected-result.json` 的 Ctrl+A 均返回空选区. 无 task-local probe 代替 production 过滤. [全局 off](../../task-memory/task-billfish-animation-browser/production4-global-show-selected-result.json) 可见并选中 item 56, note/tag 1/native_is_hide=0 保留; [on](../../task-memory/task-billfish-animation-browser/production4-global-rehide-selected-result.json) 恢复隐藏和空选区. 三 viewer 单项 show/re-hide 的正确 native 效果及无误报反馈亦已通过, 见下段.
- **重启, 增量与维护.** [无 viewer 启动快照](../../task-memory/task-billfish-animation-browser/host/production4-startup-no-viewer.snapshot.json) 当时只有 Billfish 主窗口可见, [启动隐藏](../../task-memory/task-billfish-animation-browser/native/production4-startup-hidden.json) 已恢复 pure animation 排除, [Maya 恢复](../../task-memory/task-billfish-animation-browser/production4-settings-restored.png) 为显式 Maya/+X/+Y. [真实 native copy 导入](../../task-memory/task-billfish-animation-browser/production5-import-new-action-result.json) 把不同 F 动作 B_AtkBeta01_1 导入为 QA item 59, mode=copy; 58 的 watcher 自动从 6 增至 7 rows 并 [实际播放新动作](../../task-memory/task-billfish-animation-browser/production5-increment-playing.png). 若内容已有本机 cache, inspect=0 是合法复用, 不改变原件来制造计数. 实际 UI [重建动画扩展缓存成功](../../task-memory/task-billfish-animation-browser/production5-cache-rebuilt.png); [前后验证](../../task-memory/task-billfish-animation-browser/host/production5-cache-profile-verification.json) 为 profilesIdentical=true, beforeProfiles=afterProfiles=3, afterRows=[125].
- **真实库边界和报告.** 切往另一个 QA 库再返回, [生命周期验证](../../task-memory/task-billfish-animation-browser/host/production5-library-lifecycle-verification.json) 确认原 host 57012 与全部旧 viewer 退出, 另库 profiles=0/associations=0, 另库 host 61180 亦在切回后退出, 返回 profiles=3; [另库快照](../../task-memory/task-billfish-animation-browser/host/production5-other-library.snapshot.json) / [回库快照](../../task-memory/task-billfish-animation-browser/host/production5-library-return.snapshot.json). 回库 [125 rows/F scope](../../task-memory/task-billfish-animation-browser/production5-library-return-viewer.png) 与 [Maya/+X/+Y](../../task-memory/task-billfish-animation-browser/production5-return-settings.png) 实际恢复. [报告 UI](../../task-memory/task-billfish-animation-browser/production5-report.png) 显示当前 source/target, [真实 clipboard 读回](../../task-memory/task-billfish-animation-browser/host/production5-copied-report.json) 为 106/106 tracks, full, unmatched/ambiguities 为空. clipboard 只复制匹配报告字段是现有合同, 不要求重复 UI 身份字段.

### 真实集成缺陷与关闭范围

早期 public row-hide 不满足 Flexible range/Waterfall geometry, 已改为认证 BFDataModelFilter 实例. 随后的 production 布局切换又发现 native 将同一个 source 地址 detach/rebind, 只比较最终指针会漏掉 hook 失效. 当前组件明确发布 bindingInvalidated, bridge 失效旧 generation/映射并重新按可信路径取得当前 IDs; production4 四布局已通过, 此项关闭.

Warm scan 早于 renderer 订阅完成时会丢 rows/complete, 旧 initialize snapshot 令真实 GLB 停在 discovering/0. Host 已在握手前保留事件并按序重放, UI 在 initial snapshot 后处理队列. 定点回归从 FAIL 变为 PASS, 独立顺序/close/shutdown 审查通过, production4 首开 GLB 无手动刷新通过, 此项关闭.

Python 封面前后比较原 stat_result 时误把 hash 读取导致的 atime 变化当作 SOURCE_CHANGED. 已只保留内容/大小/mtime/ctime/文件身份等有效保护, 不比较 atime; 16 项测试和真实 F 文件验证通过, 当前帧和参考姿态两条 production UI 路径通过, 此项关闭. 原生 cover 尺寸 key 的 invalid-height 和 bool/int32 ABI 问题亦已由实际卡片图像证明关闭.

单项显示后 native 56 已正确恢复, 但多 viewer/profile watcher 同时发布相同完整隐藏集合, 旧 host 会重复发请求并使第一个 ACK 返回 visibility_superseded. 当前 host 按同库 canonical 完整路径集合只共享在途真实 ACK, 不缓存已完成 ACK, 不合并不同集合, 不吞真实失败; 三项专项及独立 lifecycle/key 审查已通过. 新 host 56776 同时打开 55/58/54 三个 viewer 后, 58 报告中恢复自动隐藏得到 [enabled rows=0](../../task-memory/task-billfish-animation-browser/native/production5-single-rehide.json), [UI](../../task-memory/task-billfish-animation-browser/production5-single-rehide-success.png) 无红字且按钮变为单独显示; 再单独显示得到 [enabled rows=1](../../task-memory/task-billfish-animation-browser/native/production5-single-show-retry.json), [真实选区](../../task-memory/task-billfish-animation-browser/production5-single-show-final-selected-result.json) 为 item 56 且 note/tag 1/is_hide=0 保留, [UI](../../task-memory/task-billfish-animation-browser/production5-single-show-success.png) 无红字且按钮恢复为自动隐藏. 本项已关闭, H02/F01 功能验收闭合. 依据是实际 UI 成功反馈/原生效果与专项真实 ACK 断言, 不声称保存了未提供的私有 ACK 日志. 随后已恢复自动隐藏, [最终原生搜索](../../task-memory/task-billfish-animation-browser/native/production5-final-hidden.json) 中目标 56 的 enabled rows=0. 最终数据和退出保护见下段.

### 最终环境与数据保护

[最终数据核对](../../task-memory/task-billfish-animation-browser/host/production5-final-data-verification.json) 中 item 55 和 56 均为 sourceUnchanged=true, protectedMetadataUnchanged=true, nativeIsHide=0. [全部 viewer 关闭快照](../../task-memory/task-billfish-animation-browser/host/production5-all-viewers-closed.snapshot.json) 仅 Billfish 原生窗口可见; 随后通过 UI 正常退出, [退出后快照](../../task-memory/task-billfish-animation-browser/host/production5-after-normal-exit.snapshot.json) 为 processes=[]/windows=[]. [最终统一健康](../../task-memory/task-billfish-animation-browser/native/production5-final-health.json) 为 Healthy=true, recent library 保持 Billfish-AI-Enlarge-QA. 本目标开始时即为 app closed/recent QA, 已恢复这一基线, 不要求恢复早期其他任务的个人库状态.

### 文档收尾与保留边界

root 统一将 architecture/implementation plan/host checklist 中已过时的未部署或待四布局复验描述更新为上述最终证据, 并写入 task_state, 引用已完成的最终 runtime manifest/health 与环境恢复证据, 记录无 native schema 变更依据和可执行的恢复说明. 内置浏览器不是可卸载的外部插件; 恢复原始文件展示使用全局隐藏关闭/显示全部, runtime-before 备份只包含旧 DLL 与配对基线记录, 不包含全套 JavaScript 历史. 它不是完整 runtime 回滚包; 不能直接复制旧 manifest 就宣称恢复完成. 需要回退时按维护安装事务处理对应 DLL/records, 并核对实际现存 JS/Python/parser 等 runtime 文件, 通过维护流程校验或刷新实际基线后重新检查 Healthy. Cache 重建保留 profiles 已有实测, 不以删除 shared profiles 作为普通清缓存步骤.

历史 deployed 版本的格式边界: 大于四 skin influences, 非零 DQ, FBX morph/同 mesh 多 skin 等 typed unsupported; 可用 PNG/JPEG texture 与缺图 warning; GLB 外部未证 morph identity 的 partial 状态; 真实百度云同步环境未运行. 320 动作资源结果来自标明的合成 fixtures, GPU 为对象数量而非驱动 VRAM bytes, 不外推任意资产/任意时长上限. [F 原件保护](../../task-memory/task-billfish-animation-browser/assets/source-final-verification.json) 在其记录时刻核对 126 文件 size/mtime/SHA 全部保持, 之后新增动作仍通过 native copy 使用原件, 不声称该早期时间戳覆盖未来写入. 不新增 stage/commit/push 或重复 suite 作为完成条件.

## 10. 3D 加载反馈与动画切换验证, 2026-10-20

本轮增加 Solid/Material 双按钮与实际模式/加载状态, native intent 先显示 same-window shell, 常驻只读 catalog worker, 以及切换 clip 时保留上一姿态. candidate 实现/专项通过与 installed 验收分开记录. [本轮 task_state](../../task-memory/task-billfish-viewer-feedback-switching/task_state.md) 为当前证据入口, 其中已补 installed receipt 和原生双击/失败反馈结果.

| 验证范围 | 已有证据与结果 | 不能外推的结论 |
| --- | --- | --- |
| 按钮与动作切换 | [真实 Electron/GPU](../../task-memory/task-billfish-viewer-feedback-switching/renderer-switch-01210a37-0f1d-49e9-9f2d-f550a569b921/result.json), 真实 Chastener_A/171 bones, pending 保留旧 pose/canvas, 失败保持且 paused. DOM click 到 busy/commit/first paint: cold 28.6/163.3/174.1 ms, warm 31.5/64.2/76.6 ms | 独立 offscreen candidate 与脚本 DOM click, 非 trusted hardware/installed 原生双击. 本素材 Material 已驻留, 不代表大型材质首载 |
| Catalog transport | [同输入 before/after](../../task-memory/task-billfish-viewer-feedback-switching/clip-performance-findings.json), service+SHA cold 387.29 -> 104.66 ms, warm binary 125.11 -> 10.59 ms, ACK 165.95 -> 9.24 ms, snapshots 相同, children drained | 单样本, 不含 GUI/IPC/GPU, 不给平均/分位数或通用速度承诺. 每请求 fresh SQLite readonly, 不是结果缓存 |
| Early shell/source gate | [真实 BrowserWindow](../../task-memory/task-billfish-viewer-feedback-switching/early-shell-real-electron-725ea53c-8d47-4f1a-aa50-7b31f5a94cca/evidence.json), 423.89 ms 显示, 正式 prepare withheld 约 10.3 s, source 工作计数全 0, 注入 error 后同窗停 spinner | 不证明 installed owner attachment, 原生错误投递, 模型首帧或端到端提速 |
| Native build/test | native-build.exit 与 native-tests.exit 均为 0, stdout/stderr 为空 | 只记录 exit0, 不编造 29 passed 或其他用例总数 |
| 主素材 Waterfall 失败后跳尾 | Chastener_A/Maps 公开 invalid35003 及 Maps 真实双击失败前后 rows/selection 相同, 未复现. [调查](../../task-memory/task-billfish-viewer-feedback-switching/scroll-findings.md) 尚无失败直接写 scroll 的证据 | 本项未关闭, 不能声称已修复或认定 visibility refresh 是根因 |

[独立审查](../../task-memory/task-billfish-viewer-feedback-switching/independent-review.md) 已确认 candidate 的 source gate, pose retention 与 fresh catalog 边界, 并跟进了 source-changed/context 和 terminal-error 迟到 ready 修正. [安装回执](../../task-memory/task-billfish-viewer-feedback-switching/install-receipt.json) 已记录 9 runtime+7 tests, manifest 230 项; 启动前 Healthy=true. 既有 Temple 完整质量和 Raw Input 的未完成真人验收边界保持, 不把本轮性能数据回写为历史同画质提速结论.

安装态 [真实 Qt 双击](../../task-memory/task-billfish-viewer-feedback-switching/installed-native-click.json)/[时间轴](../../task-memory/task-billfish-viewer-feedback-switching/installed-native-open-timeline.json) 记录 click 到 window create 119 ms, formal preview 248 ms, show 332 ms. 旧 baseline 378 ms 且缓存条件不同, 不声称10s -> 332ms或固定比例. [Chastener实际viewer](../../task-memory/task-billfish-viewer-feedback-switching/installed-chastener-viewer.json) 有63clips/正确identity/Solidpressed; [失败后同窗](../../task-memory/task-billfish-viewer-feedback-switching/installed-failure-viewer.json)错误可见且busy=false/spinner=none. 主窗 rows bounds 和 selection exact 未变, Waterfall跳尾仍待具体失败文件名以复现.

[最终运行收据](../../task-memory/task-billfish-viewer-feedback-switching/final-runtime-receipt.json)/[最终统一健康](../../task-memory/task-billfish-viewer-feedback-switching/installed-unified-final.json) 为 Healthy=true/230 manifest, preview.dll SHA256 70123E5A5C576752B65A5A36EB9E496EB2258F6896C3EB6048B516769C1BFA19. 已恢复 Chastener_A/32546 与可见63clips viewer, 主列表 rows/bounds/selection 同起点, Inspector/trace清理完成. 3.96GB item35004实际成功Solid显示, 真实进行中截图有spinner/checking-source, 仅补证等待反馈, 不证明失败或完整Material质量. 本轮1/2/4已安装验收, 第3项Waterfall跳尾仍待具体失败文件名, 不标整个任务完成.

## 11. 重启后的库 context 与首个素材输入, 2026-10-20

用户反馈重启后 FBX/GLB 双击先进入原生图片查看. 本轮已修复 NativeAdapter 漏发布 ready root, 以及 discover tick 前真实素材 view 输入未绑定的窗口, 完成安装和下列验收, 见 [startup task_state](../../task-memory/task-billfish-viewer-startup-routing/task_state.md).

| 范围 | 当前证据与结论 | 保留边界 |
| --- | --- | --- |
| 缺陷与 API 补救 | [20 秒被动启动](../../task-memory/task-billfish-viewer-startup-routing/before-passive-startup.json) 无 builtin, [plugins.list](../../task-memory/task-billfish-viewer-startup-routing/before-passive-plugins.json) 为 waiting-context/空 root. 随后 [host.info](../../task-memory/task-billfish-viewer-startup-routing/before-explicit-host-info.json) 返回 ready/state2/非空 root, [Electron](../../task-memory/task-billfish-viewer-startup-routing/before-after-api-process.json) 约 190 ms 后启动 | 已证库 context 未发布, 不将 parse 时间或未捕获 vendor signal 时序当作原因 |
| 修复与构建 | [diff](../../task-memory/task-billfish-viewer-startup-routing/startup-routing.patch) 仅两份 Native .cpp, [独立审查](../../task-memory/task-billfish-viewer-startup-routing/independent-startup-review.md) 无源码阻断, build/tests exit 0 | QtTest 日志为空, 不编造用例数, 不替代真实首击 |
| 安装及自主启动 | [回执](../../task-memory/task-billfish-viewer-startup-routing/install-receipt.json) DLL 002C700D..., 230 manifest, Raw/JS/Python 保持; [启动前健康](../../task-memory/task-billfish-viewer-startup-routing/installed-unified-before-launch.json) Healthy=true. [进程时间](../../task-memory/task-billfish-viewer-startup-routing/after-autonomous-process.json) 确认 builtin 于 launcher 后约 9.236 秒自行创建, 早于首个 API | [8 秒采样](../../task-memory/task-billfish-viewer-startup-routing/after-passive-startup.json) 尚未采到该进程, 不写成 8 秒内启动或立即 ready |
| 首个被采样为可用的 FBX 输入 | [权威 first-usable](../../task-memory/task-billfish-viewer-startup-routing/first-usable-result.json) ok=true, +9839 ms 首次 target, +9997 ms Qt 双击入队, +10954 ms plugin_viewer_rendered, item33195, 无 native-image-fallback. [真实 viewer](../../task-memory/task-billfish-viewer-startup-routing/first-usable-fbx-viewer.json) source 正确/busy=false | UI connect 会经 currentInfo, 自主启动另由被动轮证明. 不等于最早可能硬件输入或所有 starting gate 覆盖. 带额外 status query 的旧结果不混计 |
| 普通图片和 GLB | [PNG 原生图片](../../task-memory/task-billfish-viewer-startup-routing/png-native-after-ui.json) 正常; [GLB](../../task-memory/task-billfish-viewer-startup-routing/glb-viewer.json) item34683 真实 geometry/busy=false, 主窗无 native 图片 fallback | GLB 为 warm 正常双击回归, 不是单独 cold first-click |

[最终收据](../../task-memory/task-billfish-viewer-startup-routing/final-runtime-receipt.json)/[最终 health](../../task-memory/task-billfish-viewer-startup-routing/installed-unified-final.json) 为 Healthy=true/230 manifest/002C700D...43B95. 已恢复 YoungAbbess/33195/空搜索与 [真实可见 viewer](../../task-memory/task-billfish-viewer-startup-routing/final-young-viewer.json), 源 FBX SHA/size/mtime 保持, Inspector/调试端口清理. 约9.236秒进程创建和另一轮10.954秒 rendered terminal 不构成通用模型时延承诺. 本轮 startup 验收关闭, 上一轮 Waterfall 失败后跳尾保持开放, 既有 Raw 真人验收边界保持.
