'use strict';
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const {createAnimationController}=require('./animation-controller.cjs');
const fail=(code,message)=>Object.assign(new Error(message),{code});
const number=(value,name)=>{if(typeof value!=='number'||!Number.isFinite(value))throw fail('invalid_arguments',`${name} must be finite`);return value;};
const frames=()=>new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(fail('core_frame_timeout','The model renderer did not draw a frame')),5000);requestAnimationFrame(()=>requestAnimationFrame(()=>{clearTimeout(timer);resolve();}));});
function restoreCoreReferencePose(preview){preview.animationMixer?.stopAllAction?.();preview.scene?.updateMatrixWorld?.(true);if(typeof preview.composer?.render==='function')preview.composer.render();else preview.renderer?.render?.(preview.scene,preview.camera);}

function createCoreViewportBridge({getConfig,registerClips,bindClip,onWarning,cleanupWorkers}){
 let core=null,controller=null,skeletonOverlay=null,config=null,dom=null,ownedDom=null,specialCanvas=null,resizeObserver=null,tick=null,eventHandler=null,selected=null,rows=[],disposed=false,disposeRequested=false,tail=Promise.resolve(),initialCamera=null,initialFacing=0;
 const subscribers=new Set();
 function serial(operation,disposing=false){if(disposeRequested&&!disposing)return Promise.reject(fail('core_viewport_disposed','The model viewport was disposed'));const next=tail.then(()=>{if(disposed&&!disposing)throw fail('core_viewport_disposed','The model viewport was disposed');return operation();});tail=next.catch(()=>{});return next;}
 function state(){const value=controller?.getState(),row=rows.find(row=>row.id===selected);return {rowId:row?.id||null,clipIndex:row?.clipIndex??null,clipName:row?.name||'',time:row?value?.time||0:0,duration:row?value?.duration||0:0,playing:!!(row&&value?.playing),speed:value?.speed||1,loop:value?.loop??true,fps:value?.fps||30,hasAction:!!row,busy:!!value?.busy,hasSkeleton:skeletonOverlay?.available===true,skeletonVisible:skeletonOverlay?.visible===true};}
 function emit(){const value=state();for(const callback of subscribers)try{callback(value);}catch(error){console.error('Core viewport state listener failed:',error.message);}return value;}
 function loaded(){if(!core||disposed)throw fail('model_not_loaded','The model is not loaded');}
 function active(){loaded();if(!controller||!selected)throw fail('animation_unavailable','Select an embedded animation first');}
 function draw(){if(typeof core?.preview.composer?.render==='function')core.preview.composer.render();else core?.preview.renderer?.render?.(core.preview.scene,core.preview.camera);}
 function resize(){loaded();const width=Math.max(1,dom.clientWidth),height=Math.max(1,dom.clientHeight),camera=core.preview.camera;core.preview.renderer?.setSize?.(width,height);core.preview.composer?.setSize?.(width,height);if(camera){camera.aspect=width/height;camera.updateProjectionMatrix();}if(specialCanvas?.style){specialCanvas.style.width='100%';specialCanvas.style.height='100%';}draw();return {width,height};}
 async function screenshot(){if(specialCanvas)return new Promise((resolve,reject)=>specialCanvas.toBlob(blob=>blob?resolve(blob):reject(fail('core_capture_failed','The source canvas did not produce an image'))));return core.preview.screenshot();}
 function saveCamera(){const camera=core.preview.camera,target=core.preview.controls?.target;if(camera)initialCamera={position:camera.position.clone(),quaternion:camera.quaternion.clone(),up:camera.up.clone(),target:target?.clone(),near:camera.near,far:camera.far};}
 function resetCamera(){loaded();const camera=core.preview.camera;if(initialCamera&&camera){camera.position.copy(initialCamera.position);camera.quaternion.copy(initialCamera.quaternion);camera.up.copy(initialCamera.up);camera.near=initialCamera.near;camera.far=initialCamera.far;camera.updateProjectionMatrix();if(initialCamera.target)core.preview.controls?.target?.copy(initialCamera.target);core.preview.controls?.update?.();}draw();return state();}
 function frame(){
  loaded();const root=core.preview.mesh,camera=core.preview.camera;let box=null;root?.updateMatrixWorld?.(true);
  root?.traverse?.(object=>{if(!object.geometry)return;let local;if(object.isSkinnedMesh&&typeof object.computeBoundingBox==='function'){object.computeBoundingBox();local=object.boundingBox;}else{if(!object.geometry.boundingBox)object.geometry.computeBoundingBox?.();local=object.geometry.boundingBox;}if(!local||local.isEmpty())return;const transformed=local.clone().applyMatrix4(object.matrixWorld);if(box)box.union(transformed);else box=transformed;});
  if(!box||!camera?.isPerspectiveCamera)return resetCamera();
  const center=box.min.clone().add(box.max).multiplyScalar(.5),size=box.max.clone().sub(box.min),radius=Math.max(size.length()/2,.01),target=core.preview.controls?.target||center,direction=camera.position.clone().sub(target);if(direction.lengthSq()<1e-8)direction.set(.8,.35,1.2);direction.normalize();const vertical=camera.fov*Math.PI/180,horizontal=2*Math.atan(Math.tan(vertical/2)*camera.aspect),distance=radius/Math.sin(Math.min(vertical,horizontal)/2)*1.1;
  camera.near=Math.max(radius/10000,.00001);camera.far=Math.max(radius*1000,100);camera.position.copy(center).addScaledVector(direction,distance);camera.updateProjectionMatrix();if(core.preview.controls?.target)core.preview.controls.target.copy(center);camera.lookAt(center);core.preview.controls?.update?.();draw();return state();
 }
 async function load(containerId){
  if(containerId!=='viewport')throw fail('invalid_arguments','The model viewport container is fixed');if(core)throw fail('model_already_loaded','The viewport already owns a model');
  config=await getConfig();if(config.renderer!=='core')throw fail('permission_denied','This preview does not use the Core renderer');
  dom=document.getElementById('viewport');if(!dom)throw fail('model_container_missing','The shared model viewport is missing');
  ownedDom=document.createElement('div');ownedDom.style.cssText='position:relative;width:100%;height:100%;overflow:hidden';ownedDom.setAttribute('data-billfish-core-viewport','');dom.append(ownedDom);
  window.isDev=false;window.envConfig={VITE_API_HOST:'https://api.eagle.pome.run'};const i18next=require('./vendor/i18next.cjs');window.i18next=i18next.createInstance();await window.i18next.init({lng:'zh-CN',fallbackLng:'en',resources:{en:{translation:{}}},initImmediate:false});
  const {Core}=require(path.join(config.library,'core.cjs')),warnings=[];
  const resource=relative=>new URL(relative,config.resourceUrl).href;
  core=new Core({dom:ownedDom,width:Math.max(1,dom.clientWidth),height:Math.max(1,dom.clientHeight),devicePixelRatio:Math.min(window.devicePixelRatio||1,2),resizeable:false,KTX2TranscoderPath:resource('basis/'),DRACODecoderPath:resource('draco/'),hdrPath:resource('hdr/background.hdr'),usdzDependenciesPath:resource('usdz/'),rhino3dmLibraryPath:resource('3dm/'),ifcWasmPath:resource('ifc/')});
  eventHandler=error=>{const warning={code:String(error.type||'core_resource_error'),message:String(error.message||'Core resource error')};warnings.push(warning);onWarning(warning);};core.preview.$on('error',eventHandler);
  let loadResult;if(['usdz','usdc'].includes(config.extension)){const bytes=await require('node:fs/promises').readFile(config.source);loadResult=await core.preview.loadFile(config.sourceUrl,config.extension,new File([bytes],path.basename(config.source)));}else loadResult=await core.preview.loadFile(config.sourceUrl,config.extension);if(['spline','splinecode'].includes(config.extension)&&typeof loadResult?.toBlob==='function')specialCanvas=loadResult;
  if(warnings.some(warning=>warning.code!=='error.core.textureMissing'))throw fail('core_load_failed',warnings.map(warning=>warning.message).join('\n'));
  await frames();if(disposeRequested)throw fail('core_viewport_disposed','The viewport closed while loading');
  controller=createAnimationController(core.preview);if(controller)controller.pause();restoreCoreReferencePose(core.preview);saveCamera();initialFacing=core.preview.mesh?.rotation?.y||0;resize();
  let meshCount=0,drawableCount=0,boneCount=0,skeletonHelpers=0,skinnedSkeleton=false;core.preview.mesh?.traverse?.(object=>{if(object.isGridHelper||object.isAxesHelper)return;if(!(object.geometry?.attributes?.position?.count>0))return;if(object.isMesh)meshCount++;if(object.isMesh||object.isPoints||object.isLine||object.isSprite)drawableCount++;if(object.isSkinnedMesh&&object.skeleton?.bones?.some(bone=>bone?.isBone))skinnedSkeleton=true;});let sourceKind=drawableCount?'geometry':null;
  if(skinnedSkeleton){const {SkeletonOverlay}=await import(pathToFileURL(path.join(__dirname,'..','animation_browser','ui','skeleton-overlay.mjs')).href);if(disposeRequested)throw fail('core_viewport_disposed','The viewport closed while loading');skeletonOverlay=new SkeletonOverlay(core.preview.scene,core.preview.mesh);}
  if(!sourceKind&&config.extension==='bvh'){core.preview.scene?.traverse?.(object=>{if(object.isBone)boneCount++;if(object.isSkeletonHelper||object.type==='SkeletonHelper')skeletonHelpers++;});if(boneCount>0&&skeletonHelpers>0){sourceKind='skeleton';drawableCount=skeletonHelpers;}}
  const environmentTexture=['hdr','exr'].includes(config.extension)&&core.preview.scene?.background?.isTexture===true;if(environmentTexture)sourceKind='environment';
  const renderedFrame=core.preview.renderer?.info?.render?.frame||0;let canvasProof;
  if(specialCanvas){const blob=await screenshot();canvasProof=new Uint8Array(await blob.arrayBuffer());sourceKind='canvas';drawableCount=Math.max(drawableCount,1);}if(!sourceKind||(renderedFrame<1&&!canvasProof))throw fail('core_render_failed','The original source has not produced drawable model content');
  const registered=await registerClips({clips:(controller?.clips||[]).map(clip=>({index:clip.index,name:clip.name,duration:clip.duration,fps:null})),meshCount,drawableCount,renderedFrame,sourceKind,boneCount,skeletonHelpers,environmentTexture,canvasFrame:!!canvasProof,png:canvasProof,displayFacing:!!core.preview.mesh?.rotation,hasSkeleton:skeletonOverlay?.available===true});rows=registered.rows;selected=null;
  resizeObserver=new ResizeObserver(()=>{if(!disposed&&!disposeRequested)serial(()=>resize()).catch(error=>onWarning({code:error.code||'core_resize_failed',message:error.message}));});resizeObserver.observe(dom);tick=setInterval(()=>{if(!disposed&&!disposeRequested)emit();},80);
  return {meshCount,drawableCount,renderedFrame,rows,hasSkeleton:skeletonOverlay?.available===true,skeletonVisible:false,capabilities:registered.capabilities,state:emit(),warnings};
 }
 async function capture(){
  loaded();if(controller&&selected){const row=rows.find(row=>row.id===selected),snapshot=state();return controller.capture(async({blob})=>({png:new Uint8Array(await blob.arrayBuffer()),pose:{kind:'clip',rowId:row.id,clip_index:row.clipIndex,clip_name:row.name,time:snapshot.time,fps:row.fps??null},state:snapshot}));}
  draw();const blob=await screenshot();if(!blob||typeof blob.arrayBuffer!=='function')throw fail('core_capture_failed','The model screenshot is unavailable');return {png:new Uint8Array(await blob.arrayBuffer()),pose:{kind:'base',time:0},state:state()};
 }
 async function dispose(){
  if(disposed)return {disposed:true};disposed=true;clearInterval(tick);resizeObserver?.disconnect();window.removeEventListener('beforeunload',unload);subscribers.clear();let firstError;
  try{controller?.dispose();}catch(error){firstError=error;}
  try{skeletonOverlay?.dispose();}catch(error){firstError||=error;}
  try{if(core){if(typeof core.preview.$off==='function'&&eventHandler)core.preview.$off('error',eventHandler);core.preview.renderer?.setAnimationLoop?.(null);await core.preview.destroy();}}catch(error){firstError||=error;}
  try{cleanupWorkers?.();}catch(error){firstError||=error;}
  finally{ownedDom?.remove();core=null;controller=null;skeletonOverlay=null;specialCanvas=null;rows=[];selected=null;}
  if(firstError)throw firstError;return {disposed:true};
 }
 function unload(){disposeRequested=true;serial(dispose,true).catch(error=>console.error('Core viewport disposal failed:',error.message));}
 window.addEventListener('beforeunload',unload,{once:true});
 return Object.freeze({
  load:containerId=>serial(()=>load(containerId)),
  select:rowId=>serial(async()=>{loaded();const row=rows.find(row=>row.id===rowId);if(!row||!controller)throw fail('animation_unavailable','Unknown embedded animation');controller.pause();controller.selectClip(row.clipIndex);await bindClip(row.id);selected=row.id;return emit();}),
  setPlaying:value=>serial(()=>{active();if(typeof value!=='boolean')throw fail('invalid_arguments','Playing must be a boolean');if(value)controller.play();else controller.pause();return emit();}),
  seek:value=>serial(()=>{active();controller.seek(number(value,'time'));return emit();}),
  setLoop:value=>serial(()=>{active();controller.setLoop(value);return emit();}),
  setSpeed:value=>serial(()=>{active();controller.setSpeed(number(value,'speed'));return emit();}),
  setFps:value=>serial(()=>{active();controller.setFps(number(value,'fps'));return emit();}),
  frame:()=>serial(frame),resetCamera:()=>serial(resetCamera),resize:()=>serial(resize),
  setFacing:value=>serial(()=>{loaded();number(value,'facing');if(core.preview.mesh?.rotation){core.preview.mesh.rotation.y=initialFacing+value*Math.PI/180;core.preview.mesh.updateMatrixWorld(true);draw();}return state();}),
  setSkeletonVisible:value=>serial(()=>{loaded();if(typeof value!=='boolean')throw fail('invalid_arguments','Skeleton visibility must be a boolean');skeletonOverlay?.setVisible(value);draw();return emit();}),
  capture:()=>serial(capture),
  dispose:()=>{disposeRequested=true;return serial(dispose,true);},
  onState:callback=>{if(typeof callback!=='function')throw new TypeError('State callback required');subscribers.add(callback);return ()=>subscribers.delete(callback);}
 });
}
module.exports={createCoreViewportBridge,restoreCoreReferencePose};
