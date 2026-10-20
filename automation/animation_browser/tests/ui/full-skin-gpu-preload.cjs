'use strict';
const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('skinProbe',{initialize:()=>ipcRenderer.invoke('skin:init'),capture:(name,data)=>ipcRenderer.invoke('skin:capture',name,data),finish:result=>ipcRenderer.invoke('skin:finish',result),fail:error=>ipcRenderer.invoke('skin:fail',error)});
