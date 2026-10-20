'use strict';
const {ipcRenderer}=require('electron');
const path=require('node:path');
const {dispatch,bootstrap,finishPreviewCreate}=require('./preload.cjs');
const {installAnimationControls}=require('./animation-controls.cjs');
const request=bootstrap.preview;
let observedCore=null,sourceLoaded=false;
const loadDiagnostics=[];
// The maintained package declares its actual Core loader. Observe the returned
// loadFile Promise without replacing parsing, material loading or license logic.
if(request.engine==='eagle-3d-core'){
  // Core has DOM initialization at module scope. Patch its export only when the
  // original page/hook requires it, after the document body exists.
  const Module=require('node:module'),originalLoad=Module._load,corePath=path.join(bootstrap.plugin.path,'lib','core.cjs');
  Module._load=function(specifier,parent,isMain){
    const exported=originalLoad.apply(this,arguments);
    if(typeof specifier==='string' && parent?.filename && path.resolve(path.dirname(parent.filename),specifier)===corePath){
      Module._load=originalLoad;
      const OriginalCore=exported.Core;
      exported.Core=class extends OriginalCore{
        constructor(...args){super(...args);const load=this.preview.loadFile.bind(this.preview);const owner=this;
          this.preview.$on('error',error=>loadDiagnostics.push({type:String(error?.type||'unknown'),message:String(error?.message||'')}));
          this.preview.loadFile=async(...values)=>{const result=await load(...values);if(path.resolve(values[0])===request.source){observedCore=owner;sourceLoaded=true;}return result;};
        }
      };
    }
    return exported;
  };
}
function reply(message){ipcRenderer.send('billfish:preview-result',message);}
window.addEventListener('load',async()=>{
  try{
    if(request.kind==='thumbnail'){
      const hook=require(request.entry);
      if(typeof hook!=='function')throw Object.assign(new Error('Thumbnail hook must export a function'),{code:'invalid_preview_hook'});
      const returned=await hook({src:request.source,dest:request.dest,item:{id:request.item.id,name:request.item.name}});
      const item={id:request.item.id};
      for(const field of ['width','height'])if(Number.isFinite(returned?.[field])&&returned[field]>0)item[field]=returned[field];
      reply({data:{item}});
    }else{
      await dispatch('create',bootstrap.plugin);
      let completion='plugin_viewer_loaded';
      if(request.engine==='eagle-3d-core'){
        const visibleError=document.querySelector('.error-output > p')?.innerText;
        const fatalDiagnostic=loadDiagnostics.some(issue=>issue.type!=='error.core.textureMissing');
        if(!sourceLoaded||!observedCore||fatalDiagnostic||(visibleError&&!loadDiagnostics.length))throw Object.assign(new Error(visibleError||'The 3D Core did not load the requested original file'),{code:'preview_render_failed'});
        await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
        const blob=await observedCore.preview.screenshot();
        const bytes=Buffer.from(await blob.arrayBuffer());
        if(bytes.length<33||!bytes.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])))throw Object.assign(new Error('The 3D renderer did not produce a PNG frame'),{code:'preview_render_failed'});
        if(loadDiagnostics.length){
          const output=document.querySelector('.error-output');
          if(output){output.setAttribute('role','status');const title=document.createElement('strong');title.textContent='模型已载入, 部分贴图缺失';title.style.cssText='display:block;margin-bottom:6px;color:#ffe0a1;font:12px/1.5 "Microsoft YaHei",sans-serif';output.prepend(title);}
        }
        installAnimationControls(observedCore.preview,{onCapture:async({blob,pose})=>{
          const response=await ipcRenderer.invoke('billfish:preview-cover',{png:new Uint8Array(await blob.arrayBuffer()),pose});
          if(response?.error)throw Object.assign(new Error(response.error.message),{code:response.error.code,details:response.error.details});
          if(response?.data?.completion!=='native_custom_cover_verified')throw Object.assign(new Error('The selected frame was not confirmed as the native cover'),{code:'cover_not_confirmed'});
          return response.data;
        }});
        completion='plugin_viewer_rendered';
      }
      reply({data:{completion,...(observedCore?{animation_count:observedCore.preview.animations.length,resource_warning_count:loadDiagnostics.length}:{})}});
    }
  }catch(error){reply({error:{code:error.code||'preview_failed',message:String(error.message||error).slice(0,4096),details:{viewer_error_visible:request.kind==='viewer'&&!!document.querySelector('.error-output > p')?.textContent.trim()}}});}
  finally{finishPreviewCreate();}
},{once:true});
