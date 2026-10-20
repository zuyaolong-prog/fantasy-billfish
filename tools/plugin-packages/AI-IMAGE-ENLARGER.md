# AI 图片放大本机迁移包

`ai-image-enlarger-1.0.9.eagleplugin` 是本账号现有 Eagle AI 图片放大插件的本机迁移副本. 它供 Billfish 维护运行时安装使用, 包内包括原插件和已验证的 Windows 离线计算依赖. 本次只在本机准备, 没有上传或新增公开发布.

- 文件大小: 42,067,561 bytes.
- SHA256: `000d69144efdd407f8d8682035329a1f16e78a02c27704089f0dc01f1e9a9b53`.
- 格式: ZIP, 顶层直接包含 `manifest.json`, id 为 `ai-image-enlarger`, version 为 `1.0.9`.
- 内容: 129 个原插件文件 + 7 个已验证的 Windows engine/model 文件, 共 136 个文件, 解包后 50,854,058 bytes.
- 不包含用户状态、启用状态、下载临时文件或个人素材.

在新电脑完成完整便携包初始化后, 可通过 Billfish 插件安装入口选择此 `.eagleplugin` 文件安装并启用. 插件自身文件保持原样, 与 Billfish 的接入修复由维护运行时承担. `.eagleplugin` 的系统默认打开方式属于每台电脑的用户设置, 也可直接通过插件安装入口选择文件.

`tools/export-portable.py` 的完整导出会携带此包及相邻说明、provenance 文件. `--source-only` 会排除此 `.eagleplugin` 二进制包. 插件安装后的本机状态不随软件目录复制, 因此新电脑仍需安装并启用插件.

## 来源和许可边界

原插件来自本账号已安装目录 `C:/Users/Administrator/AppData/Roaming/Eagle/Plugins/ai-image-enlarger`, manifest version `1.0.9`. 原目录没有 LICENSE 文件. 这里只准备本机自用迁移副本, 不宣称 Eagle 插件整体采用 MIT/BSD, 也不授予其他人再发行权.

7 项 engine/model 文件来自该插件原模块明示的官方 `https://r2-plugin.eagle.cool/image-enlarger/` 地址. 每项的来源 URL、声明大小、原 sample MD5 和完整 SHA256 保存在 `ai-image-enlarger-provenance/payload-manifest.json`. 两个模型是 `realesrgan-x4plus` 和 `realesrgan-x4plus-anime`. 两模型在本机 RTX 5090 上对 RGB/RGBA PNG 实际 4x 放大均成功, 透明度保留.

计算引擎 upstream 为 [Real-ESRGAN-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan), 采用 MIT license. 模型项目 upstream 为 [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN), 采用 BSD 3-Clause. 两份原始 license 文本及下载来源记录保存在相邻 `ai-image-enlarger-provenance/` 目录, 应随本机迁移副本保留.

`vcomp140.dll` 和 `vcomp140d.dll` 均来自上述 Eagle 官方地址并具有有效 Microsoft 签名. 它们是 Microsoft runtime, 尤其 `vcomp140d.dll` 为 debug runtime; upstream MIT/BSD license 不代表这些 DLL 的再发行许可. 此迁移包没有新增公开发布授权.

## 内容校验

`ai-image-enlarger-provenance/archive-manifest.json` 保存归档大小、SHA256 和全部 136 文件的大小及 SHA256. 129 个原插件文件均与迁移前原包清单一致, 7 项依赖均与已验证 payload 完整哈希一致. 归档已经通过维护运行时实际 `package_installer.py` 的独立暂存解包校验, 未安装到或覆盖运行中的插件目录.