'use strict';
// Run with the bundled Electron executable and a selected installed plugin path.
const {app,BrowserWindow,ipcMain,nativeImage}=require('electron');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),assert=require('node:assert/strict');
const {createPreviewRuntime}=require('../preview-runtime.cjs');
const directory=path.resolve(process.argv[2]),reportPath=path.resolve(process.argv[3]);
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-preview-recovery-')),profile=path.join(temporary,'profile');
app.setPath('userData',profile);app.on('window-all-closed',()=>{});
const report={cases:[],network:[],crashes:[],errors:[]},windows=new Map();let runtime;
class TestWindow extends BrowserWindow{
 constructor(options){super({...options,webPreferences:{...options.webPreferences,offscreen:true}});windows.set(this.id,this);this.webContents.session.webRequest.onBeforeRequest({urls:['http://*/*','https://*/*']},(details,callback)=>{report.network.push(details.url);callback({cancel:true})});this.webContents.on('render-process-gone',(_event,details)=>report.crashes.push(details));}
 show(){}showInactive(){}focus(){}
}
function fixture(){
 const data=Buffer.alloc(42);[-1,-1,0,1,-1,0,0,1,0].forEach((value,index)=>data.writeFloatLE(value,index*4));[0,1,2].forEach((value,index)=>data.writeUInt16LE(value,36+index*2));
 const gltf={asset:{version:'2.0'},scene:0,scenes:[{nodes:[0]}],nodes:[{mesh:0}],meshes:[{primitives:[{attributes:{POSITION:0},indices:1,material:0}]}],materials:[{doubleSided:true,pbrMetallicRoughness:{baseColorFactor:[.1,.7,.2,1],baseColorTexture:{index:0},metallicFactor:0,roughnessFactor:1}}],textures:[{source:0}],images:[{uri:'missing-texture.png'}],buffers:[{byteLength:data.length,uri:'data:application/octet-stream;base64,'+data.toString('base64')}],bufferViews:[{buffer:0,byteOffset:0,byteLength:36},{buffer:0,byteOffset:36,byteLength:6}],accessors:[{bufferView:0,componentType:5126,type:'VEC3',count:3,min:[-1,-1,0],max:[1,1,0]},{bufferView:1,componentType:5123,type:'SCALAR',count:3}]};
 const valid=path.join(temporary,'missing-texture.gltf'),invalid=path.join(temporary,'broken.glb');fs.writeFileSync(valid,JSON.stringify(gltf));fs.writeFileSync(invalid,'not a 3D model');return {valid,invalid};
}
app.whenReady().then(async()=>{
 const manifest=JSON.parse(fs.readFileSync(path.join(directory,'manifest.json'),'utf8'));
 runtime=createPreviewRuntime({BrowserWindow:TestWindow,ipcMain,nativeImage,directory,manifest,stagingRoot:path.join(profile,'preview-staging'),bootstrap:()=>({plugin:{path:directory,manifest},libraryRoot:temporary,dependencies:{},app:{locale:'zh_CN',theme:'DARK',isDark:true,userDataPath:profile}})});
 ipcMain.on('billfish:bootstrap',event=>{const owner=runtime.ownerForEvent(event);if(owner)event.returnValue=owner.bootstrap});
 ipcMain.on('billfish:local-sync',event=>{event.returnValue={data:null}});ipcMain.handle('billfish:local-call',async()=>({data:null}));
 const files=fixture(),result=await runtime.render({id:'missing-texture',kind:'viewer',background:true,item:{id:'1',path:files.valid}});
 assert.equal(result.completion,'plugin_viewer_rendered');assert.equal(result.resource_warning_count,1);
 const window=windows.get(result.window_id),dataUrl=await window.webContents.executeJavaScript('document.querySelector("canvas").toDataURL()'),image=nativeImage.createFromDataURL(dataUrl),pixels=image.toBitmap();let opaque=0;for(let index=3;index<pixels.length;index+=4)if(pixels[index]>0)opaque++;
 assert.ok(opaque>10,'Missing textures must not prevent real geometry from rendering');report.cases.push({name:'missing-texture-renders-geometry',...result,image:image.getSize(),opaquePixels:opaque});
 await assert.rejects(runtime.render({id:'broken-model',kind:'viewer',background:true,item:{id:'2',path:files.invalid}}),error=>{report.cases.push({name:'broken-model-stays-fatal',code:error.code});return error.code==='preview_render_failed';});
 assert.equal(report.crashes.length,0);assert.equal(report.network.length,0);runtime.shutdown();fs.writeFileSync(reportPath,JSON.stringify(report,null,2));setTimeout(()=>app.exit(0),100);
}).catch(error=>{report.errors.push(String(error.stack||error));fs.writeFileSync(reportPath,JSON.stringify(report,null,2));runtime?.shutdown();app.exit(1)});
