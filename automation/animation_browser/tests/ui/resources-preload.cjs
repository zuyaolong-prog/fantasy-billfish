'use strict';
const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('resourceProbe',{
 initialize:()=>ipcRenderer.invoke('resources:initialize'),
 loadClip:(rowId,options)=>ipcRenderer.invoke('resources:clip',rowId,options),
 sample:sample=>ipcRenderer.invoke('resources:sample',sample),
 finish:result=>ipcRenderer.invoke('resources:finish',result),
 fail:error=>ipcRenderer.invoke('resources:fail',error),
 heap:()=>typeof process.getHeapStatistics==='function'?process.getHeapStatistics():null,
});
