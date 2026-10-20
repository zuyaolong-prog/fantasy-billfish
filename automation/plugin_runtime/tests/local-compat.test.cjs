'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {createEagle} = require('../eagle-sdk.cjs');
const {createExtraModules} = require('../extra-modules.cjs');
const {createContextMenuApi} = require('../context-menu.cjs');

function eagleFor(row,options = {}) {
  return createEagle({call:async () => row,local:async () => {},plugin:{},app:{},register:() => {},progress:() => {},...options}).eagle;
}

test('readable original images provide a real thumbnail URL with escaped path characters',async t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(),'billfish-thumbnail-'));
  t.after(() => fs.rmSync(directory,{recursive:true,force:true}));
  const image = path.join(directory,'中文 空格#百分号%.png');
  fs.writeFileSync(image,Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a0S8AAAAASUVORK5CYII=','base64'));
  const item = await eagleFor({id:'40',name:path.basename(image),path:image,tags:[],thumbnail_path:null}).item.getById('40');
  assert.equal(item.thumbnailPath,image); assert.equal(item.filePath,image);
  assert.equal(item.noThumbnail,true); assert.equal(item.thumbnailURL,pathToFileURL(image).href);
  assert.match(item.thumbnailURL,/%23/); assert.match(item.thumbnailURL,/%25/); assert.match(item.thumbnailURL,/%20/);
  const missing = await eagleFor({id:'40',name:'missing.png',path:path.join(directory,'missing.png'),tags:[],thumbnail_path:null}).item.getById('40');
  assert.equal(missing.thumbnailPath,null); assert.equal(missing.thumbnailURL,null);
  const video = path.join(directory,'not-a-thumbnail.mp4'); fs.writeFileSync(video,'fixture');
  assert.equal((await eagleFor({id:'40',name:'not-a-thumbnail.mp4',path:video,tags:[],thumbnail_path:null}).item.getById('40')).thumbnailURL,null);
});

test('library.path is a synchronous fixed session value and no-library fails explicitly',() => {
  const eagle = eagleFor({}, {libraryRoot:'X:/original-library'});
  assert.equal(eagle.library.path,'X:/original-library');
  assert.throws(() => eagleFor({}).library.path,{code:'library_not_open'});
  assert.equal(eagle.isDev,false);
  assert.equal(eagle.os.tmpdir(),os.tmpdir());
});

test('context menu callbacks stay in the renderer and expire with the popup',async () => {
  let click, close, opened, count = 0;
  const menu = createContextMenuApi({
    openNative:(menuId,template) => { opened = {menuId,template}; },
    onClick:handler => { click = handler; },onClose:handler => { close = handler; },reportError:error => { throw error; }
  });
  const result = menu.open([{id:'parent',label:'Parent',submenu:[{id:'action',label:'Action',click:() => { count++; }}]}]);
  assert.equal(result,undefined);
  assert.doesNotThrow(() => structuredClone(opened.template));
  assert.equal(opened.template[0].submenu[0].click,undefined);
  const message = {menuId:opened.menuId,callbackId:opened.template[0].submenu[0].callbackId};
  click(message); await Promise.resolve(); assert.equal(count,1);
  close(opened.menuId); click(message); await Promise.resolve(); assert.equal(count,1);
  assert.throws(() => menu.open([{label:'unsupported',icon:Buffer.from('x')}]),{code:'unsupported_api'});
});

test('FFmpeg resolver returns only declared dependency paths whose files exist',async t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(),'billfish-dependency-'));
  t.after(() => fs.rmSync(directory,{recursive:true,force:true}));
  const module = createExtraModules(['ffmpeg'],{ffmpeg:directory}).ffmpeg;
  assert.equal(await module.isInstalled(),false);
  await assert.rejects(module.getPaths(),{code:'dependency_not_installed'});
  fs.writeFileSync(path.join(directory,'ffmpeg.exe'),'fixture'); fs.writeFileSync(path.join(directory,'ffprobe.exe'),'fixture');
  assert.equal(await module.isInstalled(),true);
  assert.deepEqual(await module.getPaths(),{ffmpeg:path.join(directory,'ffmpeg.exe'),ffprobe:path.join(directory,'ffprobe.exe')});
  await assert.rejects(createExtraModules([],{ffmpeg:directory}).ffmpeg.getPaths(),{code:'dependency_not_declared'});
  await assert.rejects(module.install(),{code:'dependency_install_required'});
});
