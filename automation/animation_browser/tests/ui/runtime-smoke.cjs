'use strict';
// A real backend + real GLTFLoader/WebGL integration check. No production testing API.
const {app,BrowserWindow,ipcMain}=require('electron'),fs=require('node:fs/promises'),path=require('node:path');
const {createAnimationBrowserService}=require('../../backend/service.cjs');
const args=Object.fromEntries(process.argv.slice(2).map(x=>{const i=x.indexOf('=');return[x.slice(0,i).replace(/^--/,''),x.slice(i+1)];}));
const source=path.resolve(args.source),output=path.resolve(args.output),scope=args.scope&&path.resolve(args.scope),sessionId='renderer-smoke';
 let win,service,readyResult,errors=[],loadedIds=[],stats,settingsCheck;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function waitFor(fn,ms=30000){const start=Date.now();while(Date.now()-start<ms){const value=await fn();if(value)return value;await sleep(100);}throw new Error('Timed out waiting for UI condition');}
app.whenReady().then(async()=>{
 try{
 await fs.mkdir(output,{recursive:true});
 service=createAnimationBrowserService({cacheRoot:path.join(output,'cache'),profileRoot:path.join(output,'profiles'),parserPath:path.resolve(args.parser||'tools/animation-parser/billfish-animation-parser.exe'),onEvent:(_,e)=>{if(win&&!win.isDestroyed())win.webContents.send('qa-event',e);}});
 const state=await service.openSession({sessionId,source,itemId:'qa-readonly',libraryRoot:path.dirname(source),discover:!scope});if(scope)await service.call(sessionId,'scope.set',{directory:scope});
 ipcMain.handle('qa-initialize',()=>({...service.state(service.sessions.get(sessionId)),source}));
 ipcMain.handle('qa-call',async(_,method,payload)=>{const result=await service.call(sessionId,method,payload);if(method==='clip.load'){loadedIds.push(payload.rowId);const {clip,clipBuffer,...metadata}=result;return result.reused?{...metadata,clip:null}:{...metadata,clipBinary:clipBuffer};}return result;});
 ipcMain.handle('qa-ready',async(_,result)=>{readyResult={...result,png:undefined,pngBytes:Buffer.from(result.png.split(',')[1],'base64').length};await fs.writeFile(path.join(output,'mesh-frame.png'),Buffer.from(result.png.split(',')[1],'base64'));return {ok:true};});
 ipcMain.handle('qa-directory',()=>scope||path.dirname(source));
 win=new BrowserWindow({show:false,width:1280,height:850,webPreferences:{preload:path.join(__dirname,'runtime-preload.cjs'),contextIsolation:true,nodeIntegration:false,offscreen:true,backgroundThrottling:false}});
 win.webContents.on('console-message',(...a)=>{const details=typeof a[1]==='object'?a[1]:{level:a[1],message:a[2]};if(details.level==='error'||details.level===3)errors.push(details.message);});
 win.webContents.on('render-process-gone',(_,d)=>errors.push('renderer gone: '+d.reason));
 await win.loadFile(path.resolve('automation/animation_browser/ui/index.html'));
 await waitFor(()=>readyResult);
 const geometryOnly=args.geometryOnly==='true';
 if(!geometryOnly){await waitFor(()=>win.webContents.executeJavaScript(`document.querySelectorAll('.clip-row').length>0`));
 const select=async index=>{const id=await win.webContents.executeJavaScript(`(()=>{const row=document.querySelectorAll('.clip-row')[${index}];row.click();return row.dataset.rowId;})()`);await waitFor(()=>win.webContents.executeJavaScript(`document.querySelector('.clip-row.current')?.dataset.rowId===${JSON.stringify(id)} && !document.getElementById('timeline').disabled`));};
 await select(0);const count=await win.webContents.executeJavaScript(`document.querySelectorAll('.clip-row').length`);if(count>1)await select(1);
 if(args.all==='true')for(let i=2;i<count;i++){await select(i);if((i+1)%25===0){console.log(`clip sweep ${i+1}/${count}`);await fs.writeFile(path.join(output,'progress.json'),JSON.stringify({completed:i+1,total:count,lastRowId:loadedIds.at(-1),at:new Date().toISOString()}));}}
 await win.webContents.executeJavaScript(`if(document.getElementById('play').textContent==='Ⅱ')document.getElementById('play').click();document.getElementById('seek-seconds').value='0.35';document.getElementById('seek-seconds').dispatchEvent(new Event('change'));`);}
 const rowCount=await win.webContents.executeJavaScript(`document.querySelectorAll('.clip-row').length`);
 if(args.settings==='true'){
  const original=service.state(service.sessions.get(sessionId)).profile;if(original.interpretation!=='auto'||original.manualPreset!=='maya')throw new Error('Default auto / Maya preference missing');
  await win.webContents.executeJavaScript(`document.getElementById('settings-toggle').click();document.getElementById('joint-secondary').value='+Z';document.getElementById('render-facing').value='90';document.getElementById('profile-form').dispatchEvent(new Event('submit',{cancelable:true,bubbles:true}));`);
  await waitFor(()=>service.state(service.sessions.get(sessionId)).profile.renderFacing.yaw===90);
  await waitFor(()=>win.webContents.executeJavaScript(`document.getElementById('profile-saved').textContent==='已保存'`));
  await win.webContents.executeJavaScript(`document.getElementById('profile-scope').value='file';document.getElementById('profile-scope').dispatchEvent(new Event('change'));`);
  await waitFor(()=>win.webContents.executeJavaScript(`document.getElementById('profile-saved').textContent==='继承角色配置'`));
  await win.webContents.executeJavaScript(`document.getElementById('joint-primary').value='+Y';document.getElementById('joint-secondary').value='+Z';document.getElementById('profile-form').dispatchEvent(new Event('submit',{cancelable:true,bubbles:true}));`);
  await waitFor(()=>Object.keys(service.state(service.sessions.get(sessionId)).profile.fileOverrides||{}).length>0);
  await waitFor(()=>win.webContents.executeJavaScript(`document.getElementById('profile-saved').textContent==='已保存'`));
  const saved=service.state(service.sessions.get(sessionId)).profile;
  settingsCheck={defaultInterpretation:original.interpretation,defaultPreset:original.manualPreset,renderFacing:saved.renderFacing,jointBasisPolicy:saved.jointBasisPolicy,fileOverrides:saved.fileOverrides};
  await win.webContents.executeJavaScript(`document.querySelector('[data-close="settings-panel"]').click()`);
 }
 await sleep(500);await fs.writeFile(path.join(output,'playing-window.png'),(await win.webContents.capturePage()).toPNG());
 const before=await win.webContents.executeJavaScript(`document.getElementById('row-count').textContent`);
 const playing=await win.webContents.executeJavaScript(`({name:document.getElementById('playing-name').textContent,time:document.getElementById('time-label').textContent,statusHidden:document.getElementById('viewport-status').hidden,canvas:document.querySelector('canvas').toDataURL('image/png')})`);
 await fs.writeFile(path.join(output,'selected-frame.png'),Buffer.from(playing.canvas.split(',')[1],'base64'));delete playing.canvas;
 await win.webContents.executeJavaScript(`document.getElementById('search').value='__does_not_exist__';document.getElementById('search').dispatchEvent(new Event('input'));`);
 const filteredCount=await win.webContents.executeJavaScript(`document.querySelectorAll('.clip-row').length`);
 if(filteredCount!==0)throw new Error('Search did not filter rows');
 stats=await service.call(sessionId,'diagnostics.get');
 const report={ok:errors.length===0,source,scope,ready:readyResult,rowCount,before,playing,filteredCount,loadedIds,settingsCheck,errors,stats:stats.stats};
 await fs.writeFile(path.join(output,'result.json'),JSON.stringify(report,null,2));if(errors.length)throw new Error(errors.join('\n'));
 console.log(JSON.stringify(report,null,2));await service.shutdown();win.destroy();app.exit(0);
 }catch(e){console.error(e.stack);if(service)await service.shutdown();await fs.mkdir(output,{recursive:true});await fs.writeFile(path.join(output,'failure.json'),JSON.stringify({error:e.message,errors,readyResult,loadedIds},null,2));app.exit(1);}
});
setTimeout(()=>{console.error(`Runtime smoke deadline exceeded; loaded ${loadedIds.length} clips`);app.exit(2);},args.all==='true'?300000:90000).unref();
