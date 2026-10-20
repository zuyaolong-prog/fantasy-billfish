# Billfish character animation browser implementation plan

本文保留维护过程中的历史计划和本机 QA 记录. `task-memory` 链接指向未随源码发布的本地验收材料, 公开仓库中不可访问. 当前功能以 [README](../../README.md#3d-models-and-animation) 和 [协议的最新章节](protocol.md) 为准; 历史轮次中的 pending 或完成状态仅描述当时的验证范围.

## 长动作, 时间轴与侧栏导航新请求, 2026-10-19

本轮实现, 安装与验收完成, 下方已完成轮次记录保持原时点含义. 用户新增五项: 优化长动作切换3-4s停顿; 在原时间下方增加所选速度对应的实际 current/total; 拖动时间轴暂停并保持暂停, 直到点击播放或Space; 左侧栏始终提示当前目录; 侧栏填满/overflow时从中间子目录导航避免整列跳动. 最终性能after, transport/sidebar验收, 组合安装及Healthy均已完成, 文档与生产source已freeze.

实际时间显示为 source current/speed 与 source total/speed, 保留原始时间和帧语义. 2x显示一半, 0.5x显示两倍. 拖动开始即pause, seek后及释放/取消仍保持paused, 显式播放或时间轴焦点下Space才继续; 时间轴保持焦点不应阻止thumb跟随. 不劫持搜索/数值输入框中的Space.

侧栏当前项提示使用真实model index selection/currentIndex, 避免复用delegate的stale selected状态. Overflow导航候选在expand后映射真实flattened row并用 `ListView.Contain`, 已可见目标不移动, 屏外目标最小揭示; 不是重排目录顺序, 不改普通scrollbar handlers. Sidebar final normal-cache实机验收, 最后组合安装及health均通过.

[本轮 task_state](../../task-memory/task-billfish-long-clips-transport-sidebar/task_state.md), [长动作before](../../task-memory/task-billfish-long-clips-transport-sidebar/long-before-latest.json), [transport candidate](../../task-memory/task-billfish-long-clips-transport-sidebar/transport-candidate.json), [sidebar实现](../../task-memory/task-billfish-long-clips-transport-sidebar/sidebar-evidence/implementation.md) 记录当前进展. 既有短动作性能结果不能替代此次长动作before/after.

本轮分别保留frozen runtime-before长clip测量, transport actualDOM before/after, 与sidebar资源candidate/独立review. Parser或payload/cache机制只有在当前基线支持下才实施; 不重采样/截断tracks或弱化成员/source检查. 部署与用户现场恢复由root统一执行, sidebar final已实机验证, final perfafter/组合安装/health均完成.

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

当前 ownership: worker_eagle_3d_port的performance和worker_animation_viewer的shared accessor/full-skin候选已冻结, final after/GPU/math/<=4及principal review证据如下; parser2已安装tools且native33726打开通过. 最后实际外部动作检查已PASS, 当前交付状态已freeze. 不把仍在进行的候选改写为已交付.

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

## 骨架显示已冻结实现, 2026-10-19

Renderer 新增 `ui/skeleton-overlay.mjs`, 并接入 `viewport.mjs`, `app.mjs`, `index.html`, `style.css`; Core host 接线位于 `automation/plugin_runtime/core-viewport.cjs` 和 `animation-browser-runtime.cjs`, facade 使用窄异步 ACK contract.

实际 SkinnedMesh skeleton 才显示 `显示骨架`, 每模型默认 off, static/Bone-only 隐藏. FBX/GLB Three 和 Core skinned 模型统一使用当前 pose, 随 play/seek 更新. Helpers lazy create, scene sibling, disable 移除, re-enable 复用, dispose 释放 own geometries/materials, 保留原模型与源文件. 发布时包含新 overlay module, 不仅更新 checkbox 页面.

骨架显示 renderer/host 实现已冻结. [Renderer implementation](../../task-memory/task-billfish-gameexport-skeleton/renderer-implementation.md), [Core host contract](../../task-memory/task-billfish-gameexport-skeleton/host-core-skeleton-implementation.json), [实际 Core production proof](../../task-memory/task-billfish-gameexport-skeleton/core-skeleton-poc/production/result.json). Focused 13 tests 与 Core facade/UI 15 tests PASS; 实际两骨骼 skinned GLTF 的 Core POC 和 production renderer smoke PASS. Production proof 使用 task-only synthetic source, 不等同于 native imported item 的整体验收. 本轮 GameExport 漏 FBX 原因与修复仍由 root 调查, 此处不提前给结论或宣称整体任务完成.

## 当前实施方案与阶段状态, 2026-10-19

本节优先于下方旧阶段计划. 本轮保留插件弹窗交互, 将普通 3D 与动画预览收敛到同一插件和单个可见窗口; 动画侧栏及 transport 常驻, 没有可用动画时显示空列表并禁用播放相关 controls. 原 Core 格式已接入同一维护插件, 旧普通 3D 插件已禁用并归档移出扫描目录. 注册验证为单个 enabled `billfish.animation-browser`, 名称 `3D 预览`, 保留原全部 25 viewer / 23 thumbnail extensions, HDR/EXR 仅 viewer. OBJ/GLTF production smoke run 4, 3dm WASM targeted run 5 和 reference pose run 2 PASS; 不宣称逐一实测了全部 25 格式. 原生 OBJ orbit/封面, GLTF 动画/seek/当前帧封面, FBX/GLB 单窗口和真实切库返回均已通过. 最终 source/protected metadata 与最新 cover 保持, 主应用保持打开于 QA 库, viewer 已关闭, 原生搜索为空.

1. 已完成: `automation/billfish_api/catalog.py` 和 `billfish_host_helper.py` 提供只读 `animation.catalog_snapshot`; 在一致事务内读取全库非回收 FBX/GLB, 包含隐藏成员, linked/无效路径返回诊断. 无磁盘递归发现, 不改 native schema.
2. 已完成: `automation/plugin_runtime/animation-browser-catalog.cjs` 把固定当前库 root 绑定到 bundled Python helper; 严格 schema/ID/path 校验, 30s deadline, 64MiB output limit, abort/shutdown 终止进程. backend 必须注入可信 `getLibraryCandidates`, 不提供外部目录 fallback.
3. 已完成: `automation/animation_browser/backend/service.cjs` 仅使用当前 catalog. 非 skinned Mesh 仅列自身 embedded clips, skinned Mesh 关联当前库候选; row 身份包含 `sourceItemId`, `origin`, policy 和 clip index. scan/load/restore/visibility 重验成员身份. watchers 仅绑定 `.bf` 数据库及候选父目录, 不递归; 150ms debounce 和现有 30s reconciliation 保留.
4. 已完成: session schema 4 / `billfish-catalog-1` 失效旧 schema 3 衍生关联. 旧 external `profile.scope` 不进入有效配置, `scope.set` 及 scope patch 拒绝. 保留 Maya/Max/custom, joint basis, facing, manual path associations, 原 relative/hash overrides 和 immutable revision history, 新 file override 使用 native item ID. 不删除 profiles 来完成迁移.
5. 已完成: 同 item 在真实完成且 fingerprint 未变时复用窗口, 不同 item/source 变化先关闭旧 owner/window/session; stale generation 不得覆盖新 viewer. thumbnail 不占 viewer slot. UI 移除目录选择入口, 使用当前库上下文, 统一空列表和晚到 rows 的播放行为.
6. 已完成: Core adapter 与必要 resources/licenses 收入同一维护插件, 原 19 个资源文件按 provenance 保留. 旧插件先通过 plugins.enable false 停用, 确认 Billfish 及插件进程均已退出后执行 archive-old-plugin.ps1 移入 removed-plugins. sourceAbsent/archivePresent 均 true. 唯一根入口重启后的注册验证 PASS, 保留全部 25 viewer / 23 thumbnail extensions, HDR/EXR 仅 viewer. Production OBJ/GLTF run 4, WASM 3dm run 5, reference pose run 2 及 principal 增量审查 PASS. 格式声明与代表性执行验证分开记录.
7. 已完成: 原生 OBJ orbit/native cover, GLTF 4 clips/seek/current-pose cover, FBX default full/cross-folder playback/same-window reuse, static GLB empty list, actual library switch/return 和偏好保留. Source/protected metadata/latest cover verification ok=true. 主应用保持打开 QA 库, 搜索为空, viewer 已关闭; 不归因为 root 正常关闭, 不宣称主应用已退出. [最终统一健康](../../task-memory/task-billfish-library-animation-mode/final-health.json) 为 `Healthy=true`, manifest 222 项且 native DLL unchanged.

已知验证: backend 6 files / 34 tests PASS; Python catalog 15 tests PASS; focused host catalog 3 + scope 1 + single-viewer 1 tests PASS; UI 最新 7 tests PASS, 包括每次打开默认 full, 手动 all/partial 仍可用. 原生双击 cross-folder FBX 自动播放, 同 item 窗口复用和 static GLB 空列表已实测. 当前 runtime 已刷新为 222 fingerprints, health-before-final-launch Healthy=true. 从唯一 Billfish.exe 重启后 core 46636, unified-registration-verification.json PASS. 原生最终 E2E 与保护数据核对已完成, 主应用按当前现场保持打开于 QA 库, viewer 已关闭. 证据见 requirements-and-validation.md 本轮最终原生验收.

本轮恢复材料在 `task-memory/task-billfish-library-animation-mode/runtime-before/`, 包含本轮前 automation JS/Python 和 portable manifest; 旧任务 `runtime-before/` 只含 native rollback 材料. 维护恢复须在正常退出后核对实际文件及对应 fingerprints, 不把复制旧 manifest 当作源码恢复. 保留用户 profiles 和 source originals.

## 旧轮次计划与已验证历史

以下保留此前外部目录浏览器的实施记录. 其中 chooser/父目录递归/多窗口和独立旧 Core route 由上方当前方案替代; 旧 F:/TempFBX 125 动作仅作为历史 benchmark.

This plan adapts the supplied proposal to the audited installed runtime. It preserves the full requested workflow, source-file safety, presentation filtering, persistence, diagnostics and performance verification. Phase completion requires evidence, not only green unit tests.

## Architecture decisions

1. Repair observed preview failures first and retain debugger evidence for unexplained host exits.
2. Add a maintained, built-in FBX/GLB browser using the existing Electron/native lifetime and publication infrastructure. Bundle one pinned Three.js version for renderer, GLTFLoader, mixer and controls. Keep the existing broad-format Core path for other formats.
3. Use a small isolated ufbx-based helper for FBX inspection, base-Mesh conversion and selected-clip baking. It must evaluate real FBX transforms, not reinterpret Euler/pre-rotation curves manually. No second C++ renderer or Autodesk runtime is needed.
4. Use a single normalized scene/clip contract for FBX and GLB. Normalize scene basis and units once; preserve full hierarchy, skin inverse bind matrices, non-joint ancestors and stable source-node identities. Never silently drop non-root translation or scale.
5. Inspect only structure and clip summaries during discovery. Convert the base Mesh once into local cache. Load/bake a clip only when requested, keep the Mesh resident and release old clip/action resources under a bounded recent-use cache.
6. Persist local file/index/normalized-output caches separately from versioned user association and coordinate profiles. Shared profile publication must preserve concurrent revisions instead of overwriting a single shared database.
7. Keep native material model/proxy/selection ownership. The public QListView row-hide route failed actual Flexible and Waterfall gates. Use the verified BFDataModelFilter instance predicate boundary, preserve the complete primary vtable/RTTI except its row predicate, and refresh through the native counter-reset slot. Source/owner changes invalidate prior mapped IDs. Uncertain, invalid or stale associations remain visible. Do not mutate is_hide, recycle state, tags or notes to simulate filtering.
8. Default scan root is the actual Mesh parent, recursively. A visible "选择动画目录" control supports the common copy-import case and persists that explicit scope. Multiple Mesh associations remain distinct and manual overrides are available.

## Source ownership and modules

- `_animation-browser-parser/`: ufbx helper source, pinned third-party code, fixtures and native tests. Deploy only its built executable and provenance under `tools/animation-parser/`.
- `automation/animation_browser/backend/`: incremental discovery, GLB metadata/clip reading, skeleton matching, parser jobs, local cache, profiles and diagnostics.
- `automation/animation_browser/ui/` and `vendor/`: maintained viewport, clip list, playback, filters, diagnostics, coordinate settings and bundled renderer dependencies.
- Existing `automation/plugin_runtime/` files: narrow built-in dispatch/lifetime/IPC integration. Keep source fingerprint and verified cover publication semantics.
- `_video-trim-patch/src/automation/`: `plugin_manager` owns built-in registration/private IPC, `animation_visibility` owns the guarded native predicate, `native_cover_geometry` owns the precise invalid QSize repair, and `format_preview_bridge` owns read-only mapping, generations/lifetime and the existing thumbnail publication queue.
- `docs/animation-browser/`: audit, architecture, protocol, operations/rollback, limits and evidence-based validation records.

## Phases and gates

### 0. Audit and contracts

Produce architecture-audit.md and this plan before changing native index/model behavior. Derive completion gates from requirements-and-validation.md. Record source and runtime baselines and the unchanged original asset hashes. No automatic Git commits are part of this repository workflow.

### 1. Parser, normalization and association

Implement metadata inspection, actual Mesh/pure-animation classification, multi-clip enumeration, hierarchy-aware matching and diagnostic reports. Resolve name collisions explicitly. Use the real 106-bone character and at least two external animation files to verify normalized joint matrices and visible poses before expanding UI. Add meaningful incompatible, missing-track, large-skeleton, multi-root, GLB multi-clip/morph and malformed-file fixtures.

### 2. Resident-Mesh viewer

Provide the integrated viewport, complete camera controls/reset, animation list with names/folders/duration/source FPS/status, click/previous/next, search, sort, grouping and filters. Include pause/replay/loop/once/timeline/speed/frame navigation. Restore base defaults between actions, stop and uncache previous actions, and support root motion without automatic retarget. Show unsupported semantics and missing texture diagnostics without falsely reporting success or destroying otherwise valid geometry.

### 3. Native UI and covers

Route native double-click to the built-in browser. Add hide-associated global control, show-all mode and per-animation visibility override. Validate all four native layouts, compact geometry, pointer/keyboard/range/select-all behavior, search/sort/reset/library changes and disabling/restoration. Verify actual Mesh imagery in the native grid and current-frame cover publication. Preserve native metadata and original files.

The native candidate now uses complete path snapshots, read-only path-to-ID mapping and BFDataModelFilter source-role exclusion. A removed path is unhidden before asynchronous remapping; fresh helper results must match the captured owner/proxy/source/generation. Save/restore all shared-proxy selections through source persistent indexes. Stop/crash, library change and shutdown clear owned exclusions. Repeated same-ID updates must not reset the model unnecessarily.

Production layout switching exposed an integration gap after the diagnostic predicate pass: native arrangement handling rebinds the same source address, so final proxy/source equality does not prove the hook remains attached. The component now notifies actual binding invalidation; the bridge cancels old mapping generations, clears old mapped IDs and schedules fresh path mapping outside the native signal. The corrected installed production4 passed all four layouts and Ctrl+A exclusion without diagnostic probes.

The cover repair normalizes only the verified paint builder's invalid non-image custom-cover request, from `(width,INT_MIN)` to `(width,width)`. Keep the exact int32 builder flag ABI. Do not alter source dimensions or database type/thumbnail fields. Current-frame covers verify both the base fingerprint and the main-bound external animation fingerprint at prepare, publish and verify; explicit reference-thumbnail regeneration can replace an existing cover, while ordinary automatic generation preserves it.

Installed production status: `preview.dll` is 1,603,072 bytes, SHA-256 `622F058F8FD4B76E5B45FF65EC2DB9A321DB178FDA4E5EA1A402C4F8F262551F`. It includes the empty-library startup guard and binding-invalidation correction. The plugin suite passed 28 tests and Python preview/helper suite passed 16. Production4 passed no-viewer restart filtering, all four layouts, global show/re-hide, warm FBX 125-row/Maya restoration, GLB first-open row delivery, current-pose cover and reference-thumbnail regeneration. Actual cover checks confirmed source/metadata preservation and changed cover bytes. Earlier diagnostic selection/range/source replacement and ordinary JPG/MP4 parity remain diagnostic evidence, separate from these production passes.

Production5 exposed repeated identical complete visibility snapshots racing the explicit UI operation. The host now reuses the real in-flight native ACK for an identical canonical path set, retaining failure for different sets and actual native errors. Three regression cases pass. The corrected host PID56776 passed actual per-item re-hide/show with three viewers: native rows changed from 0 to 1, actual selection identified item 56, and the UI had no red error. Automatic hiding was restored afterward. See `production5-single-{rehide-success,show-success}.png` and `native/production5-single-{rehide,show-retry}.json`.

### 4. Persistence and incremental work

Persist file identity/stat/hash, parser/schema versions, skeleton/track summaries, compatibility, associations, profiles and cached output locations. Warm opening immediately reads cached metadata and uses bounded background reconciliation. Reparse only changed entries. Add cancellable progress, watcher recovery, refresh-current, reparse-current, folder verification and full-cache rebuild while preserving manual profiles. Persist detected/selected Maya/Max/custom basis, units and per-joint basis information without double conversion. Detect shared profile conflicts and preserve both edits.

Actual production5 incremental discovery/playback and derived-cache rebuild have passed; the cache verification records three profile files before/after with `profilesIdentical:true` and 125 restored rows. Library switching stopped the previous host and all old viewers, created no borrowed profiles/associations in the second library, and returning restored three original profiles and the viewer/settings state. See `host/production5-increment.snapshot.json`, `host/production5-cache-profile-verification.json`, `host/production5-library-lifecycle-verification.json`, `production5-library-return-viewer.png` and `production5-return-settings.png` under the task evidence directory.

### 5. Final verification

Exercise the actual Billfish workflow: native Mesh card -> double-click -> automatically restored list -> multiple external actions -> close/restart -> warm cache -> add an animation -> incremental refresh -> playback. Toggle visibility back on and verify original indexed cards/files/metadata are intact. Measure cold build, warm hit and one-file update separately; measure repeated switching and bounded memory. Use 125 provided actions plus explicit hundreds-of-clips and topology/format fixtures, clearly distinguishing real assets from generated stress inputs.

Final production restoration is complete: all three viewers were normally closed, the remaining native window was normally exited, and the after-exit snapshot records no related processes/windows. Items 55/56 retain source/protected metadata with native `is_hide=0`; automatic filtering was restored before exit and the final hidden query has no row/selection. Unified health is true with the QA library retained as the recent library. Evidence: `host/production5-all-viewers-closed.snapshot.json`, `host/production5-after-normal-exit.snapshot.json`, `host/production5-final-data-verification.json`, `native/production5-final-hidden.json`, `native/production5-final-health.json`.

## Delivery and operational boundaries

- Ship working built-in behavior, deployment fingerprints, migration/rollback instructions, cache/profile maintenance controls, compatibility limits and exact evidence paths.
- Root's pre-install backup is `task-memory/task-billfish-animation-browser/runtime-before/`, containing the previous `preview.dll`, `prepared-manifest.json` and `portable-runtime.json`. This is native rollback material rather than a complete JS/Python source backup. After normal application exit, restore the selected native DLL, reconcile the actual runtime files and matching fingerprints/baseline, then recheck unified health. Merely copying old baseline JSON does not revert maintained files or manual profile revisions. No dedicated one-command full browser rollback is provided. The builtin has no disable switch: `PluginManager::setEnabled()` returns `builtin_read_only`. A viewer window closes its own session; normal Billfish exit stops the host. Visibility settings can show associated files without uninstalling the browser.
- New cache schemas may invalidate derived data safely; never discard user choices during migration or cache cleanup.
- Originals remain untouched. No merging/exporting over the user's FBX/GLB, no native record deletion, no cross-character automatic retargeting and no shared runtime SQLite writes.
- Historical deployed parser/renderer boundary: Supported builtin formats are FBX and GLB. Typed unsupported cases include FBX nonzero DQ, more than four influences, multiple deformers and FBX morph geometry; GLB external buffers, unavailable Draco decoding and secondary skin influence sets. Other formats retain their existing Core path.
- The original unexplained native exit has no established root cause: the debugger captured no fatal exception/dump. Controlled normal and malformed-file open/close/reopen behavior passed, including a corrupt GLB error followed by a successful valid open. Do not label these checks a proven repair of the original crash.
- The goal remains active until each required gate has direct current-state evidence. A partial implementation or standalone demo is not completion.
