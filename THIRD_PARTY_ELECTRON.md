# Electron plugin runtime

The JavaScript plugin host uses Electron 38.8.6 for Windows x64, distributed under `tools/electron/`. The maintained host sources live in `automation/plugin_runtime/`; its exact npm dependency and integrity metadata are recorded in `package.json` and `package-lock.json`.

Electron is licensed under MIT. The original license is preserved at `tools/electron/LICENSE`. Chromium and other bundled third-party notices are preserved at `tools/electron/LICENSES.chromium.html`. The portable runtime includes these files unchanged. Electron project: https://www.electronjs.org/ and https://github.com/electron/electron.

This runtime is independent of the installed Eagle application. It does not redistribute Eagle's application binaries, private SDK implementation, plugin packages, icons or other assets.

To prepare the runtime from a source checkout, run `npm ci` in `automation/plugin_runtime`, then copy the complete `node_modules/electron/dist` directory to `tools/electron`. Keep the package version pinned unless the host and selected plugins have been validated against a replacement version. The source-only export excludes the Electron binaries and npm dependency cache; the complete portable export includes `tools/electron`.

## i18next localization

The plugin preload also exposes the public i18next library, matching Eagle's documented host-provided localization global. The exact dependency is pinned to i18next 26.5.2 in the same npm manifest and lockfile. Its upstream standalone UMD distribution, `node_modules/i18next/dist/umd/i18next.js`, is redistributed unchanged as `automation/plugin_runtime/vendor/i18next.cjs`, so an installed portable runtime does not require npm or node_modules. The original MIT license is preserved as `automation/plugin_runtime/vendor/i18next.LICENSE`.

Upstream project: https://github.com/i18next/i18next. Package source: https://registry.npmjs.org/i18next/-/i18next-26.5.2.tgz. The npm tarball integrity is recorded in `package-lock.json`. The vendored distribution SHA-256 is `7ACE28BD98C239C22EC3544E407A0FE2A51ABBAFB0A96FFF2285F7A56319E4B7`.

Locale resources are read from each installed plugin's own `_locales` files. This does not copy Eagle's private implementation or locale resources, and does not imply compatibility with every Eagle SDK feature. The `nativeImage` global is Electron's genuine public NativeImage API, not a copied Eagle implementation.
