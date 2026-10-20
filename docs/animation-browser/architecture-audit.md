# Billfish animation browser architecture audit

本文保留维护过程中的历史设计和本机 QA 记录. `task-memory` 链接指向未随源码发布的本地验收材料, 公开仓库中不可访问. 当前功能以 [README](../../README.md#3d-models-and-animation) 和 [协议的最新章节](protocol.md) 为准; 历史轮次中的 pending 或完成状态仅描述当时的验证范围.

## 长动作, 时间轴与侧栏导航新请求, 2026-10-19

本轮实现, 安装与验收完成, 下方已完成轮次记录保持原时点含义. 用户新增五项: 优化长动作切换3-4s停顿; 在原时间下方增加所选速度对应的实际 current/total; 拖动时间轴暂停并保持暂停, 直到点击播放或Space; 左侧栏始终提示当前目录; 侧栏填满/overflow时从中间子目录导航避免整列跳动. 最终性能after, transport/sidebar验收, 组合安装及Healthy均已完成, 文档与生产source已freeze.

实际时间显示为 source current/speed 与 source total/speed, 保留原始时间和帧语义. 2x显示一半, 0.5x显示两倍. 拖动开始即pause, seek后及释放/取消仍保持paused, 显式播放或时间轴焦点下Space才继续; 时间轴保持焦点不应阻止thumb跟随. 不劫持搜索/数值输入框中的Space.

侧栏当前项提示使用真实model index selection/currentIndex, 避免复用delegate的stale selected状态. Overflow导航候选在expand后映射真实flattened row并用 `ListView.Contain`, 已可见目标不移动, 屏外目标最小揭示; 不是重排目录顺序, 不改普通scrollbar handlers. Sidebar final normal-cache实机验收, 最后组合安装及health均通过.

[本轮 task_state](../../task-memory/task-billfish-long-clips-transport-sidebar/task_state.md), [长动作before](../../task-memory/task-billfish-long-clips-transport-sidebar/long-before-latest.json), [transport candidate](../../task-memory/task-billfish-long-clips-transport-sidebar/transport-candidate.json), [sidebar实现](../../task-memory/task-billfish-long-clips-transport-sidebar/sidebar-evidence/implementation.md) 记录当前进展. 既有短动作性能结果不能替代此次长动作before/after.

实际before长clip分段中warm main load约1.979s, IPC clone/preload约1.859s, renderer bind首pose约4.8ms, 因而优化应基于传输/数据处理测量, 不能直接归因为skin GPU. Sidebar原handler强制Beginning与delegate stale highlight已由真实overflow前后截图复现, 目录兄弟顺序未变.

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

独立审查指出 stock Three 只对首组 skinWeight 做 normalize, GPU skinning shader 与 CPU applyBoneTransform 也只覆盖四权重. 候选需 paired parser/exporter, GLB group validation, loader accessor ownership, GPU/CPU full LBS 与 bounds 一致修复; 保持 skeleton overlay 现有层级和资源 ownership. 实际预算/compile/link,真实资产与synthetic seek/morph已有下方GPU run2证据, 正式部署, native33726打开及最后外部动作实件检查均已PASS.

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

## 骨架显示架构补充, 2026-10-19

`ui/skeleton-overlay.mjs` 只读取实际 `SkinnedMesh.skeleton.bones` 的 isBone joints. Pinned Three.js `SkeletonHelper` 显示实际 bone-parent connections, joint points 表达 single-joint skin 和 independent roots, 不以任意 Bone hierarchy 推断可显示 skeleton.

Helper lazy create 并作为 scene sibling, positions 源于当前 bones.matrixWorld, 在原 Three/Core render loop 内更新, 因而 play/seek 使用当前 pose, display transform 不重复应用. 开关默认关闭, static 模型隐藏. Disable 移出 scene, re-enable 复用 owned buffers, viewport dispose 释放 helper geometries/materials, 不改 source/model hierarchy/skin/material/tracks 或模型构图 bounds.

Isolated Core adapter 仅序列化 `hasSkeleton`, `skeletonVisible` 和 concrete `skeletonOverlay` capability; 共享 UI 等待 `setSkeletonVisible(boolean)` 的真实 ACK. Core cleanup 与 helper ownership 留在 preload, 不向主页面传 Core/Three objects, 不增加第二 animation loop. Core 与 overlay 的独立 Three instances 已由实际 skinned POC/production smoke 验证; 这不外推所有格式的 skin interoperability.

骨架显示 renderer/host 实现已冻结. [Renderer implementation](../../task-memory/task-billfish-gameexport-skeleton/renderer-implementation.md), [Core host contract](../../task-memory/task-billfish-gameexport-skeleton/host-core-skeleton-implementation.json), [实际 Core production proof](../../task-memory/task-billfish-gameexport-skeleton/core-skeleton-poc/production/result.json). Focused 13 tests 与 Core facade/UI 15 tests PASS; 实际两骨骼 skinned GLTF 的 Core POC 和 production renderer smoke PASS. Production proof 使用 task-only synthetic source, 不等同于 native imported item 的整体验收. 本轮 GameExport 漏 FBX 原因与修复仍由 root 调查, 此处不提前给结论或宣称整体任务完成.

## 当前架构修订, library-only unified 3D

更新: 2026-10-19. 本节是当前架构边界, 下方外部目录和多 viewer 记录保留为旧轮次审计证据. 当前 FBX/GLB 路线已安装验证, 原 Core 格式已接入同一维护插件, 旧普通 3D 插件已禁用并归档移出扫描目录. 注册验证为单个 enabled `billfish.animation-browser`, 名称 `3D 预览`, 保留原全部 25 viewer / 23 thumbnail extensions, HDR/EXR 仅 viewer. OBJ/GLTF production smoke run 4, 3dm WASM targeted run 5 和 reference pose run 2 PASS; 不宣称逐一实测了全部 25 格式. 原生 OBJ orbit/封面, GLTF 动画/seek/当前帧封面, FBX/GLB 单窗口和真实切库返回均已通过. 最终 source/protected metadata 与最新 cover 保持, 主应用保持打开于 QA 库, viewer 已关闭, 原生搜索为空.

- 预览继续通过 native double-click -> 可信 native item/library binding -> Billfish-owned Electron 插件弹窗. 不改为主窗内嵌页或独立应用. 当前维护 UI 统一显示常驻动画侧栏; 无动画模型仍渲染, 列表为空, transport disabled.
- 候选身份来自当前库 read-only catalog snapshot, 不是 Mesh parent 或用户选择的外部目录. Python `animation_snapshot()` 使用一个只读 SQLite transaction 取得 folders 和非回收 FBX/GLB items, 包括 native hidden 和动画过滤器已隐藏成员. semantic revision 不依赖 DB mtime, 不把 UI hide 当作成员删除. 路径根据当前库归属重建, 无法解析的 linked/无效成员保留 typed diagnostic.
- Main 绑定当前库 root 并调用 bundled helper, 验证完整快照/ID/owned path 后注入 backend `getLibraryCandidates`. Renderer 无权提供 catalog 或 arbitrary scope. Backend 重验 base 和 clip 的 `sourceItemId`/path, 所有候选来源为该快照, 不遍历外部目录或磁盘未注册邻居. 跨原生文件夹是正常候选范围.
- 对非 skinned Mesh 仅显示自身 embedded clips, 对 skinned Mesh 匹配当前库动画. session schema 4 和 `billfish-catalog-1` 阻止旧外部关联 warm restore; 仅恢复仍在当前 catalog 且 fingerprint 有效的 rows. `profile.scope` 被有效配置剔除, 旧 immutable revisions 及 Maya/Max/custom/joint/facing/manual associations 保留. 新 file override 按 native item ID 保存.
- 宿主仅保留一个可见 unified viewer slot. 同 item 的真实完成结果和完整 source identity 未变才复用; 新 item/source 先清旧 owner/window/session, generation guard 阻止旧异步操作夺回窗口. 后台 thumbnail 不占此 slot. 实机同 item 60 复用, item 51 替换且显示空动画列表已通过.
- 原 Core/resources/licenses 已进入维护插件, 同一 UI 通过窄 adapter 使用其 renderer, 不混合 ufbx 与 Core 骨架. 资源 provenance/freeze 及 OBJ/GLTF/3dm 和 reference pose 代表性执行已通过. 旧插件 `89fb801f-7761-4de6-b7ce-3785a084edaf` 已禁用, 确认 Billfish 及插件进程均已退出后归档至 Billfish automation/removed-plugins, sourceAbsent/archivePresent 均 true. 重启后单个 enabled builtin 名为 `3D 预览`, 原全部 25 viewer / 23 thumbnail extensions 保持, HDR/EXR 仅 viewer. 全格式注册保留与已实测代表格式分开记录. [Core freeze](../../task-memory/task-billfish-library-animation-mode/host/core-freeze.json), [旧插件移除](../../task-memory/task-billfish-library-animation-mode/old-plugin-removal.json), [注册验证](../../task-memory/task-billfish-library-animation-mode/unified-registration-verification.json)
- 当前 native `preview.dll` 保持 SHA-256 `622F058F8FD4B76E5B45FF65EC2DB9A321DB178FDA4E5EA1A402C4F8F262551F`, 1,603,072 bytes. 本轮 catalog 变更不需要 native schema migration 或 DLL 重建. 当前 runtime 222 fingerprints, health-before-final-launch Healthy=true, 唯一根入口重启后 unified registration PASS; 最终原生 orbit/封面/单窗口/切库返回和保护数据均已通过, 详见 requirements-and-validation.md 本轮最终验收. 主应用保持打开 QA 库, 空搜索, viewer 已关闭, 不宣称 root 正常关闭或 app 已退出.

本轮测试依据: 34 backend, 15 Python catalog, 5 focused host 和 最新 7 UI tests PASS, 包括每次打开默认只显示 full compatibility, 另有独立代码审查 PASS. 实机证据和剩余 gate 见 [library-only task_state](../../task-memory/task-billfish-library-animation-mode/task_state.md). 历史 125 external actions/320 synthetic clips 不等于本轮 catalog 成员数量或当前用户流程.

[最终统一健康](../../task-memory/task-billfish-library-animation-mode/final-health.json) 为 `Healthy=true`, manifest 222 项且 native DLL unchanged.

## 旧轮次架构与证据

以下 Date/Scope 和 production4/production5 结论保持原时点含义, 不覆盖上方修订或尚未完成的迁移.

Date: 2026-10-19. Scope: crash repair and the full built-in character animation browser requested by the user, including the supplied plan, F:/TempFBX, and persistent Maya/Max axis settings.

## Current runtime and editable boundaries

- The supported entry is E:/Billfish/Billfish.exe. The installed unified status was Healthy=true before this work; no DPI/HDR/AV1 reinstallation is required.
- Billfish.core.exe and vendor DLLs are frozen native Qt software. The maintained C++17/Qt 5.15 preview proxy, automation host, shell integration, Python catalog helpers, and Electron runtime are editable source.
- Native folder/index/material UI remains in vendor modules. This work must preserve native records and selection/model ownership, and use established native actions for writes.
- 3D viewers are separate Billfish-owned Electron windows. The maintained candidate registers `billfish.animation-browser` for FBX/GLB independently of Eagle package state. The broad-format Core route remains for other formats. The legacy preview used during diagnosis is not evidence that the final built-in candidate has passed native E2E.
- Existing source has uncommitted user and earlier task changes. There is no authorization to stage, commit, push, or reset them.

## Proven integration points

- `_video-trim-patch/src/automation/format_preview_bridge.cpp` observes the current native view and handles double-click/Enter/Space. It resolves a real native item ID and current library before dispatching preview work.
- `native_adapter.cpp` identifies native rows through role 0x164 with validated IDataItem/FileItem identities. All four material layouts inherit QListView, but the real `setRowHidden` gate failed: Flexible Shift-range included a hidden item and Waterfall retained its geometry/hit target. That route is rejected for production.
- The accepted native boundary is the current `BFDataModelFilter` instance, a `QSortFilterProxyModel` over the same native source. `animation_visibility.cpp` clones its complete primary vtable, including the RTTI locator and all 63 slots, and changes only `filterAcceptsRow` at +0x190. A verified, excluded source FileItem returns false before the original predicate; every other row follows the original function. It preserves the source/proxy/view/selection-model objects and does not write `is_hide` or replace a model.
- Refresh uses the recovered native `slotFileOrFolderHidden()` to reset the vendor counters and rebind the same source. All affected view selections/current items are saved as source persistent indexes, restored only if still valid/visible, and the current view is restored last. Generic `invalidate()` is not used because it does not establish the native counter reset.
- `format_preview_bridge.cpp` consumes complete path snapshots through the read-only Catalog mapper. IDs remain bound to the actual library owner, proxy and source. A changed binding drops old IDs before remapping; late helper results must match their captured owner/source and generation. Component invalidation explicitly marks the bridge dirty because a native layout change can unbind/rebind the same source pointer. The next discovery performs a fresh mapping instead of relying only on final pointer equality. Removed paths are unhidden before asynchronous remapping. Stop/crash, library change and shutdown clear the native exclusion state.
- `format_preview_bridge.cpp` already has a visible-viewport-driven thumbnail queue, explicit viewer priority and one active publication task. Reuse its native custom-cover action and fresh verification, not direct database/image slot writes.
- `automation/plugin_runtime/preview-runtime.cjs`, `main.cjs`, `window-owner.cjs` and the private cover channel provide authenticated viewer lifetime, Billfish window ownership and verified frame publication. Preserve those boundaries for the new built-in renderer.

## Database and file identity

- The current catalog is the library SQLite database, accessed read-only through Catalog and helper snapshots. File IDs and folder IDs belong to the native importer; the extension does not allocate substitutes.
- Paths are reconstructed from the owned library root, folder chain and filename. Metadata, notes, tags and native cover state stay in existing native tables.
- Native API file import currently uses ImportCopy. It records no authoritative original filesystem path in the persisted catalog; origin is a user-editable text field.
- Consequently copying only a Mesh into Billfish cannot recover its original animation neighbors. Default discovery will use the actual Mesh parent directory, with an explicit persistent animation-folder selection for external assets. Do not infer F: from a filename or origin.
- Linked/indexed imports are not yet a supported preview path. ImportIndex=3 is only a known enum, not a recovered end-to-end contract.
- New file metadata, parser output and association caches will be separate local extension storage. No native schema migration or deletion of native resource records is required.

## Thumbnail and synchronization boundaries

- Native custom covers are stored under the library `.bf/.preview/<bucket>/<id>.cover.<ext>` and are selected by native cover state. These files can be synchronized with the library.
- New parse, normalized Mesh and runtime clip caches must reside under the current user's local Billfish cache, not in a shared SQLite file.
- Manual association and coordinate profiles are not disposable cache. They require versioned records, atomic publication and preservation/detection of concurrent branches. Cache rebuild must retain them.
- The real native cover failure was isolated beyond PNG generation and cache loading: the saved cache pixmap contained the correct robot, while the actual paint requested a key with height `INT_MIN`. The frozen Waterfall delegate divides zero non-image source dimensions when constructing the request. V5 diagnosis confirmed that normalizing only this request to the existing square size restores the robot and leaves ordinary JPG/MP4 display intact.
- `native_cover_geometry.cpp` implements that narrow repair at the cache manager's +0x70 key-builder boundary. Only the verified paint callsite, an FBX/GLB FileInfo with a custom cover and missing image dimensions, positive requested width, and `INT_MIN` height qualify. It passes a local `QSize(width,width)` to the exact original builder. Its fifth argument is int32, as read by the frozen implementation. No stored dimensions, cache bytes or native metadata are changed. Production contains no fetch observer, task logging, diagnostic takeover or timeout.

## Native modules and frozen guards

The candidate's relevant files are `plugin_manager.{hpp,cpp}`, `format_preview_bridge.{hpp,cpp}`, `native_adapter.{hpp,cpp}`, `animation_visibility.{hpp,cpp}`, `native_cover_geometry.{hpp,cpp}`, `automation_controller.cpp`, `automation_ui.cpp`, and `automation/billfish_api/previews.py`. The built-in descriptor remains `automation/animation_browser/manifest.json`; renderer/main/backend ownership is separate from the native ABI implementation.

| Frozen module | SHA-256 | Narrow boundary |
| --- | --- | --- |
| `ui_plugins/datamodel.dll` | `4d47008537fd72ccbe63f84c87b1b822f661e1f83186d56acbae37cf3af3d244` | Filter vtable 0x15488, predicate 0x92f0, native refresh 0xb760, real source FileItem role |
| `ui_plugins/viewmodule.dll` | `ad1e8e407433f8028ad7418afa485ff3a80dfabded2e4f65b4aece283ced57db` | Current material views; cover paint builder return address 0x383c8 |
| `ui_plugins/basemodule.dll` | `d2fdefec8f03a806c44507c5030a0470b121ba5a3d4d720af93ecf6d1d867bf1` | Cache manager vtable 0xde28, 25 slots plus locator; original key builder 0x3440 |
| `ui_plugins/datacenter.dll` | `c3086ed8efce3d2dc11a482b2798eaa1b5f171970b05a681015bece4d5b27f09` | FileInfo vtable 0x13b7d0 and extension getter 0x2d8c0 |

Existing NativeAdapter guards for the core, interfaces, data-center manager and library remain in force. The diagnostic evidence is offline PE/disassembly plus real Qt execution, not a claim of REA retained decompilation evidence. Unsupported bindings stay unfiltered instead of receiving an unverified hook.

## Parsing, coordinates and actual assets

- F:/TempFBX contains one 13,977,920-byte Mesh FBX and 125 zero-Mesh animation FBX files. All 126 declare 106 bones with the same names and parent topology. The separate asset report contains the exact structural evidence and original SHA/stat baseline.
- All supplied files declare current Y-up, positive X coord, positive Z front and centimeter units. OriginalUpAxis=Z and root PreRotation=-90 degrees X also exist. OriginalUpAxis is provenance, not another conversion to apply.
- Initial local TRS differs between files because it can represent the initial animation pose. It is not by itself proof of a different joint orientation or incompatible bind space.
- The reference fbx-anim-merger is pinned at 0a577e1d7b5af59b615b8eed104e165f821192b7, MIT. Its ufbx evaluate/bake approach is useful. Its name-only matching, first-wins duplicate mapping, 200-bone renderer, root-only translation, ignored scale and hip-height retarget defaults are unsuitable as this application's contract.
- Scene basis/units, joint local/bind basis and display orientation must be separate. Maya is the fallback preset when reliable information is missing, and the default manual preset; actual metadata and explicit user overrides are recorded separately. No automatic cross-character retargeting is planned.

## Observed failure and production evidence

- The supplied Mesh references 11 absent textures. The old adapter treated any visible Core diagnostic as fatal after loadFile succeeded. The responsible implementation owner reproduced and corrected this error boundary; a real Mesh frame and successful Billfish double-click now exist.
- This proves a specific preview failure, not the cause of the original unexplained native exit. The debugger run ended normally without an access violation, failfast or heap-corruption capture; no crash dump was produced. Controlled normal/bad-file open, close and reopen checks passed. A corrupt GLB returns `Invalid GLB header`, leaves the core and existing viewers alive, and a subsequent valid GLB opens. These observations bound the current failure behavior without establishing the original crash root cause.
- Installed production `preview.dll` is 1,603,072 bytes, SHA-256 `622F058F8FD4B76E5B45FF65EC2DB9A321DB178FDA4E5EA1A402C4F8F262551F`. Production4, without diagnostic probes, passed Grid/Tabulation/Flexible/Waterfall filtering and Ctrl+A selection exclusion after the explicit binding-invalidation correction. Global show restored item 56 and its selection, and re-hide removed both. No-viewer startup restored the persisted filter. Actual FBX warm opening restored 125 rows and Maya settings; the GLB first opening delivered its six rows without manual refresh. Current-pose cover and reference-thumbnail regeneration preserved source/metadata and changed the cover, with native imagery confirmed. C++ plugin tests passed 28 and Python preview/helper tests passed 16; diagnostic range/current/source-replacement evidence remains separately identified.
- Production5 passed actual incremental discovery/playback, derived-cache rebuild with all three profile files unchanged, library switch with the old host/viewers gone, and return with three profiles restored. Evidence is `host/production5-increment.snapshot.json`, `host/production5-cache-profile-verification.json`, `host/production5-library-lifecycle-verification.json`, and the return-viewer/settings images. A repeated identical visibility publication exposed a host request race: an old request reported `visibility_superseded` although the newer request had applied. The host now shares one real in-flight ACK for the same canonical complete path set; different sets and genuine errors remain failures. Its three regression cases pass. The corrected host PID56776 then passed the actual three-viewer per-item re-hide/show rerun: native rows changed from 0 to 1, item 56 was actually selected, and no UI red error remained. Automatic hiding was restored afterward. Evidence is `production5-single-{rehide-success,show-success}.png` and `native/production5-single-{rehide,show-retry}.json`.
- Root saved the previous native runtime and paired manifest records in `task-memory/task-billfish-animation-browser/runtime-before/{preview.dll,prepared-manifest.json,portable-runtime.json}`. These are native rollback materials, not a complete backup of all maintained JS/Python sources. After normal application exit, maintenance must restore the selected native runtime, reconcile its actual runtime fingerprints with the matching baseline and recheck unified health. Restoring old baseline JSON alone does not restore changed runtime files. No one-command full browser rollback is claimed; user profile revisions and source assets are not reverted.

## Close, rollback and format boundaries

- Closing a viewer uses Electron `closed` -> runtime `close(o)`: remove its owner, revoke its asset token, close its backend session and destroy its window. It does not disable the library verifier or other viewers. Runtime shutdown closes all owners and stops the backend; PluginManager library rebinding stops the old host before starting the new library context. Native owned exclusions clear on builtin stop/exit.
- The builtin is read-only and always enabled: `PluginManager::setEnabled()` returns `builtin_read_only`. There is no plugin-manager disable switch for this feature. The viewer window close button closes one viewer; normal Billfish exit stops the host. Showing associated files is controlled by the visibility settings. The saved native runtime/baseline pair supports a maintenance rollback after normal exit, with actual runtime fingerprints reconciled before the unified health check.
- Historical deployed parser/renderer boundary: Builtin input is FBX/GLB. Other formats retain the existing Core route. FBX nonzero dual-quaternion weights, more than four skin influences, multiple skin deformers on one mesh and FBX morph geometry return typed unsupported errors. GLB external buffers, Draco data without a bundled decoder and secondary skin influence sets are rejected. GLB supported morph animation is distinct from unsupported FBX morph conversion. No automatic cross-character retargeting is claimed.

## Evidence

Final restoration evidence: all three viewers closed normally, leaving only the native window (`host/production5-all-viewers-closed.snapshot.json`), then normal Billfish exit left no recorded related processes/windows (`host/production5-after-normal-exit.snapshot.json`). Final item 55/56 checks preserve source and protected metadata with native `is_hide=0` (`host/production5-final-data-verification.json`). Automatic hiding was restored before exit (`native/production5-final-hidden.json`: no matching material row or selected item). `native/production5-final-health.json` reports `Healthy:true`; the recent library remains the QA library.

- `task-memory/task-billfish-animation-browser/native-audit.md`
- `task-memory/task-billfish-animation-browser/native/visibility-public-api-gate.md`
- `task-memory/task-billfish-animation-browser/native/filter-direction-review.md`
- `task-memory/task-billfish-animation-browser/native/filter-probe.jsonl`
- `task-memory/task-billfish-animation-browser/native/filter-data-preserved.json`
- `task-memory/task-billfish-animation-browser/native/cover-fetch-probe.jsonl`
- `task-memory/task-billfish-animation-browser/native/cover-key-v5-live.png`
- `task-memory/task-billfish-animation-browser/reference/asset-reference-findings.md`
- `task-memory/task-billfish-animation-browser/assets/fbx-structure.json`
- `task-memory/task-billfish-animation-browser/assets/source-baseline.json`
- `task-memory/task-billfish-animation-browser/crash/`
- `docs/animation-browser/requirements-and-validation.md`
