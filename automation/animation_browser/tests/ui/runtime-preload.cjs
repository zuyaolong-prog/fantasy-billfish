'use strict';
const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('billfishAnimationBrowser',{
 initialize:()=>ipcRenderer.invoke('qa-initialize'),
 call:(method,args)=>ipcRenderer.invoke('qa-call',method,args),
 onEvent:callback=>{const fn=(_,event)=>callback(event);ipcRenderer.on('qa-event',fn);return()=>ipcRenderer.removeListener('qa-event',fn);},
 ready:payload=>ipcRenderer.invoke('qa-ready',payload),
 chooseDirectory:()=>ipcRenderer.invoke('qa-directory'),
 reveal:()=>Promise.reject(new Error('QA harness does not open Explorer')),
 copyReport:()=>Promise.reject(new Error('QA harness does not write clipboard')),
 setCover:()=>Promise.reject(new Error('QA harness does not publish native covers')),
});
