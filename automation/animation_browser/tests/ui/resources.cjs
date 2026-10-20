'use strict';
const {app,BrowserWindow,ipcMain}=require('electron'),fs=require('node:fs/promises'),path=require('node:path');
const {createAnimationBrowserService}=require('../../backend/service.cjs');
const args=Object.fromEntries(process.argv.slice(2).map(x=>{const i=x.indexOf('=');return[x.slice(0,i).replace(/^--/,''),x.slice(i+1)];}));
const source=path.resolve(args.source||'task-memory/task-billfish-animation-browser/assets/fixtures/hundreds/character.glb'),output=path.resolve(args.output||'task-memory/task-billfish-animation-browser/viewer/resources-320');
const sessionId='renderer-resource-curve';let service,win,finished=false;const samples=[],errors=[],startedAt=new Date().toISOString();
async function finish(error,result){if(finished)return;finished=true;if(!error&&errors.length)error={message:'Renderer logged errors',details:errors};const stats=service?await service.call(sessionId,'diagnostics.get').catch(()=>null):null;const report={...result,ok:!error,startedAt,finishedAt:new Date().toISOString(),source,output,electron:process.versions.electron,rendererPid:win?.webContents.getOSProcessId(),errors,error,samples,backend:stats?.stats};await fs.writeFile(path.join(output,'result.json'),JSON.stringify(report,null,2));if(service)await service.shutdown();if(win&&!win.isDestroyed())win.destroy();console.log(JSON.stringify({ok:report.ok,samples:samples.length,selected:report.selected,error:report.error}));app.exit(error?1:0);}
app.whenReady().then(async()=>{try{
 await fs.mkdir(output,{recursive:true});
 await fs.writeFile(path.join(output,'samples.jsonl'),'');
 service=createAnimationBrowserService({cacheRoot:path.join(output,'cache'),profileRoot:path.join(output,'profiles'),parserPath:path.resolve('tools/animation-parser/billfish-animation-parser.exe')});
 await service.openSession({sessionId,source,itemId:'resource-qa-readonly',libraryRoot:path.dirname(source),discover:false});
 const state=await service.call(sessionId,'scan.refresh'),rows=state.rows.filter(r=>r.compatibility==='full'&&r.association?.enabled!==false);
 const limit=args.limit?Number(args.limit):rows.length;if(!Number.isInteger(limit)||limit<1||rows.length<limit)throw new Error('Requested clip count is unavailable');
 const expected=args.expected?Number(args.expected):null;if(expected!==null&&rows.length!==expected)throw new Error(`Expected ${expected} compatible fixture clips, got ${rows.length}`);
 ipcMain.handle('resources:initialize',()=>({model:state.model,rows:rows.slice(0,limit)}));
 ipcMain.handle('resources:clip',async(_,rowId,options={})=>{const result=await service.call(sessionId,'clip.load',{rowId,...options}),{clip,clipBuffer,...metadata}=result;return result.reused?{...metadata,clip:null}:{...metadata,clipBinary:clipBuffer};});
 ipcMain.handle('resources:sample',async(event,sample)=>{const pid=event.sender.getOSProcessId(),metric=app.getAppMetrics().find(m=>m.pid===pid);const record={...sample,rendererPid:pid,rssKiB:metric?.memory?.workingSetSize ?? null,processMemory:metric?.memory || null};samples.push(record);await fs.appendFile(path.join(output,'samples.jsonl'),JSON.stringify(record)+'\n');await fs.writeFile(path.join(output,'progress.json'),JSON.stringify(record,null,2));console.log(`resource sample ${record.phase} ${record.completed}/${limit}: geometry=${record.gpu.geometries},texture=${record.gpu.textures},program=${record.gpu.programs},cache=${record.player.cacheEntries}/${record.player.cacheBytes},rssKiB=${record.rssKiB}`);return record;});
 ipcMain.handle('resources:finish',(_,result)=>finish(null,result));ipcMain.handle('resources:fail',(_,error)=>finish(error));
 win=new BrowserWindow({show:false,width:900,height:650,webPreferences:{preload:path.join(__dirname,'resources-preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:false,offscreen:true,backgroundThrottling:false}});
 win.webContents.on('console-message',(...a)=>{const d=typeof a[1]==='object'?a[1]:{level:a[1],message:a[2]};if(d.level==='error'||d.level===3)errors.push(d.message);});
 win.webContents.on('render-process-gone',(_,details)=>finish({message:'Renderer exited: '+details.reason}));
 await win.loadFile(path.join(__dirname,'resources.html'));
 }catch(error){await fs.mkdir(output,{recursive:true});await finish({message:error.message,stack:error.stack});}});
setTimeout(()=>finish({message:'Resource curve harness deadline exceeded'}).catch(()=>app.exit(2)),300000).unref();
