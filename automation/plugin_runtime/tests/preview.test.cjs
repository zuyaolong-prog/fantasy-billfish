'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {resolvePreview,validatePreviewRequest,inspectPng}=require('../preview-runtime.cjs');
const {EventEmitter}=require('node:events');
function fixture(t){
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-preview-'));
 t.after(()=>fs.rmSync(directory,{recursive:true,force:true}));
 fs.writeFileSync(path.join(directory,'viewer.html'),'<!doctype html>');fs.writeFileSync(path.join(directory,'thumbnail.js'),'module.exports=async()=>({})');
 fs.writeFileSync(path.join(directory,'original.GLB'),'synthetic original');
 return {directory,stagingRoot:path.join(directory,'preview-staging'),manifest:{id:'fixture',preview:{'glb,gltf':{viewer:{path:'viewer.html'},thumbnail:{path:'thumbnail.js'}},'hdr,exr':{viewer:{path:'viewer.html'}}}}};
}
test('format routing preserves separate viewer and thumbnail capabilities and rejects ambiguous hooks',t=>{
 const f=fixture(t);assert.equal(resolvePreview(f.directory,f.manifest,'gltf','viewer').entry,path.join(f.directory,'viewer.html'));
 assert.equal(resolvePreview(f.directory,f.manifest,'exr','viewer').entry,path.join(f.directory,'viewer.html'));
 assert.throws(()=>resolvePreview(f.directory,f.manifest,'exr','thumbnail'),{code:'unsupported_preview_format'});
 assert.throws(()=>resolvePreview(f.directory,{...f.manifest,preview:{...f.manifest.preview,glb:{viewer:{path:'viewer.html'}}}},'glb','viewer'),{code:'ambiguous_preview_format'});
 assert.throws(()=>resolvePreview(f.directory,{preview:{glb:{viewer:{path:'../viewer.html'}}}},'glb','viewer'),{code:'invalid_preview_manifest'});
});
test('thumbnail requests keep original files and existing staging outputs protected',t=>{
 const f=fixture(t),source=path.join(f.directory,'original.GLB'),dest=path.join(f.stagingRoot,'request-1.png');
 const request={id:'request-1',kind:'thumbnail',item:{id:'17',path:source},dest};
 assert.equal(validatePreviewRequest(request,f).extension,'glb');
 assert.throws(()=>validatePreviewRequest({...request,dest:path.join(f.directory,'other.png')},f),{code:'invalid_thumbnail_destination'});
 assert.throws(()=>validatePreviewRequest({...request,item:{id:'17',path:'original.GLB'}},f),{code:'invalid_arguments'});
 fs.mkdirSync(f.stagingRoot);fs.writeFileSync(dest,'existing output');
 assert.throws(()=>validatePreviewRequest(request,f),{code:'thumbnail_destination_exists'});
 assert.equal(fs.readFileSync(source,'utf8'),'synthetic original');assert.equal(fs.readFileSync(dest,'utf8'),'existing output');
});
test('thumbnail validation uses actual PNG dimensions and rejects malformed output',t=>{
 const f=fixture(t),file=path.join(f.directory,'image.png');
 const png=Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a0S8AAAAASUVORK5CYII=','base64');
 fs.writeFileSync(file,png);assert.deepEqual(inspectPng(file),{width:1,height:1,bytes:png.length});
 fs.writeFileSync(file,'not png');assert.throws(()=>inspectPng(file),{code:'invalid_thumbnail_output'});
 const invalid=Buffer.from(png);invalid.writeUInt32BE(0,16);fs.writeFileSync(file,invalid);assert.throws(()=>inspectPng(file),{code:'invalid_thumbnail_output'});
});
test('native owner failure rejects the preview without waiting for a renderer frame', {timeout:1000},async t=>{
 const f=fixture(t),ipcMain=new EventEmitter();
 class Window extends EventEmitter{
  constructor(){super();this.id=1;this.destroyed=false;this.webContents=new EventEmitter();this.webContents.id=1;this.webContents.setWindowOpenHandler=()=>{};}
  isDestroyed(){return this.destroyed;}
  loadFile(){return Promise.resolve();}
  destroy(){this.destroyed=true;this.emit('closed');}
 }
 const {createPreviewRuntime}=require('../preview-runtime.cjs');
 const runtime=createPreviewRuntime({...f,BrowserWindow:Window,ipcMain,nativeImage:{},bootstrap:()=>({app:{isDark:false,locale:'en'}}),attachWindow:()=>Promise.reject(Object.assign(new Error('Billfish owner closed'),{code:'window_owner_unavailable'}))});
 t.after(()=>runtime.shutdown());
 await assert.rejects(runtime.render({id:'owner-failure',kind:'viewer',background:true,item:{id:'17',path:path.join(f.directory,'original.GLB')}}),{code:'window_owner_unavailable'});
});
