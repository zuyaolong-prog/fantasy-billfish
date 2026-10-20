# Animation browser internal contracts, catalog policy 1

本文包含当前接口约定和带日期的历史验收记录. `task-memory` 链接指向未随源码发布的本机 QA 材料, 公开仓库中不可访问. 功能概览见 [README](../../README.md#3d-models-and-animation); 具体行为应结合本文件相应主题的最新章节阅读, 不将历史轮次的 pending 或局部验收外推为当前全部状态.

## Three progressive model modes and Walk, 2026-10-20

本轮契约已实现并安装, navigation run04历史验收通过. 安装后34985真人trace已确认大trusted delta与旋转的精确因果, 最小Raw请求改动已安装, 真人Raw lock和效果仍pending, 当前状态见[Temple task_state](../../task-memory/task-billfish-temple-performance-navigation/task_state.md). 普通Three viewer以`solid`开始, thumbnail以`material`开始. Geometry payload只移出image-only ranges, 保留原geometry/accessors/nodes/skin/morph及精确primitive identity; 完整材料描述与原image ranges由同一owner/token/freshness边界按需提供. 不按mesh name猜材质绑定, 不降低原image resolution.

Walk入口现调用`requestPointerLock({unadjustedMovement:true})`, 不支持/权限/激活/其他错误显式失败, 不降回adjusted输入, 不自动重试. 仍需真实用户gesture和浏览器实际lock确认, 请求option不等于native Raw已verified. 无大delta过滤/新增clamp/敏感度或Shift/渲染变更. 八项focused及安装readback/229manifest/Healthy通过, 真人Raw效果待实际trace, 见[stability task_state](../../task-memory/task-billfish-navigation-stability/task_state.md). 已存第一snapshot及原viewer6的trace; 用户换窗后两次RAM热更安全拒绝, 实际未热更/刷新/移动camera, 后续新viewer自然load. Run04及下方旧能力验收不证明本次Raw效果.
支持模式的viewport提供`setRenderMode('solid'|'material')`与真实`renderModeState:{mode,requestedMode,materialState,error}`. `mode`是实际显示, `requestedMode`是最新意图, `materialState`区分idle/loading/ready/error. Material异步加载后不得覆盖用户后来的Solid意图; 资源失败不报告Material ready. Three ready payload带实际`renderMode`/`materialState`; Core无此能力时不增加伪造字段. `plugin_viewer_rendered`可对应明确Solid几何首帧, 不表示完整材质已准备好; 完整Material必须另看真实状态.

自动thumbnail须完整Material ready再capture/完成, 手动cover须等待当前viewport Material ready并再次确认模式, 失效或用户更改模式即拒绝本次Solid capture. 手动操作临时暂停/模式恢复不覆盖等待期间新的模式意图. 原native fingerprint/pose/metadata验证不变. culling仅适用于保守认证的rigid/non-shear对象, skin/morph/动态/shader风险对象跳过; 不更改alpha/DoubleSide/depthWrite或依赖冻结动态matrix.

Walk以真实Pointer Lock作为active前提, 进入pending不等于已启用. 仅视口焦点内Shift+Backquote或显式按钮触发, WASD/EQ/Shift5x/滚轮/鼠标控制, Enter/左键commit, Esc/右键cancel恢复进入前camera/target. 失锁或失焦退出并清键, active期间暂停OrbitControls, 退出后恢复. Core不支持时UI禁用. 相机和controls不跨preload, 不新增公共test automation接口. 最终run04已验证实际Pointer Lock/trusted relative mouse, Shift约4.93x, commit/cancel/blur/input/dispose与Material切换保持Walk, 13/13 PASS; run02不足仅是历史验证限制. 安装态真实preview完成时已返回solid/idle, 不把此ready扩展为完整Material ready.

## 私有viewer intent/cancel与当前请求语义, 2026-10-19

Native在最新builtin viewer选择被接纳时发送 `{type:"preview_intent",id:intentId}`, render request携带同一 `viewer_intent`. Native丢弃旧queued viewer并向旧active child发送 `{type:"preview_cancel",id:requestId}`. Main仅将消息路由到builtin runtime. Intent ID为有界非空string, cancel按exact request identity匹配, 旧cancel不得关闭新窗口. 公共preview可省略pluginId, handler由native按格式owner dispatch, 不由renderer猜测.

Runtime在source/reuse fingerprint或owner/window创建前建立viewer context, 保存intent/generation/AbortController. Admit新intent失效旧pending contexts, stale supplied intent返回 `preview_superseded`. Source hashing/支持的filesystem读传播abort; initialize, service.openSession, show/focus, source复用和rendered ACK在各异步边界确认context仍为current. Completed same-source复用仍验证hash/bytes/mtime等真实identity. 新intent不使旧completed结果绕过current检查重新show. Exact cancel只撤销其owner/session/asset token, 不操作新请求.

`preview_superseded`是旧请求受新选择替代的明确结果, 不是成功或完整模型已render. 只由实际current model rendered后完成 `plugin_viewer_rendered`. 提前显示窗口和阶段不改变完成语义. Checking-source使用main绑定真实file/library/directory; fingerprint完成后、service open之前进入preparing-model, loader实际工作用loading. 旧上下文不能向新窗口发布进度或ready.

Viewer latest-wins只改变viewer工作, 不抢占已active非viewer任务. 同步parse在event loop返回前不能立即取消, 不保证无条件零等待. Thumbnail不占可见viewer slot. Runtime shutdown/关闭中旧异步结果不得注册session或创建late window.

Viewport按当前load收集真实resource errors, 失败清理场景并拒绝ready; disposed/superseded load不得late success. 合法embedded blob和service转成data URI的图像允许self/data/blob fetch, 无http(s)扩权. GLB编码移除大临时buffer仍保留全部输出bytes, 不是压缩/降质契约. 完整质量timing与early identity证据分开, latest请求正确性不构成打开速度SLA.

当前安装和验证见 [3D followup task_state](../../task-memory/task-billfish-3d-open-followup/task_state.md). 本节覆盖下方旧轮次单窗口描述的请求排队/取消细节, 其余source/cover/profile边界保持.

## 长动作, 时间轴与侧栏导航新请求, 2026-10-19

本轮实现, 安装与验收完成, 下方已完成轮次记录保持原时点含义. 用户新增五项: 优化长动作切换3-4s停顿; 在原时间下方增加所选速度对应的实际 current/total; 拖动时间轴暂停并保持暂停, 直到点击播放或Space; 左侧栏始终提示当前目录; 侧栏填满/overflow时从中间子目录导航避免整列跳动. 最终性能after, transport/sidebar验收, 组合安装及Healthy均已完成, 文档与生产source已freeze.

实际时间显示为 source current/speed 与 source total/speed, 保留原始时间和帧语义. 2x显示一半, 0.5x显示两倍. 拖动开始即pause, seek后及释放/取消仍保持paused, 显式播放或时间轴焦点下Space才继续; 时间轴保持焦点不应阻止thumb跟随. 不劫持搜索/数值输入框中的Space.

侧栏当前项提示使用真实model index selection/currentIndex, 避免复用delegate的stale selected状态. Overflow导航候选在expand后映射真实flattened row并用 `ListView.Contain`, 已可见目标不移动, 屏外目标最小揭示; 不是重排目录顺序, 不改普通scrollbar handlers. Sidebar final normal-cache实机验收, 最后组合安装及health均通过.

[本轮 task_state](../../task-memory/task-billfish-long-clips-transport-sidebar/task_state.md), [长动作before](../../task-memory/task-billfish-long-clips-transport-sidebar/long-before-latest.json), [transport candidate](../../task-memory/task-billfish-long-clips-transport-sidebar/transport-candidate.json), [sidebar实现](../../task-memory/task-billfish-long-clips-transport-sidebar/sidebar-evidence/implementation.md) 记录当前进展. 既有短动作性能结果不能替代此次长动作before/after.

本轮transport候选保持原clip时间/seek单位, 新实际时长行仅由有效speed派生, 不是重写动画时间或降低采样. 暂停保持必须同时适用于Three ClipPlayer与Core facade, 不依赖blur timeline来恢复thumb更新. 最终实现契约以真实DOM验证和配对部署证据收口.

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

候选 protocol 保持 parser request/response version 1, parserVersion 升至 billfish-ufbx-2 以使受影响 derived cache 区分版本. 多组 attribute 必须成对/连续且数值有效, 原始全部影响权重统一归一化; 实现是否已满足该 contract 以最终 review/GPU evidence 为准. 现有低影响数路径和非零 DQ 拒绝保持.

当前协议更新于 2026-10-19, library-only FBX/GLB 路线已验证. 原 Core 格式已接入同一维护插件, 旧普通 3D 插件已禁用并归档移出扫描目录. 注册验证为单个 enabled `billfish.animation-browser`, 名称 `3D 预览`, 保留原全部 25 viewer / 23 thumbnail extensions, HDR/EXR 仅 viewer. OBJ/GLTF production smoke run 4, 3dm WASM targeted run 5 和 reference pose run 2 PASS; 不宣称逐一实测了全部 25 格式. 原生 OBJ orbit/封面, GLTF 动画/seek/当前帧封面, FBX/GLB 单窗口和真实切库返回均已通过. 最终 source/protected metadata 与最新 cover 保持, 主应用保持打开于 QA 库, viewer 已关闭, 原生搜索为空.

[最终统一健康](../../task-memory/task-billfish-library-animation-mode/final-health.json) 为 `Healthy=true`, manifest 222 项且 native DLL unchanged.

These are maintained internal interfaces, not new native vendor ABI. Owners may refine a field only after notifying the other consumers and updating this document. IDs are strings; times and transforms are finite numbers. Paths in a trusted request are absolute. Original files are read-only.

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

## Skeleton overlay contract

`显示骨架` 的 eligibility 来自实际 loaded `SkinnedMesh.skeleton.bones`, 每模型加载默认 `skeletonVisible:false`. Static Mesh 和无真实 skin 的 Bone hierarchy 不提供开关. FBX/GLB CharacterViewport 与 CoreViewport 的共享 UI 使用同一语义.

Core load/state 提供 serialized `hasSkeleton` / `skeletonVisible`; 仅同时有真实 `hasSkeleton` 和 concrete `skeletonOverlay` capability 才显示开关. `setSkeletonVisible(boolean) -> Promise<state>` 返回实际 acknowledged state, UI 等待结果同步 checkbox. 无 capability 的模型不发送此命令. Dispose 清除 facade flags; Core/Three objects 不跨 bridge.

Overlay 直接依据当前 bones 的 `matrixWorld` 更新, 沿用 resident renderer 的原 render loop, 不新增 animation loop. Helpers 作为 scene siblings lazy create, 不改变模型层级或 camera model bounds. Disable 从 scene 移除, re-enable 复用其 buffers, dispose 释放 helper geometries/materials, 原 model resources 由既有 viewport owner 管理.

骨架显示 renderer/host 实现已冻结. [Renderer implementation](../../task-memory/task-billfish-gameexport-skeleton/renderer-implementation.md), [Core host contract](../../task-memory/task-billfish-gameexport-skeleton/host-core-skeleton-implementation.json), [实际 Core production proof](../../task-memory/task-billfish-gameexport-skeleton/core-skeleton-poc/production/result.json). Focused 13 tests 与 Core facade/UI 15 tests PASS; 实际两骨骼 skinned GLTF 的 Core POC 和 production renderer smoke PASS. Production proof 使用 task-only synthetic source, 不等同于 native imported item 的整体验收. 本轮 GameExport 漏 FBX 原因与修复仍由 root 调查, 此处不提前给结论或宣称整体任务完成.

## Parser CLI

`billfish-animation-parser.exe <request.json> <response.json>`

The process writes a single JSON result to the caller-owned response path and diagnostics to stderr. Exit 0 is success; nonzero is an error, never a partly successful parse. Request contains `version:1`, `operation:"inspect"|"model"|"clip"`, `source`, and optional `profile`. `model` adds `output` for a local GLB cache. `clip` adds `clipIndex` and `output` for the BFCLIP1 Float64 binary cache. Current parserVersion is `billfish-ufbx-3`, while request/response version remains 1. The helper may write only explicitly named output paths and their owned texture subdirectory, never the source directory.

Success: `{version:1, parserVersion, ok:true, data:{...}}`. Error: `{version:1, parserVersion, ok:false, error:{code,message,details}}`.

### Inspect data

`format`, `hasMeshes`, `hasSkinnedMeshes`, `meshCount`, `vertexCount`, `nodes`, `clips`, `coordinates`, `warnings`.

- `nodes`: `{id,path,name,parentId,joint,rest:{translation:[x,y,z],rotation:[x,y,z,w],scale:[x,y,z]},basis:{...}}`. Preserve every ancestor needed for skin/animation, not only deforming bones. IDs refer to the source object and remain stable between inspect/model/clip of the same source. `basis` contains available pre/post/rotation-order/inheritance/bind information for compatibility; an animated initial rest TRS alone is not a bind mismatch.
- `clips`: `{index,name,startTime,duration,fps,tracks:[{nodeId,properties:["translation","rotation","scale","weights"]}],hasMorph}`. Use the take span. Source FPS may be null when it cannot be established. Metadata inspection does not bake every clip.
- `coordinates`: distinguish detected current basis/units, original provenance, canonical target and applied override. Canonical scene is right-handed, Y-up, meters. Actual FBX global/geometry transforms and pivot/pre/post rotations must be applied exactly once by the parser.
- `warnings`: typed `{code,message,details}` records. Missing optional textures can be a warning; absent/broken required geometry, invalid skin or unknown transform semantics must not be fabricated as valid output.

### Model data

`{path,metadata,nodeMap,warnings}`. GLB embeds stable source IDs in node extras using `billfishSourceId` and carries the correct node hierarchy, skin inverseBind matrices, Mesh transforms, geometry, material/texture data where available. `nodeMap` relates each source `id` to output node indices. The model cache need not include all baked animations.

### Clip data

Small JSON response data: `{path,format:"billfish-clip-f64-1",clipIndex,trackCount,sampleCount,duration,coordinates,warnings}`. Full numeric tracks are written once to the binary output, not duplicated in the response.

BFCLIP1 consists of 8-byte magic `BFCLIP1` plus trailing NUL, little-endian uint32 metadata byte length and data byte length, UTF-8 metadata padded to an 8-byte boundary, then complete little-endian Float64 samples. Metadata retains `{version:1,name,startTime,duration,fps,samplingRate,tracks}`; tracks use `{nodeId,property,times:{offset,count},values:{offset,count},interpolation}`, with offsets/counts in Float64 elements. Shared sample ranges are valid. Validate magic, exact total lengths, fatal UTF-8/metadata version and every range before constructing views.

Properties remain translation/rotation/scale/weights; quaternions remain xyzw, times start at zero, interpolation semantics and exact endpoints are retained. FBX baking keeps the prior sampling and source key times, without truncating/resampling to reduce payload. Real comparison across long/medium/short clips preserved all 18,105,230 doubles exactly. Three's existing final KeyframeTrack construction precision is unchanged. Parser version 3 and paired backend/UI codecs supersede the historical JSON clip contract.

Renderer may send `cachedKey` only for an actually resident row/cacheKey; every load still reaches backend freshness/member/profile checks. Main may return a validated cached acknowledgement, otherwise sends the full binary data. If the acknowledged renderer entry is missing, retry one full load. This is not a no-IPC bypass or zero-copy guarantee. Renderer LRU budget is 128 MiB, based on measured three-clip typed working set of 72.42 MB exceeding the prior 64 MiB.

## Backend service

CommonJS modules live in `automation/animation_browser/backend/`. Export `createAnimationBrowserService(options)` from `service.cjs`. Options supply `cacheRoot`, `profileRoot`, `parserPath`, the mandatory trusted callback `getLibraryCandidates({libraryRoot,signal})`, and callbacks `onEvent(sessionId,event)` and `onVisibilityChanged(libraryRoot,hiddenPaths)`. `hiddenPaths` is a complete replacement of fresh fully-compatible pure-animation absolute paths, not a delta. Settings, show-all mode, per-item overrides, conflicts, parser/schema/coordinate policy and current source/mesh fingerprints participate in the backend predicate. Invalidation removes affected paths before deferred reconciliation. The native owner maps them to real IDs through the current read-only catalog.

The service exposes asynchronous `openSession({sessionId,source,itemId,libraryRoot,sourceFingerprint})`, `call(sessionId,method,args)`, `closeSession(sessionId)` and `shutdown()`. Main supplies immutable source/item/library identity. Browser renderer cannot choose another material or arbitrary cache paths. 当前库 catalog 是唯一候选范围. 选中 base 必须是当前库真实 item, 与 main 绑定的 item ID/path 一致; 不允许外部目录 fallback 或 renderer 提交候选列表.

- Initial session state includes `model:{path,format,nodeMap,metadata}`, `rows`, `scan:{state,completed,total,errorCount}`, `profile`, `libraryContext:{libraryRoot,libraryName,relativeFolder}`, `diagnostics` and persisted list settings.
- Each row represents one clip, with stable `id`, `sourceItemId`, `origin:"embedded"|"library-animation"`, `source`, `relativePath`, `folder`, `name`, `clipIndex`, `duration`, `fps`, `format`, `compatibility:"full"|"partial"|"incompatible"`, `report`, `hidden`, and `association`.
- `clip.load({rowId})` returns normalized clip JSON data, a verified mapping from source node IDs to base node IDs, source fingerprint and `sourceItemId`. Catalog membership is checked before and after decode. Never parse/upload the Mesh again for a list click.
- `scan.refresh`, `scan.cancel`, `profile.update`, `association.update`, `visibility.update`, `diagnostics.get`, `cache.command` and `profile.resolveConflict` implement the documented controls. Actions report progress and real errors.
- `cache.command` distinguishes refresh-current, reparse-current, regenerate-thumbnail, verify-folder and rebuild-derived-cache. Rebuild retains manual profile/association revisions.
- Events are `state`, `rows`, `progress`, `warning`, `profileConflict` and `visibility` with complete typed payloads. An old session cannot update a new window/library.
- Main handles opening source folders, clipboard copy and native cover publication; the backend has no unrestricted UI/shell operations.

### 模型目录子树候选与恢复

默认只搜索真实模型文件所在目录及其全部子目录. `candidateScope.set` 只接受 `{mode:"current"}` 或 `{mode:"parent"}`; parent 恰好扩大到模型目录的上一级及其全部子目录, 不继续向上, 不越过当前库根. 根由可信 source path 派生, renderer 无权传入任意目录. 例如 `AttestantDog/FBX/SK_Dog.fbx` 默认搜索 `AttestantDog/FBX`, 手动选中`搜索上一级目录`后搜索 `AttestantDog`, 可包含平级 `Anim` 或其他名称的文件夹. 不要求目录名固定为 FBX/Anim, 不按模型与动画的 basename 或 SK_/AS_ 前后缀筛掉候选; 仍由实际骨架兼容性确认动作.

每个新模型 session 默认 current. 同一已打开 viewer 的复用保留其本窗口选择; 不存储 global 范围偏好. 切换范围立即取消旧扫描并清空旧 rows/播放绑定, 再在新范围内枚举和解析. 自身 embedded clips 始终保留, non-skinned Mesh 仅列自身 clips. 目录或父目录没有动作时显示空列表, 不回退到全库.

可信 Python helper request 为 `mode:animation_catalog`, `operation:animation.catalog_snapshot`, `arguments:{scope_root:"<trusted absolute directory>"}`, library root 为进程绑定的绝对路径. `Catalog.animation_snapshot(scope_root)` 在单个 `mode=ro` SQLite transaction 中解析 anchor 的祖先, 再使用 recursive CTE 仅查询该子树的非回收 FBX/GLB. 在 hydration/exists 查询前已经排除外部和兄弟目录; native hidden 成员仍参与, 磁盘未入库邻居不参与.

```json
{"schemaVersion":2,"libraryRoot":"<bound absolute root>","scopeRoot":"<model directory or its one parent>","scopeMode":"model-directory-recursive","complete":true,"revision":"<semantic SHA>","items":[{"id":"61","name":"AS_Run.fbx","folderId":"3","path":"<owned absolute path in scope>","relativePath":"<library-relative path>","exists":true,"isLink":false,"pathError":null}]}
```

IDs 保持 decimal strings. `complete` 表示指定目录子树完整, scopeRoot 与 scopeMode 都参与 revision. Linked/无效路径成员带 null path 和诊断, 不作为可加载源. Main reader 验证 library/scope/schema/complete/revision/唯一 IDs/owned extension paths. 2026-10-20 安装版改用绑定单一 libraryRoot 的常驻 bundled Python worker, 逐行发送 requestId/scopeRoot; 每请求仍创建 fresh mode=ro/query_only SQLite connection/transaction, 无结果或 connection 缓存. 请求保持 deadline/output bounds, abort 按身份隔离迟到回复, malformed/timeout worker 退役且不自动重放, shutdown 关闭 child. 安装身份见本轮 task_state 引用的 install-receipt.json. 缺 provider 报 `CATALOG_REQUIRED`, invalid snapshot 报 `INVALID_CATALOG`; 无未限定 scope 的请求, 无全库或磁盘递归 fallback.

Backend 为 session 保留它自己的 scoped catalog, 在 scan、warm rows、clip.load 与 native visibility 重新验证相同目录边界. Row ID 使用 `billfish-model-subtree-1`, native item ID 与 clip index, `sourceItemId` 保持原生身份. `libraryContext` 返回 `candidateScope`, `scopeRoot`, `relativeScope`, `canSearchParent`; rows/state 更新携带真实范围及该范围的 scan counts. 父目录选项在模型已处于库根时禁用.

Session derivation 为 `sessionSchema:5`, `candidatePolicy:billfish-model-subtree-1`; session cache key 与记录包含具体 scopeRoot 和 current/parent. 旧 whole-library/external 关联不恢复, scope 外 rows 在 stat/hash/解析前排除. Parent 缓存可作为本次明确选择的衍生数据, 但不会在无 viewer 或新 current session 中恢复兄弟目录隐藏状态. Metadata/model/clip 内容缓存仍按有效 fingerprint 复用. Watch 只保留有效关联记录和当前 session 的范围、catalog 成员父目录及库 `.bf`, 非递归; 150ms debounce 与现有 30s reconciliation 都重读 scoped snapshot.

旧 `scope.set` 和 `profile.scope` patch 继续拒绝; `candidateScope.set` 不接受 path 或额外字段. 有效 profile 剔除旧 scope, 保留 immutable history. Maya/Max/custom, units, joint basis, facing, 范围内的 manual associations/file overrides 不清除. 旧全库 snapshot 和 `{}` helper arguments 不设兼容 fallback.

## Coordinate and profile contract

Persist separate detected scene basis, chosen interpretation override, user display orientation, units and per-joint basis/mapping records. Default unknown/manual preset is Maya. `auto` respects reliable file metadata. A preset never erases actual joint basis or automatically retargets a different character. Shared user revisions are immutable records with parents; diverging heads are a conflict, not last-write-wins. Discarding derived cache preserves all user revisions.

## Renderer contract

The maintained page receives `window.billfishAnimationBrowser` from a dedicated preload. It exposes `initialize()`, `call(method,args)`, `onEvent(callback)`, `reveal(rowId)`, `copyReport(rowId)` and `setCover({png,pose})`; main validates sender ownership and payloads. No public automation endpoint is added just for tests.

Use one bundled Three version for GLTFLoader, Object3D, AnimationClip and AnimationMixer. Resolve `billfishSourceId` to actual loaded objects; imported names are display data, not unique binding keys. Keep one base scene and mixer. During replacement loading, pause and retain the displayed pose. Decode and validate the complete replacement before synchronously stopping/restoring the previous action and committing the new action. Failed or superseded replacement cannot reset the displayed pose or stop a newer action. Use a bounded clip LRU and dispose renderer/geometry/material/texture resources on close. Keep valid geometry visible when optional resources are missing. All success messages require actual parsing/rendering/native completion, not only queued requests.

### 统一侧栏与单窗口

动画列表与 transport 在普通模型和动画模型中常驻. 无 clips 时显示空列表并禁用播放/seek/frame controls, 模型, 相机和封面能力仍可用; 有自身 transform clips 时正常列出. 晚到 rows 可启用播放并从第一条有效动作开始. UI 不再提供目录 picker.

Main 仅保留一个可见 viewer slot. 同 item 仅在 ID/path/SHA/bytes/mtime 未变且原打开已真实完成时复用其 HWND/completed result; 其他 item 或 source 变化先关闭旧 owner/window/session, 再新建. `viewerGeneration` 排除旧异步结果. thumbnail owners 不占此 slot. Core 格式已使用同一维护 UI 和 viewer slot; 原生 OBJ/GLTF 与 FBX/GLB 单窗口 E2E, cover 和 library switch/return 已通过. 每次打开列表默认只展示 full compatibility; 手动选择 all/partial 仍可使用.

## Native integration

Register the built-in FBX/GLB browser independently of a user-installed Eagle package while reusing existing process/session ownership and cover workflow. It is not an entry in the right-click Plugins menu. Library/context changes stop scoped work. Native local visibility accepts only fresh, fully compatible pure-animation associations plus user visibility settings. It must preserve the native model/proxy and clear hidden selections; unknown/stale records remain visible. Persisted catalog IDs are resolved from actual paths, never synthesized.

The reserved descriptor ID is `billfish.animation-browser`. Native registration reads the installed manifest, enables it independently of user plugin state, rejects user install/update/disable of that ID, and passes the complete validated descriptor in `BILLFISH_BUILTIN_DESCRIPTOR` with `BILLFISH_ANIMATION_BROWSER_BUILTIN=1`. Main verifies the canonical built-in directory, ID and engine. FBX/GLB 使用维护的 Three/ufbx 路线; 其余原格式通过同一维护 UI 内的 Core adapter. 单个 builtin `billfish.animation-browser` 名为 `3D 预览`, 保留 25 viewer / 23 thumbnail extensions, HDR/EXR 仅 viewer. 旧普通 3D 插件已禁用并归档移出扫描目录, 不再参与路由. 这些格式保留声明由注册验证支持, 不等同于所有格式均已逐个执行验收.

### Private visibility snapshot

Main sends `{type:'animation_visibility',request_id,hidden_paths:[absolutePath,...]}` only from the trusted backend callback. Native supplies the immutable process-bound library root. Renderer item IDs/library paths are not an admission route.

`animation_visibility_result` echoes `request_id` and contains either `data:{applied:true,hidden_item_ids:[id,...],unresolved_paths:[path,...],state:'filtered'|'visible'}` or `error:{code,message}`. Unknown/external/ambiguous paths do not acquire a native ID. Success follows actual native predicate installation/refresh, not a queued intention. Old request generations or changed owner/proxy/source bindings cannot apply an earlier catalog result. Host stop/crash, library change and shutdown restore owned native filtering. A backend that can no longer verify its retained associations must publish an unhidden replacement.

The maintained predicate is attached to the verified BFDataModelFilter instance, preserves original native behavior for unexcluded rows and restores all valid visible selections through source persistent indexes. The rejected QListView row-hide candidate is not the production contract. No native SQL flags or source records are changed.

### Private native covers

`preview_cover` carries a main-bound native `item:{id,path}`, original base `source:{sha256,bytes,mtime_ns}`, PNG `dest`, and `pose`. Fingerprint byte count and nanosecond time remain decimal strings across Qt JSON.

- Base: `pose:{kind:'base',time:0}`, no `animation_source`.
- External clip: `pose:{kind:'clip',rowId,clip_index,clip_name,time,fps}`, with source FPS positive or null when unknown; UI stepping FPS is separate. Main supplies `animation_source:{path,fingerprint:{sha256,bytes,mtime_ns},clip_index,rowId}` from its last successful bound clip load. Renderer cannot substitute that source identity.

Native rechecks the base fingerprint and external binding at prepare, publish and verify. `preview_cover_result.data` is the actual verified native result, with `completion:'native_custom_cover_verified'`, matching item, `source_unchanged:true`, `metadata_unchanged:true`, `cover_path`, `image`, the same pose and external animation binding when present. Error is nested and is not a rollback claim.

The across-read stability check uses size, exact modification time and file identity, together with the content hash. Access time is excluded because reading a source to hash it can update that value. A read-induced atime change must not reject an unchanged external animation; actual content/mtime/identity changes still fail verification.

Explicit reference-thumbnail rebuild sends `{type:'animation_thumbnail',request_id,item:{id,path},source}` and receives `animation_thumbnail_result`. It rerenders the base/reference pose and may replace an existing cover. Its verified data includes `pose:{kind:'base',time:0}`. Ordinary automatic thumbnail generation continues to preserve an existing custom cover.

The native custom-cover QSize repair is an internal frozen-ABI boundary, not a renderer control: only verified FBX/GLB custom-cover paint requests with missing source image dimensions and `(positiveWidth,INT_MIN)` are normalized locally to square size. All native key-builder arguments, including the exact int32 flag, otherwise pass unchanged. It does not alter stored dimensions or cover content.

## 加载反馈与动画切换合同, 2026-10-20

Solid/Material 使用两个按钮, aria-pressed 只表示实际 renderMode, aria-busy/requested 表示已请求但未完成的材质工作. Solid 就绪可交互, 完整 Material 尚未成功时不得发布自动缩略图或手动封面. 材质切换保留原尺寸图像/材质和真实错误边界.

Native viewer early intent 在正式 preview_prepare 前发出, 仅携带 item ID, extension, bound libraryRoot 和 background. Same-window shell 先显示准备/等待反馈, 不接收 source path 或提前 hash/解析/注册源资产. 正式 prepare 仍验证 item/extension/library/source fingerprint; early intent 不产生 rendered success. Terminal failure 停 spinner 并使迟到 initialize/ready 失败, 后续 retry 使用新 context 和 fresh source hash. 新 intent 仍遵守 latest-wins; 同 item source 变化关闭旧 owner 时不得 abort 新 context. Third-party viewer, thumbnail 和 cover 不成为 early shell.

本节已随9 runtime+7 tests安装, manifest230. Candidate source/real Electron 专项与installed Qt双击/错误反馈证据分别记录, 不能由假窗口测试或early shell显示推出模型首帧时延. 证据及未解决的 Waterfall 跳尾问题见 [本轮 task_state](../../task-memory/task-billfish-viewer-feedback-switching/task_state.md).

[最终运行收据](../../task-memory/task-billfish-viewer-feedback-switching/final-runtime-receipt.json) 核对230项manifest和最终Healthy=true. 原生早期shell与正式prepare/实际渲染仍是分开的状态边界. 主素材Waterfall跳尾未复现且未关闭, 不新增推测性scroll恢复机制.

## 启动库 context 与素材输入路由, 2026-10-20

NativeAdapter 的 lifecycle 连接随真实 manager 身份重连; 每次已有 observation 发现 authenticated owner ready 且 root 非空, 都应 reconciliation 到 activeRoot. 先保存 root 再发 activeLibraryChanged, 保证同步 currentInfo/info 重入不重复发布. 不因 manager 指针没变跳过 ready-root 更新, 不在首次观察 already-ready manager 时只静默保存. transient notready 不生成人工 close, 显式 libraryClosed 继续清空 context.

FormatPreviewBridge 构造时使用 application event filter 观察输入, 不依赖 750 ms discover 已绑定 view/viewport. 仅确切 BFBaseItemView 或其 viewport 进入原 native currentMaterialView/item/module/extension 认证; 不向祖先泛化 rename editor 等子控件. 左键 viewport 双击和原 view 键盘触发条件保持. Maintenance events 限定缓存 view/viewport, shutdown 移除 app filter. preview protocol 未 ready 或格式不匹配时不谎报可预览, 不无反馈吞掉原生 fallback.

库 root 同步, builtin process 创建, preview protocol ready, native type registration 和模型 viewer 显示分别验证. `host.info`, `ui.*`, `items.selected` 经过 currentInfo, 对原启动缺陷有补救副作用; 无 API 被动冷启动与直接 `plugins.list` 用于隔离这条补救路径. [startup task_state](../../task-memory/task-billfish-viewer-startup-routing/task_state.md) 已记录安装态无 API 自主启动, first-observed-usable FBX 输入, GLB 和普通 PNG 回归. 首击脚本本身连接 UI 会经过 currentInfo, 因而不能单独证明自主启动, 也不宣称任意最早硬件输入或每个 starting 握手间隙均被覆盖. [最终运行收据](../../task-memory/task-billfish-viewer-startup-routing/final-runtime-receipt.json) 为 DLL 002C700D...43B95 / 230 manifest / Healthy=true.
