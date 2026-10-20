'use strict';
const {ipcRenderer,webFrame}=require('electron');
webFrame.setIsolatedWorldInfo(999,{securityOrigin:'file://',name:'Reference-pose proof',csp:"default-src 'self' file: data: blob:; script-src 'self' 'unsafe-eval' 'wasm-unsafe-eval'; connect-src 'self' file: data: blob:; style-src 'self' 'unsafe-inline'; img-src 'self' file: data: blob:; object-src 'none'"});
const frames=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
window.addEventListener('DOMContentLoaded',async()=>{
 let core,controller;const report={};
 try{
  const config=await ipcRenderer.invoke('reference:config'),path=require('node:path');window.envConfig={VITE_API_HOST:'https://api.eagle.pome.run'};window.i18next=require(config.i18next).createInstance();await window.i18next.init({lng:'en',resources:{en:{translation:{}}},initImmediate:false});const {Core}=require(path.join(config.library,'core.cjs')),{createAnimationController}=require(config.controller),{restoreCoreReferencePose}=require(config.adapter);
  core=new Core({dom:document.getElementById('viewport'),width:640,height:480,devicePixelRatio:1,resizeable:false,hdrPath:path.join(config.library,'hdr','background.hdr'),DRACODecoderPath:path.join(config.library,'draco')+path.sep,KTX2TranscoderPath:path.join(config.library,'basis')+path.sep});await core.preview.loadFile(config.source,'gltf');await frames();controller=createAnimationController(core.preview);if(!controller)throw new Error('Fixture animation was not loaded');controller.pause();controller.seek(0);const target=core.preview.mesh.getObjectByName('CoreReferenceNode');if(!target)throw new Error('Authored fixture node was lost');report.authoredRestX=2;report.firstKeyX=8;report.beforeRestoreX=target.position.x;if(Math.abs(report.beforeRestoreX-8)>1e-6)throw new Error('The fixture did not distinguish first key from authored rest');
  restoreCoreReferencePose(core.preview);await frames();report.afterRestoreX=target.position.x;report.activeActions=core.preview.animationActions.filter(action=>action.isRunning()).length;if(Math.abs(report.afterRestoreX-2)>1e-6||report.activeActions!==0)throw new Error('Reference restore did not deactivate animation and recover authored transform');report.png=new Uint8Array(await(await core.preview.screenshot()).arrayBuffer());report.ok=true;
 }catch(error){report.ok=false;report.error={message:error.message,stack:error.stack};}
 finally{try{controller?.dispose();await core?.preview.destroy();document.getElementById('viewport').replaceChildren();}catch(error){report.ok=false;report.disposeError=error.message;}}
 await ipcRenderer.invoke('reference:result',report);
},{once:true});
