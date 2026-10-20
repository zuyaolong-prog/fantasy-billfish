# 3D 格式扩展 1.9.0, Billfish 维护包

此包保留本机 Eagle 3D 格式扩展的原始 49 个文件及其 viewer/thumbnail 加载器, 供 Billfish 的格式扩展宿主使用. 它同时包含原插件的设置入口, 3D 预览使用独立 viewer 窗口, 缩略图使用独立隐藏 WebGL renderer.

维护改动仅有:

- `viewer.html`: 将原来的 `/ifc/` WASM 绝对路径改为随包 `lib/ifc/` 相对资源路径.
- `manifest.json`: 增加 `billfish.previewEngine="eagle-3d-core"`, 让 Billfish 等待原 Core 的模型加载 Promise 和真实 PNG frame, 再报告 `plugin_viewer_rendered`.

所有原模型解析器, 材质/贴图加载逻辑和付费模块授权逻辑均保留. 包内不含付费 `.module`, 未迁移任何账号或授权凭据. 原插件在动画素材上显示的可选动画控制模块购买提示仍然保留; 基础动画模型可直接播放.

已在 Billfish 随包 Electron 38.8.6 上用真实原 viewer 和 thumbnail hook 运行 GLB, GLTF, OBJ, STL, FBX, PLY, VTK, XYZ, PCD, DAE, AMF, BVH. FBX 包含静态和带动画模型, 动画 FBX 与 BVH 均取得变化的真实帧. 这些测试拦截了 HTTP(S), 没有发生网络请求. 原清单还保留 Spline, SplineCode, USDZ, USDC, 3DM, 3MF, LWO, 3DS, VOX, IFC, KMZ 以及 HDR/EXR viewer 声明; 这些格式尚未逐一实测. Spline 内容可能根据其功能加载远端 runtime/WASM 或连接其原服务, 不能据此测试结果承诺所有文件离线可用.

该包要求支持 `previewProtocol=1` 的 Billfish 维护版宿主. 安装并启用后, Billfish 根据真实素材扩展名路由 viewer 和缩略图任务. 缩略图 hook 输出先写入 session 的 `preview-staging/`, 宿主校验 PNG 并发布到原生封面缓存, 原始模型文件不会被该流程覆盖. 单独打开设置页不代表原生模型接管已经验收.

逐文件原始 SHA256, 改动 SHA256, 安装包 SHA256, fixtures 来源和运行结果记录在 `task-memory/task-billfish-independent-control-3d-trim/3d-plugin/`. 本包的生成步骤不安装插件, 不更改个人 profile, 不启用插件, 不执行市场购买或激活.
