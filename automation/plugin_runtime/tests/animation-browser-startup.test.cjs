'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {createAnimationBrowserRuntime}=require('../animation-browser-runtime.cjs');

test('builtin startup restores the trusted library visibility set before any viewer exists and accepts its native ACK',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-browser-startup-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));const directory=path.join(root,'browser');fs.mkdirSync(path.join(directory,'backend'),{recursive:true});
 const hiddenPaths=[path.join(root,'animations','walk.fbx')];
 fs.writeFileSync(path.join(directory,'backend','service.cjs'),`exports.createAnimationBrowserService=options=>({async initializeLibrary(root){options.onVisibilityChanged(root,${JSON.stringify(hiddenPaths)});return {libraryRoot:root,hiddenPaths:${JSON.stringify(hiddenPaths)},diagnostics:[]};},async shutdown(){}});`);
 const sent=[];let windows=0;
 const runtime=createAnimationBrowserRuntime({app:{getPath:()=>path.join(root,'Roaming')},BrowserWindow:class{constructor(){windows++;throw new Error('Startup restore must not create a viewer');}},ipcMain:{handle(){}},protocol:{handle(){},unhandle(){}},directory,manifest:{id:'billfish.animation-browser'},libraryRoot:root,stagingRoot:path.join(root,'staging'),send:message=>sent.push(message)});t.after(()=>runtime.shutdown());
 assert.equal(sent.length,0);await new Promise(resolve=>setImmediate(resolve));assert.equal(windows,0);assert.equal(sent.length,1);assert.equal(sent[0].type,'animation_visibility');assert.deepEqual(sent[0].hidden_paths,hiddenPaths);assert.equal(typeof sent[0].request_id,'string');
 runtime.receive({type:'animation_visibility_result',request_id:sent[0].request_id,data:{applied:true,hidden_item_ids:['52'],unresolved_paths:[]}});await new Promise(resolve=>setImmediate(resolve));assert.equal(windows,0);assert.equal(sent.length,1);
});
