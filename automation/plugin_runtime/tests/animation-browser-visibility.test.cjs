'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{EventEmitter}=require('node:events');
const {createAnimationBrowserRuntime}=require('../animation-browser-runtime.cjs');

async function fixture(t){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-browser-visibility-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));const directory=path.join(root,'browser'),source=path.join(root,'mesh.glb'),cache=path.join(root,'Local','Billfish','animation-browser','cache','models');fs.mkdirSync(path.join(directory,'backend'),{recursive:true});fs.mkdirSync(path.join(directory,'ui'));fs.mkdirSync(cache,{recursive:true});fs.writeFileSync(path.join(directory,'ui','index.html'),'test');fs.writeFileSync(source,'mesh');
 const paths=[path.join(root,'a.fbx'),path.join(root,'b.fbx')],backendPath=path.join(directory,'backend','service.cjs');
 fs.writeFileSync(backendPath,`exports.createAnimationBrowserService=options=>{let library;exports.publish=paths=>options.onVisibilityChanged(library,paths);return {async initializeLibrary(root){library=root;options.onVisibilityChanged(root,[]);return {diagnostics:[]};},async openSession(r){return {source:r.source,scope:require('node:path').dirname(r.source),model:{path:${JSON.stringify(path.join(cache,'mesh.glb'))}},rows:[]};},async call(){exports.publish(${JSON.stringify(paths)});return {saved:true};},async closeSession(){},async shutdown(){}};};`);
 const windows=[];class Window extends EventEmitter{constructor(){super();this.webContents=new EventEmitter();this.webContents.id=1;this.webContents.mainFrame={};this.webContents.send=()=>{};this.webContents.setWindowOpenHandler=()=>{};windows.push(this);}isDestroyed(){return !!this.destroyed;}destroy(){this.destroyed=true;this.emit('closed');}async loadURL(){this.webContents.emit('dom-ready');}setMenu(){}show(){}showInactive(){}focus(){}}
 const handlers={},sent=[];let runtime,nativePending;
 runtime=createAnimationBrowserRuntime({app:{getPath:()=>path.join(root,'Roaming')},BrowserWindow:Window,ipcMain:{handle(name,fn){handlers[name]=fn;}},protocol:{handle(){},unhandle(){}},directory,manifest:{id:'billfish.animation-browser',preview:{glb:{viewer:{path:'ui/index.html'}}}},libraryRoot:root,stagingRoot:path.join(root,'staging'),attachWindow:async()=>{},send(message){if(message.type!=='animation_visibility')return;if(!message.hidden_paths.length){setImmediate(()=>runtime.receive({request_id:message.request_id,data:{applied:true}}));return;}sent.push(message);if(nativePending)runtime.receive({request_id:nativePending,error:{code:'visibility_superseded',message:'A newer animation visibility snapshot replaced this request.'}});nativePending=message.request_id;}});t.after(()=>runtime.shutdown());
 runtime.render({id:'visibility',kind:'viewer',item:{id:'58',path:source}}).catch(()=>{});const window=windows[0],event={sender:window.webContents,senderFrame:window.webContents.mainFrame},invoke=(method,...args)=>handlers['billfish:animation'](event,method,...args);await invoke('initialize');await new Promise(resolve=>setImmediate(resolve));
 return {runtime,paths,sent,publish:require(backendPath).publish,invoke};
}

test('duplicate full visibility publications share a real native ACK instead of superseding the user action',async t=>{
 const f=await fixture(t),action=f.invoke('call','visibility.update',{rowId:'56',show:true});await new Promise(resolve=>setImmediate(resolve));f.publish([...f.paths].reverse());f.runtime.receive({request_id:f.sent.at(-1).request_id,data:{applied:true,hidden_item_ids:['52','53']}});
 const result=await action;assert.equal(result.error,undefined);assert.equal(result.data.saved,true);assert.equal(f.sent.length,1);
});

test('a real native failure is still rejected for a shared visibility publication',async t=>{
 const f=await fixture(t),action=f.invoke('call','visibility.update',{rowId:'56',show:true});await new Promise(resolve=>setImmediate(resolve));f.publish(f.paths);f.runtime.receive({request_id:f.sent.at(-1).request_id,error:{code:'native_visibility_unavailable',message:'Authenticated source model was lost'}});
 const result=await action;assert.equal(result.error.code,'native_visibility_unavailable');
});

test('different full snapshots never inherit another snapshot successful native ACK',async t=>{
 const f=await fixture(t),action=f.invoke('call','visibility.update',{rowId:'56',show:true});await new Promise(resolve=>setImmediate(resolve));f.publish([f.paths[0]]);f.runtime.receive({request_id:f.sent.at(-1).request_id,data:{applied:true,hidden_item_ids:['52']}});
 const result=await action;assert.equal(result.error.code,'visibility_superseded');assert.equal(f.sent.length,2);
});
