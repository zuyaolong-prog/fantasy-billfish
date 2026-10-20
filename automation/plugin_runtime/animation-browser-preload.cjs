'use strict';
const {contextBridge,ipcRenderer,webFrame}=require('electron');
const usesCore=process.argv.includes('--billfish-core-viewport');
if(usesCore)webFrame.setIsolatedWorldInfo(999,{securityOrigin:'file://',name:'Trusted frozen 3D Core',csp:"default-src 'self' billfish-animation://app data: blob: file:; script-src 'self' billfish-animation://app 'unsafe-eval' 'wasm-unsafe-eval'; style-src 'self' billfish-animation://app 'unsafe-inline'; connect-src 'self' billfish-animation://app data: blob: file: https:; img-src 'self' billfish-animation://app data: blob: file: https:; worker-src 'self' billfish-animation://app blob: file:; object-src 'none'"});
async function invoke(method,...args){const result=await ipcRenderer.invoke('billfish:animation',method,...args);if(result?.error)throw Object.assign(new Error(result.error.message),result.error);return result?.data;}
function frame(payload){if(typeof payload?.png==='string'){if(payload.png.length>90*1024*1024||!/^data:image\/png;base64,[A-Za-z0-9+/=]+$/.test(payload.png))throw new TypeError('A valid PNG frame is required');return {...payload,png:Uint8Array.from(Buffer.from(payload.png.slice(22),'base64'))};}return payload;}
const workers=usesCore?require('./core-workers.cjs').installCoreWorkers({reserve:()=>{const result=ipcRenderer.sendSync('billfish:core-worker-reserve');if(result?.error)throw Object.assign(new Error(result.error.message),result.error);return result;},complete:(token,code)=>invoke('core.worker.source',{token,code}),release:()=>ipcRenderer.send('billfish:core-workers-dispose'),onFailure:error=>{invoke('core.warning',{code:'core_worker_failed',message:error.message}).catch(()=>{});invoke('failed',{message:error.message}).catch(()=>{});}}):null;
const core=usesCore?require('./core-viewport.cjs').createCoreViewportBridge({getConfig:()=>invoke('core.config'),registerClips:metadata=>invoke('core.register',metadata),bindClip:rowId=>invoke('core.selected',{rowId}),onWarning:warning=>invoke('core.warning',warning).catch(error=>console.error(error.message)),cleanupWorkers:()=>workers.dispose()}):undefined;
contextBridge.exposeInMainWorld('billfishAnimationBrowser',Object.freeze({
 core,
 initialize:()=>invoke('initialize'),call:(method,args)=>invoke('call',method,args),reveal:rowId=>invoke('reveal',rowId),copyReport:rowId=>invoke('copyReport',rowId),setCover:payload=>invoke('setCover',frame(payload)),ready:payload=>invoke('ready',frame(payload)),failed:error=>invoke('failed',error),
 onEvent(callback){if(typeof callback!=='function')throw new TypeError('Callback required');const listener=(_event,data)=>callback(data);ipcRenderer.on('billfish:animation-event',listener);return ()=>ipcRenderer.removeListener('billfish:animation-event',listener);}
}));
window.addEventListener('error',event=>invoke('failed',{message:event.message}).catch(()=>{}));
window.addEventListener('unhandledrejection',event=>invoke('failed',{message:event.reason?.message||String(event.reason)}).catch(()=>{}));
