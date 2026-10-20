'use strict';
// Hidden real-render integration gate. Run with the bundled Electron executable.
const {app,BrowserWindow,ipcMain,nativeImage,protocol,dialog,shell,clipboard}=require('electron');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),assert=require('node:assert/strict');
const {createAnimationBrowserRuntime}=require('../animation-browser-runtime.cjs');
const {encodeGlb}=require('../../animation_browser/backend/service.cjs');
protocol.registerSchemesAsPrivileged([{scheme:'billfish-animation',privileges:{standard:true,secure:true,supportFetchAPI:true,corsEnabled:true}}]);
const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-animation-browser-render-'));let runtime;
app.setPath('userData',path.join(root,'userData'));
app.on('web-contents-created',(_event,contents)=>contents.on('console-message',(_event,details)=>{if(details?.level==='error'||details?.level==='warning')console.error('renderer:',details.message);}));
app.whenReady().then(async()=>{
 const source=path.join(root,'triangle.glb'),dest=path.join(root,'staging','triangle.png'),bin=Buffer.alloc(36);[-1,-1,0,1,-1,0,0,1,0].forEach((v,i)=>bin.writeFloatLE(v,i*4));
 fs.writeFileSync(source,encodeGlb({json:{asset:{version:'2.0'},scene:0,scenes:[{nodes:[0]}],nodes:[{name:'Triangle',mesh:0}],meshes:[{primitives:[{attributes:{POSITION:0}}]}],buffers:[{byteLength:36}],bufferViews:[{buffer:0,byteLength:36}],accessors:[{bufferView:0,componentType:5126,count:3,type:'VEC3',min:[-1,-1,0],max:[1,1,0]}]},bin}));
 const directory=path.resolve(__dirname,'..','..','animation_browser'),manifest=JSON.parse(fs.readFileSync(path.join(directory,'manifest.json')));
 runtime=createAnimationBrowserRuntime({app,BrowserWindow,ipcMain,nativeImage,protocol,dialog,shell,clipboard,directory,manifest,libraryRoot:root,stagingRoot:path.join(root,'staging'),attachWindow:async()=>{},send(message){if(message.type==='animation_visibility')setImmediate(()=>runtime.receive({request_id:message.request_id,data:{applied:true}}));},publishCover:async()=>{throw new Error('Cover should not be called in thumbnail mode');}});
 const result=await runtime.render({id:'triangle',kind:'thumbnail',item:{id:'1',path:source},dest});assert.equal(result.completion,'plugin_thumbnail_rendered');assert.ok(result.width>0&&result.height>0);assert.equal(nativeImage.createFromPath(dest).isEmpty(),false);console.log(JSON.stringify({ok:true,result}));
 runtime.shutdown();app.exit(0);
}).catch(error=>{console.error(error.stack);runtime?.shutdown();app.exit(1);});
