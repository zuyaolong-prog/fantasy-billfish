'use strict';
// Hidden production-UI integration, independent of the running Billfish GUI.
const {app,BrowserWindow,ipcMain,nativeImage,protocol,shell,clipboard}=require('electron');
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),{spawnSync}=require('node:child_process');
const {createAnimationBrowserRuntime}=require('../animation-browser-runtime.cjs');
const root=path.resolve(__dirname,'..','..','..'),directory=path.join(root,'automation','animation_browser'),evidence=path.join(root,'task-memory','task-billfish-library-animation-mode','host','core-production-smoke'),libraryRoot='E:/Temp/Billfish-AI-Enlarge-QA';
fs.mkdirSync(evidence,{recursive:true});app.setPath('userData',path.join(evidence,'profile'));
protocol.registerSchemesAsPrivileged([{scheme:'billfish-animation',privileges:{standard:true,secure:true,supportFetchAPI:true,corsEnabled:true}}]);
const report={startedAt:new Date().toISOString(),cases:[],console:[]};let runtime,finished=false;
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function item(id){const response=spawnSync(path.join(root,'tools','python','python.exe'),['-I',path.join(root,'automation','billfish_host_helper.py')],{windowsHide:true,input:JSON.stringify({mode:'read',operation:'items.get',arguments:{item_id:id},library_root:libraryRoot}),encoding:'utf8',maxBuffer:8*1024*1024});if(response.status!==0)throw new Error(response.stderr||'Catalog read failed');const parsed=JSON.parse(response.stdout);if(!parsed.ok)throw new Error(parsed.error?.message||'Catalog item read failed');return parsed.data;}
function save(ok,error){if(finished)return;finished=true;report.ok=ok;report.error=error?.stack;report.finishedAt=new Date().toISOString();runtime?.shutdown();fs.writeFileSync(path.join(evidence,'result.json'),JSON.stringify(report,null,2));process.stdout.write(JSON.stringify({ok,result:path.join(evidence,'result.json'),cases:report.cases.map(c=>({itemId:c.item.id,extension:c.extension,rows:c.rows,disposed:c.disposed}))})+'\n',()=>app.exit(ok?0:1));}
app.on('window-all-closed',()=>{});
app.on('web-contents-created',(_event,contents)=>contents.on('console-message',(_e,details,message)=>report.console.push(typeof details==='object'?{level:details.level,message:details.message}:{level:details,message})));
app.whenReady().then(async()=>{
 const manifest=JSON.parse(fs.readFileSync(path.join(directory,'manifest.json')));
 const instrumentedIpc={on:(...args)=>ipcMain.on(...args),handle(channel,handler){ipcMain.handle(channel,async(event,method,...args)=>{
  if(method==='ready'){
   const owner=runtime.ownerForEvent(event),row=owner.rows?.[0],state=report.cases.at(-1);state.rows=owner.rows?.length||0;
   state.proof=await event.sender.executeJavaScript(`(async()=>{const b=window.billfishAnimationBrowser;let evalBlocked=false;try{new Function('return true')();}catch(e){evalBlocked=e instanceof EvalError;}const isolation={require:typeof require,process:typeof process,Core:typeof window.Core,evalBlocked};let pathRejected=false,pathError=null;try{await b.core.load('forged-container');}catch(e){pathRejected=true;pathError={name:e.name,message:e.message};}return {isolation,pathRejected,pathError,coreCanvas:!!document.querySelector('[data-billfish-core-viewport]')};})()`);
   if(!state.proof.isolation.evalBlocked||state.proof.isolation.require!=='undefined'||state.proof.isolation.process!=='undefined'||!state.proof.pathRejected||!state.proof.coreCanvas)throw new Error('Production Core isolation boundary failed');
   if(row){
    state.animation=await event.sender.executeJavaScript(`(async()=>{const b=window.billfishAnimationBrowser;const row=${JSON.stringify(row)};await b.core.select(row.id);await b.core.seek(row.duration*.2);const a=await b.core.capture();await b.core.seek(row.duration*.7);const z=await b.core.capture();const digest=async p=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',p))).map(x=>x.toString(16).padStart(2,'0')).join('');return {poseA:a.pose,poseB:z.pose,shaA:await digest(a.png),shaB:await digest(z.png)};})()`);
    if(state.animation.poseA.rowId!==row.id||state.animation.poseA.fps!==null||state.animation.shaA===state.animation.shaB)throw new Error('Core embedded pose capture did not prove distinct canonical poses');
   }
   state.disposed=await event.sender.executeJavaScript(`(async()=>{await window.billfishAnimationBrowser.core.dispose();return document.querySelector('[data-billfish-core-viewport]')===null;})()`);if(!state.disposed)throw new Error('Core dispose left its canvas container attached');
  }
  return handler(event,method,...args);
 });}};
 runtime=createAnimationBrowserRuntime({app,BrowserWindow,ipcMain:instrumentedIpc,nativeImage,protocol,shell,clipboard,directory,manifest,libraryRoot,stagingRoot:path.join(evidence,'staging'),attachWindow:async()=>{},send(message){if(message.type==='animation_visibility')setImmediate(()=>runtime.receive({request_id:message.request_id,data:{applied:true}}));},publishCover:async()=>{throw new Error('No native cover writes are allowed in this integration smoke');}});
 const only=process.argv.find(arg=>/^--only-item=[1-9][0-9]*$/.test(arg))?.split('=')[1],extra=process.argv.find(arg=>/^--extra-item=[1-9][0-9]*$/.test(arg))?.split('=')[1];for(const id of [...new Set(only?[only]:['50','63',...(extra?[extra]:[])])]){const source=item(id),before=hash(source.path),entry={item:source,extension:path.extname(source.path).slice(1)};report.cases.push(entry);const dest=path.join(evidence,'staging',id+'-'+crypto.randomUUID()+'.png'),result=await runtime.render({id:'core-smoke-'+id,kind:'thumbnail',item:{id,path:source.path,name:source.name},dest});if(result.completion!=='plugin_thumbnail_rendered'||nativeImage.createFromPath(result.dest).isEmpty()||hash(source.path)!==before)throw new Error('Production Core thumbnail or source invariant failed');entry.result=result;entry.sourceUnchanged=true;}
 save(true);
}).catch(error=>save(false,error));
setTimeout(()=>save(false,new Error('Core production integration timed out')),90000).unref();
