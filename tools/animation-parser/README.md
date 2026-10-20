# Billfish FBX animation parser

Built from `_animation-browser-parser/`, with ufbx v0.23.0 and nlohmann/json v3.11.3. This helper is an internal read-only FBX importer; GLB source decoding belongs to the JavaScript backend.

Run `billfish-animation-parser.exe request.json response.json`. Both files are UTF-8 JSON. The explicitly named output and response must be outside the source directory; the helper rejects paths that could overwrite original assets. Request version is 1; operations are `inspect`, `model`, and `clip`, as documented in `docs/animation-browser/protocol.md`. Every response reports `parserVersion: billfish-ufbx-3`. The request and response remain UTF-8 JSON; a `clip` output is the binary format described below, not a JSON track file.

`inspect` enumerates real playable stacks and complete node ancestors without baking clips. Empty stacks are omitted with `EMPTY_ANIMATION_STACK`. Clip indices retain their original take index. Node identity is `ufbx:<element_id>` and is stable between operations on the same unchanged source and normalization profile; names alone are never identities.

`model` writes one self-contained GLB. Node extras preserve `billfishSourceId` and `billfishSourcePath`. Skin inverse binds are the normalized ufbx `geometry_to_bone` matrices. Geometry transforms and nonstandard scale inheritance use explicit helper nodes. Embedded or resolved PNG/JPEG textures are embedded in GLB; missing or unsupported optional textures produce typed warnings. Linear, rigid and Blend skins with all DQ weights exactly zero are supported. All positive influences are preserved through paired `JOINTS_n` / `WEIGHTS_n` groups, with complete-set LBS support in the maintained renderer. Static and at-most-four-influence meshes retain their original path. Actual renderer vertex-attribute limits can reject a larger representation; weights are never silently truncated. Nonzero DQ, multiple deformers on one mesh and FBX morph geometry remain typed failures rather than silent approximations. The complete hierarchy has no fixed bone-count cap.

`clip` bakes only the selected take via ufbx, retains evaluated translation, quaternion rotation and scale, respects take start/end and source FPS, and includes the exact endpoint. Source key times and ufbx cubic/step resampling times are preserved alongside frame times. Sampling uses four times the source FPS, after a real off-key pose gate demonstrated up to 4.1 cm error at source-rate-only interpolation; `fps` retains source FPS and `samplingRate` reports the bake rate. Source files are never merged or exported over.

`clip` writes `billfish-clip-f64-1` with an 8-byte `BFCLIP1` magic including its trailing NUL, followed by little-endian uint32 JSON-byte and numeric-data-byte lengths. UTF-8 metadata follows the 16-byte header, is padded to an 8-byte boundary, then contains the complete little-endian Float64 sample body. Metadata tracks retain node/property/interpolation and describe `times` / `values` as `{offset,count}` ranges in Float64 elements. A shared times range avoids duplicating the same timeline for every track. The caller must validate magic, exact lengths, metadata version and all sample ranges before use.

The small JSON `data` response contains `path`, `format: billfish-clip-f64-1`, `clipIndex`, `trackCount`, `sampleCount`, `duration`, `coordinates` and `warnings`; it does not duplicate the full numeric tracks. Source times, samples, endpoints, interpolation and Float64 values are retained. Three's existing final KeyframeTrack construction precision is unchanged; this format is not a resampling or lossy-compression scheme. Real long/medium/short comparison preserved all 18,105,230 numeric doubles exactly. The internal binary cache replaces the old JSON clip representation with a matching parser version; no legacy JSON clip fallback is part of the current contract.

Profile inputs are `interpretation: auto|maya|max|custom`, optional `sourceAxes: {right,up,front}` using signed axes, and optional `sourceUnitMeters`. `auto` respects current FBX metadata; original axis provenance is informational and is not a second conversion. Maya is the default manual preset. Canonical output is right-handed Y-up, meters. Display facing and joint orientation preferences are separately owned by persistent profiles and are not implicit retarget operations.

Build with Visual Studio 2022 C++ tools and CMake:

```powershell
cmake -S E:/Billfish/_animation-browser-parser -B E:/Billfish/_animation-browser-parser/build -G "Visual Studio 17 2022" -A x64
cmake --build E:/Billfish/_animation-browser-parser/build --config Release --parallel 4
```

The `verify-real` target verifies serialized model defaults and clips against `ufbx_evaluate_scene`, including hierarchy reconstruction and sampled skinned vertices using native binding data. This native test alone does not prove binary glTF skin serialization. `tests/verify_glb_bind.py` independently decodes the emitted GLB POSITION, JOINTS, WEIGHTS and inverseBind buffers, compares source raw positions, and verifies the supplied fixture bind pose using NumPy. The separate actual Three viewer gate verifies loaded `SkinnedMesh.getVertexPosition` values. The read-only real fixture command is:

```powershell
_animation-browser-parser/build/Release/verify-real.exe F:/TempFBX/EB2010_00.fbx F:/TempFBX/EB2010_00_Skeleton/B_AtkBeta01_0.fbx F:/TempFBX/EB2010_00_Skeleton/B_AtkBeta01_1.fbx E:/Billfish/task-memory/task-billfish-animation-browser/parser/real-gate
```

Vendored license texts are preserved under `_animation-browser-parser/vendor/`. There is no Autodesk SDK or second renderer dependency. The consulted MIT `fbx-anim-merger` reference revision is `0a577e1d7b5af59b615b8eed104e165f821192b7`; its name matching, retarget policy, root-only translations and 200-bone GPU cap were not copied.
