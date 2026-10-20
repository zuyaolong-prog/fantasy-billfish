'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {failure} = require('./eagle-sdk.cjs');

// This resolver consumes only the installed dependency roots granted to this
// plugin session. It neither searches Eagle's profile nor downloads binaries.
function createExtraModules(declarations, roots) {
  const declared = new Set(Array.isArray(declarations) ? declarations : []);
  function ffmpegPaths() {
    if (!declared.has('ffmpeg')) throw failure('dependency_not_declared','Declare dependencies:["ffmpeg"] in the plugin manifest');
    const root = roots.ffmpeg;
    if (typeof root !== 'string' || !path.isAbsolute(root)) throw failure('dependency_not_installed','Install the FFmpeg dependency package in Billfish plugin management, activate any pending update, then restart this plugin');
    const paths = {ffmpeg:path.join(root,'ffmpeg.exe'),ffprobe:path.join(root,'ffprobe.exe')};
    for (const file of Object.values(paths)) {
      try { if (!fs.statSync(file).isFile()) throw new Error('Not a file'); }
      catch { throw failure('dependency_not_installed','The installed FFmpeg dependency package is missing ffmpeg.exe or ffprobe.exe'); }
    }
    return paths;
  }
  return {ffmpeg:{
    isInstalled:async () => {
      try { ffmpegPaths(); return true; }
      catch(error) { if (error.code === 'dependency_not_declared' || error.code === 'dependency_not_installed') return false; throw error; }
    },
    getPaths:async () => ffmpegPaths(),
    install:async () => { throw failure('dependency_install_required','Open Billfish Settings > Plugins, install the FFmpeg dependency package, activate any pending update, then restart this plugin. No package was downloaded or installed by this call.'); }
  }};
}

module.exports = {createExtraModules};
