'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {validatePreviewRequest,inspectPng}=require('./preview-runtime.cjs');
const {fingerprintFile}=require('./preview-cover.cjs');
const {createAnimationAssets}=require('./animation-browser-assets.cjs');
const {createLibraryCatalogReader}=require('./animation-browser-catalog.cjs');
function failure(code,message){return Object.assign(new Error(message),{code});}
function createAnimationBrowserRuntime({app,BrowserWindow,ipcMain,nativeImage,protocol,dialog,shell,clipboard,directory,manifest,libraryRoot,stagingRoot,attachWindow,send,publishCover}){
 const owners=new Map(),pending=new Map(),viewerRequests=new Map();let latestViewerIntent=null;let stopped=false,visibilitySync=null,visibilityGeneration=0,visibilityFlight=null,viewerGeneration=0,workerIpcReady=false,intentOwner=null;
 let nativeVisibility={state:'restoring',libraryRoot},startupDiagnostics=[];
 const cacheRoot=path.join(app.getPath('appData'),'..','Local','Billfish','animation-browser','cache');
 const assets=createAnimationAssets({protocol,directory,cacheRoot});
 const catalog=createLibraryCatalogReader({installRoot:path.resolve(directory,'..','..'),libraryRoot});
 const {createAnimationBrowserService}=require(path.join(directory,'backend','service.cjs'));
 const service=createAnimationBrowserService({cacheRoot,profileRoot:path.join(libraryRoot,'.bf','animation-browser','profiles'),parserPath:path.join(directory,'..','..','tools','animation-parser','billfish-animation-parser.exe'),getLibraryCandidates:catalog.getLibraryCandidates,onEvent(id,event){const o=[...owners.values()].find(o=>o.sessionId===id);if(o&&!o.closed){try{if(!o.initialized)o.initialEvents.push(event);else o.window.webContents.send('billfish:animation-event',decorate(o,event));}catch(error){o.reject(error);}}},onVisibilityChanged(root,hiddenPaths){
  if(stopped||path.resolve(root).toLowerCase()!==path.resolve(libraryRoot).toLowerCase())return;
  // Each publication replaces the whole library set. Profile watchers and a
  // viewer action can publish that same set concurrently; submitting it twice
  // makes the native mapper invalidate the first request's real ACK.
  const key=JSON.stringify([...new Set(hiddenPaths.map(p=>path.resolve(p).toLowerCase()))].sort());
  if(visibilityFlight?.key===key)return;
  const flight={key};visibilityFlight=flight;
  const generation=++visibilityGeneration;nativeVisibility={state:'restoring',libraryRoot,requestedHiddenPaths:hiddenPaths.length};
  visibilitySync=requestNative('animation_visibility',{hidden_paths:hiddenPaths}).then(data=>{
   if(data?.applied!==true)throw failure('visibility_not_confirmed','Billfish did not confirm the native visibility filter');
   if(generation===visibilityGeneration)nativeVisibility={state:'applied',libraryRoot,requestedHiddenPaths:hiddenPaths.length,data};return data;
  }).catch(error=>{if(generation===visibilityGeneration){nativeVisibility={state:'failed_open',libraryRoot,error:{code:error.code||'native_visibility_failed',message:error.message}};for(const o of owners.values())if(!o.closed)o.window.webContents.send('billfish:animation-event',{type:'warning',warning:nativeVisibility.error});}return {error};}).finally(()=>{if(visibilityFlight===flight)visibilityFlight=null;});
 }});
 // Restore confirmed library associations without opening a model. The service
 // publishes its full replacement set, including [], through the callback above.
 Promise.resolve().then(async()=>{
  const restored=await service.initializeLibrary(libraryRoot);if(stopped)return;startupDiagnostics=restored.diagnostics||[];
  if(!visibilitySync)throw failure('visibility_not_confirmed','Library initialization did not publish its visibility state');
  const result=await visibilitySync;if(result?.error)throw result.error;
  if(restored.diagnostics?.length)for(const o of owners.values())if(!o.closed)for(const warning of restored.diagnostics)o.window.webContents.send('billfish:animation-event',{type:'warning',warning});
 }).catch(error=>{if(stopped)return;const warning={code:error.code||'visibility_restore_failed',message:error.message};startupDiagnostics.push(warning);if(nativeVisibility.state!=='applied')nativeVisibility={state:'failed_open',libraryRoot,error:warning};for(const o of owners.values())if(!o.closed)o.window.webContents.send('billfish:animation-event',{type:'warning',warning});});
 function superseded(){return failure('preview_superseded','A newer model preview replaced this request');}
 function ensureViewer(context){if(context&&(stopped||context.controller.signal.aborted||context.intent!==latestViewerIntent||context.generation!==viewerGeneration))throw superseded();}
 function ensureOwner(o,allowFailure=false){if(stopped||o.closed||owners.get(o.contentsId)!==o)throw failure('preview_window_closed','Animation browser closed');if(o.failure&&!allowFailure)throw o.failure;if(allowFailure&&o.failure&&o.context?.intent===latestViewerIntent&&o.context.generation===viewerGeneration)return;ensureViewer(o.context);}
 function sendLoading(o,stage=o.loadingStage){ensureOwner(o);o.loadingStage=stage;o.window.webContents.send('billfish:animation-event',{type:'loading',payload:{stage,source:o.request.source,name:o.request.item.name,libraryContext:o.libraryContext}});}
 function cancelPreview(id){const context=viewerRequests.get(id);if(context?.owner?.loadingStage==='error')return false;if(context){context.controller.abort(superseded());if(context.owner?.renderRequestId===id)close(context.owner,superseded());}for(const o of [...owners.values()])if(o.request.kind==='viewer'&&o.renderRequestId===id&&o.loadingStage!=='error')close(o,superseded());return !!context;}
 function admitViewerIntent(id,feedback){if(typeof id!=='string'||!id||id.length>256)throw failure('invalid_arguments','A bounded viewer intent ID is required');if(id===latestViewerIntent)return;latestViewerIntent=id;++viewerGeneration;if(intentOwner&&!intentOwner.completedResult)close(intentOwner,superseded());intentOwner=null;for(const context of viewerRequests.values()){context.controller.abort(superseded());if(context.owner&&!context.owner.completedResult)close(context.owner,superseded());}
  if(!feedback)return;
  const extension=String(feedback.extension||'').replace(/^\./,'').toLowerCase();
  if(typeof feedback.item_id!=='string'||!feedback.item_id||!/^[a-z0-9]+$/.test(extension)||typeof feedback.background!=='boolean'||typeof feedback.library_root!=='string'||path.resolve(feedback.library_root).toLowerCase()!==path.resolve(libraryRoot).toLowerCase())return;
  const context={id,intent:id,generation:viewerGeneration,controller:new AbortController(),owner:null};
  const previous=[...owners.values()].find(o=>!o.closed&&o.completedResult&&o.request.item.id===feedback.item_id);
  if(previous){previous.failure=null;previous.context=context;context.owner=previous;intentOwner=previous;sendLoading(previous,'waiting-native');if(feedback.background)previous.window.showInactive();else{previous.window.show();previous.window.focus();}return;}
  for(const o of [...owners.values()])if(o.request.kind==='viewer')close(o,superseded());
  intentOwner=createOwner({kind:'viewer',source:'',extension,background:feedback.background,item:{id:feedback.item_id,name:''}},context,{id},true);
 }
 function failViewerIntent(id,error){const o=intentOwner;if(id!==latestViewerIntent||!o||o.closed)return;const reason=failure(error?.code||'preview_failed',error?.message||'Model preparation failed');o.loadingStage='error';o.failure=reason;if(!o.completedResult)o.controller.abort(reason);clearTimeout(o.timer);o.window.webContents.send('billfish:animation-event',{type:'error',error:{code:reason.code,message:reason.message}});o.prepareReject?.(reason);o.reject(reason);}

 async function render(message){
  let context;
  if(message?.kind==='viewer'){
   if(typeof message.id!=='string'||!message.id||message.id.length>256)throw failure('invalid_arguments','A preview request ID is required');
   const intent=message.viewer_intent||message.id;if(message.viewer_intent){if(latestViewerIntent===null)admitViewerIntent(intent);if(intent!==latestViewerIntent)throw superseded();}else admitViewerIntent(intent);
   for(const old of viewerRequests.values()){old.controller.abort(superseded());if(old.owner&&!old.owner.completedResult)close(old.owner,superseded());}
   context=intentOwner?.context?.intent===intent?intentOwner.context:{id:message.id,intent,generation:++viewerGeneration,controller:new AbortController(),owner:null};context.id=message.id;viewerRequests.set(context.id,context);
  }
  try{return await renderRequest(message,context);}finally{if(context&&viewerRequests.get(context.id)===context)viewerRequests.delete(context.id);}
 }
 function requestNative(type,payload){if(stopped)return Promise.reject(failure('plugin_disabled','Browser stopped'));const request_id=crypto.randomUUID();return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{pending.delete(request_id);reject(failure('native_timeout','Billfish did not confirm the operation'));},90000);pending.set(request_id,{resolve,reject,timer});try{send({type,request_id,...payload});}catch(error){clearTimeout(timer);pending.delete(request_id);reject(error);}});}
 function ownerForEvent(event){const o=owners.get(event.sender.id);return o&&!o.closed&&event.senderFrame===event.sender.mainFrame?o:null;}
 function decorate(o,value){if(!value||typeof value!=='object')return value;const out={...value};if(Array.isArray(value.rows))o.rows=value.rows;if(value.model&&value.model.renderer!=='core'){if(o.modelPath!==value.model.path){if(o.asset)assets.revoke(o.asset.token);o.asset=assets.register(value.model,libraryRoot);o.modelPath=value.model.path;}out.model={...value.model,url:o.asset.url,...(o.asset.materialsUrl?{materialsUrl:o.asset.materialsUrl}:{}),initialDisplayMode:o.request.kind==='thumbnail'?'material':'solid'};}if(value.state)out.state=decorate(o,value.state);return out;}
 function close(o,reason=failure('preview_window_closed','Animation browser closed')){if(o.closed)return;o.closed=true;clearTimeout(o.timer);o.prepareReject?.(reason);o.controller?.abort(reason);o.context?.controller.abort(reason);if(owners.get(o.contentsId)===o)owners.delete(o.contentsId);if(o.asset)assets.revoke(o.asset.token);if(o.sourceAsset)assets.revoke(o.sourceAsset.token);for(const token of o.workerAssets||[])assets.revoke(token);service.closeSession(o.sessionId).catch(console.error);o.reject(reason);if(!o.window.isDestroyed())o.window.destroy();}
 ipcMain.handle('billfish:animation',async(event,method,...args)=>{
  const o=ownerForEvent(event);try{if(!o||stopped)throw failure('permission_denied','Only the active animation browser main frame is authorized');
   let data;
   if(method==='initialize'){
    sendLoading(o);const initial=await o.initial;ensureOwner(o);const state=decorate(o,initial);data={...state,mode:o.request.kind,kind:o.request.kind,nativeVisibility:{...nativeVisibility},diagnostics:[...(state.diagnostics||[]),...startupDiagnostics,...(nativeVisibility.error?[nativeVisibility.error]:[])]};
    // The renderer subscribes before invoking initialize. Warm scans can finish
    // before its module loads, so replay those events after the initial snapshot
    // is taken and before resolving this handshake. The UI drains its event queue
    // after applying the snapshot, preserving the backend's actual order.
    o.initialized=true;for(const queued of o.initialEvents)o.window.webContents.send('billfish:animation-event',decorate(o,queued));o.initialEvents=[];
   }
   else if(method==='core.worker.source'){
    const source=args[0];if(o.renderer!=='core'||!source||!o.workerAssets?.has(source.token)||typeof source.code!=='string'||Buffer.byteLength(source.code)>16*1024*1024)throw failure('permission_denied','Only the current owned Core worker may receive its script');assets.completeWorker(source.token,source.code);data={accepted:true};
   }else if(method==='core.config'){
    if(o.renderer!=='core')throw failure('permission_denied','This native preview does not use Core');await o.initial;ensureOwner(o);o.sourceAsset||=assets.registerSource(o.request.source,libraryRoot);data={renderer:'core',source:o.request.source,sourceUrl:o.sourceAsset.url,extension:o.request.extension,library:path.join(directory,'vendor','core','lib'),resourceUrl:'billfish-animation://app/vendor/core/lib/'};
   }else if(method==='core.register'){
    if(o.renderer!=='core')throw failure('permission_denied','Only the current native Core viewport may register embedded animations');await o.initial;ensureOwner(o);const metadata=args[0];if(!metadata||!Array.isArray(metadata.clips)||!Number.isInteger(metadata.meshCount)||metadata.meshCount<0||!Number.isInteger(metadata.drawableCount)||metadata.drawableCount<0||(!(metadata.renderedFrame>0)&&metadata.canvasFrame!==true))throw failure('invalid_preview_response','Core did not prove a rendered source frame');
    const contentProved=(metadata.sourceKind==='geometry'&&metadata.drawableCount>0)||(metadata.sourceKind==='skeleton'&&o.request.extension==='bvh'&&metadata.boneCount>0&&metadata.skeletonHelpers>0)||(metadata.sourceKind==='environment'&&['hdr','exr'].includes(o.request.extension)&&metadata.environmentTexture===true)||(metadata.sourceKind==='canvas'&&['spline','splinecode'].includes(o.request.extension)&&metadata.canvasFrame===true);if(!contentProved)throw failure('invalid_preview_response','Core did not identify content from the loaded original source');
    if(metadata.canvasFrame===true){if(!ArrayBuffer.isView(metadata.png)||metadata.png.byteLength>64*1024*1024||metadata.drawableCount<1)throw failure('invalid_preview_response','Core did not return a bounded source canvas frame');const frame=Buffer.from(metadata.png.buffer,metadata.png.byteOffset,metadata.png.byteLength);if(!frame.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10]))||nativeImage.createFromBuffer(frame).isEmpty())throw failure('invalid_preview_response','Core canvas frame cannot be decoded');}
    const indices=new Set();o.rows=metadata.clips.map(clip=>{if(!Number.isInteger(clip.index)||clip.index<0||indices.has(clip.index)||typeof clip.name!=='string'||clip.name.length>1024||!Number.isFinite(clip.duration)||clip.duration<=0)throw failure('invalid_preview_response','Core returned invalid embedded animation metadata');indices.add(clip.index);return {id:`core:${o.request.item.id}:${o.sourceFingerprint.sha256}:${clip.index}`,name:clip.name,clipIndex:clip.index,duration:clip.duration,fps:null,source:o.request.source,sourceItemId:o.request.item.id,origin:'embedded',relativePath:path.relative(libraryRoot,o.request.source).replaceAll('\\','/'),folder:o.libraryContext.relativeFolder,format:o.request.extension,compatibility:'full',association:{enabled:true,confirmed:true,source:'embedded'},report:{compatibility:'full',source:'embedded',retargeting:false}};});o.coreProof={meshCount:metadata.meshCount,drawableCount:metadata.drawableCount,renderedFrame:metadata.renderedFrame,canvasFrame:metadata.canvasFrame===true};o.capabilities.displayFacing=metadata.displayFacing===true;o.capabilities.skeletonOverlay=metadata.hasSkeleton===true;o.currentClip=null;data={rows:o.rows,capabilities:o.capabilities};o.window.webContents.send('billfish:animation-event',{type:'rows',rows:o.rows});
   }else if(method==='core.selected'){
    const row=o.renderer==='core'&&(o.rows||[]).find(row=>row.id===args[0]?.rowId);if(!row)throw failure('invalid_arguments','Unknown native embedded animation');o.currentClip={rowId:row.id,source:o.request.source,clipIndex:row.clipIndex,nativeFingerprint:o.sourceFingerprint,row};data={accepted:true};
   }else if(method==='core.warning'){
    if(o.renderer!=='core')throw failure('permission_denied','This preview does not use Core');const warning=args[0];if(!warning||typeof warning.code!=='string'||typeof warning.message!=='string')throw failure('invalid_arguments','A Core diagnostic is required');o.window.webContents.send('billfish:animation-event',{type:'warning',warning:{code:warning.code.slice(0,128),message:warning.message.slice(0,4096)}});data={accepted:true};
   }else if(method==='ready'){
    await o.initial;ensureOwner(o);const payload=args[0];if(payload?.kind!==o.request.kind||(o.renderer==='core'?(!o.coreProof||(!(payload.renderedFrame>0)&&!o.coreProof.canvasFrame)||!Number.isInteger(payload.drawableCount)||payload.drawableCount<0):(!Number.isInteger(payload.meshCount)||payload.meshCount<1)))throw failure('invalid_preview_response','The native source has not produced a rendered model frame');
    if(o.renderer!=='core'&&(!['solid','material'].includes(payload.renderMode)||!['idle','loading','ready','error'].includes(payload.materialState)||(payload.renderMode==='material'&&payload.materialState!=='ready')||(o.request.kind==='thumbnail'&&(payload.renderMode!=='material'||payload.materialState!=='ready'))))throw failure('invalid_preview_response','The rendered frame must identify its actual display mode; thumbnails require fully loaded original materials');
    const png=payload.png;if(!ArrayBuffer.isView(png))throw failure('invalid_thumbnail_output','A rendered PNG is required');const bytes=Buffer.from(png.buffer,png.byteOffset,png.byteLength);if(bytes.length<33||bytes.length>64*1024*1024)throw failure('invalid_thumbnail_output','Invalid rendered PNG size');
    const decoded=nativeImage.createFromBuffer(bytes);if(decoded.isEmpty())throw failure('invalid_thumbnail_output','Rendered PNG cannot be decoded');
    let image;if(o.request.kind==='thumbnail'){await fs.mkdir(path.dirname(o.request.dest),{recursive:true});await fs.writeFile(o.request.dest,bytes,{flag:'wx'});image=inspectPng(o.request.dest);}
    await o.presentationReady;await new Promise(resolve=>setImmediate(resolve));ensureOwner(o);o.completedResult={plugin_id:manifest.id,item_id:o.request.item.id,path:o.request.source,extension:o.request.extension,completion:o.request.kind==='thumbnail'?'plugin_thumbnail_rendered':'plugin_viewer_rendered',...(o.renderer!=='core'?{renderMode:payload.renderMode,materialState:payload.materialState}:{}),...(image?{dest:o.request.dest,...image}:{window_id:o.window.id})};o.resolve(o.completedResult);data={accepted:true};
   }else if(method==='failed'){o.reject(failure('preview_failed',args[0]?.message||'Renderer failed'));data=null;}
   else {const initial=await o.initial;ensureOwner(o);
    if(method==='reveal'||method==='copyReport'){const row=(o.rows||[]).find(r=>r.id===args[0]);if(!row)throw failure('invalid_arguments','Unknown animation row');if(method==='reveal')shell.showItemInFolder(row.source);else clipboard.writeText(JSON.stringify(row.report,null,2));data={completed:true};}
    else if(method==='setCover'){const payload=args[0];if(o.renderer==='core'&&!o.capabilities.currentFrameCover)throw failure('unsupported_api','This format supports viewing only');if(payload?.pose?.kind==='clip'&&(!o.currentClip||payload.pose.rowId!==o.currentClip.rowId||payload.pose.clip_index!==o.currentClip.clipIndex))throw failure('invalid_arguments','Cover must use the current accepted animation');data=await publishCover(o,payload);}
    else if(method==='call'){
     const [operation,input={}]=args;if(o.request.kind==='thumbnail')throw failure('permission_denied','Thumbnail workers cannot change browser state');
     if(operation==='scope.set'||input.patch?.scope!==undefined)throw failure('permission_denied','Animation candidates are restricted to the bound Billfish library');
     if(['candidateScope.set','profile.update','profile.resolveConflict','association.update','scan.refresh','cache.command'].includes(operation)){o.currentClip=null;++o.clipGeneration;}
     const generation=operation==='clip.load'?++o.clipGeneration:o.clipGeneration;if(o.renderer==='core'){if(operation!=='cache.command'||input.command!=='regenerate-thumbnail'||!o.capabilities.cacheCommands.includes(input.command))throw failure('unsupported_api','This library operation is unavailable for this model format');data={requiresHostAction:'regenerate-thumbnail'};}else data=await service.call(o.sessionId,operation,input);
     if(['candidateScope.set','visibility.update','association.update','scan.refresh','profile.update','profile.resolveConflict'].includes(operation)){const visibilityResult=await visibilitySync;if(visibilityResult?.error)throw visibilityResult.error;}
     if(operation==='clip.load'){const fingerprint=await fingerprintFile(data.source,{signal:o.controller.signal});ensureOwner(o);if(fingerprint.sha256!==data.sourceFingerprint.sha256||fingerprint.bytes!==String(data.sourceFingerprint.size))throw failure('viewer_source_changed','Animation source changed while loading');if(generation===o.clipGeneration){const row=(o.rows||[]).find(r=>r.id===data.rowId);o.currentClip={rowId:data.rowId,source:data.source,sourceItemId:data.sourceItemId,clipIndex:data.clipIndex,sourceFingerprint:data.sourceFingerprint,row,nativeFingerprint:fingerprint};}const {clip,clipBuffer,...metadata}=data;data=data.reused?{...metadata,clip:null}:{...metadata,clipBinary:clipBuffer};}
     if(data?.requiresHostAction==='regenerate-thumbnail'){data=await requestNative('animation_thumbnail',{item:{id:o.request.item.id,path:o.request.source},source:o.sourceFingerprint});if(data?.completion!=='native_custom_cover_verified'||data.item_id!==o.request.item.id||data.source_unchanged!==true||data.metadata_unchanged!==true||data.pose?.kind!=='base'||data.pose.time!==0||typeof data.cover_path!=='string'||!data.cover_path||!(data.image?.width>0)||!(data.image?.height>0))throw failure('cover_not_confirmed','Billfish did not confirm a regenerated reference-pose thumbnail');}
     ensureOwner(o);data=decorate(o,data);
    }else throw failure('unsupported_api','Unknown animation browser operation');
   }
   return {data};
  }catch(error){return {error:{code:error.code||'animation_browser_error',message:error.message,details:error.details||{}}};}
 });
 async function renderRequest(message,context){
  ensureViewer(context);if(stopped)throw failure('plugin_disabled','Browser stopped');const request=validatePreviewRequest(message,{directory,manifest,stagingRoot});
  const relativeSource=path.relative(path.resolve(libraryRoot),request.source);if(relativeSource==='..'||relativeSource.startsWith('..'+path.sep)||path.isAbsolute(relativeSource))throw failure('permission_denied','The native model source is outside the bound library');const renderer=['fbx','glb'].includes(request.extension)?'three':'core';
  if(renderer==='core'&&!workerIpcReady){
   ipcMain.on('billfish:core-worker-reserve',event=>{const owner=ownerForEvent(event);if(!owner||owner.renderer!=='core'||!owner.sourceFingerprint||stopped){event.returnValue={error:{code:'permission_denied',message:'Only the active Core preload can reserve a resource worker'}};return;}owner.workerAssets||=new Set();if(owner.workerAssets.size>=64){event.returnValue={error:{code:'core_worker_limit',message:'The model exceeded its owned resource worker limit'}};return;}const registration=assets.reserveWorker();owner.workerAssets.add(registration.token);event.returnValue=registration;});
   ipcMain.on('billfish:core-workers-dispose',event=>{const owner=ownerForEvent(event);if(!owner||owner.renderer!=='core')return;for(const token of owner.workerAssets||[])assets.revoke(token);owner.workerAssets?.clear();});workerIpcReady=true;
  }
  if(request.kind==='viewer'){
   ensureViewer(context);
   const previous=[...owners.values()].find(o=>!o.closed&&o.request.kind==='viewer');
   if(previous?.completedResult&&previous.request.item.id===request.item.id&&path.resolve(previous.request.source).toLowerCase()===path.resolve(request.source).toLowerCase()){
    context.owner=previous;const actual=await fingerprintFile(request.source,{signal:context.controller.signal});ensureViewer(context);
    if(!previous.closed&&['sha256','bytes','mtime_ns'].every(k=>actual[k]===previous.sourceFingerprint[k])){
     await previous.presentationReady;await new Promise(resolve=>setImmediate(resolve));ensureViewer(context);if(previous.closed||previous.window.isDestroyed())throw failure('preview_window_closed','The model preview closed before it could be reused');
     previous.context=context;previous.renderRequestId=message.id;sendLoading(previous,'ready');if(request.background)previous.window.showInactive();else{previous.window.show();previous.window.focus();}return {...previous.completedResult};
    }
   }
   if(intentOwner&&!intentOwner.closed&&intentOwner.prepareResolve&&intentOwner.context===context){
    const o=intentOwner;if(o.request.item.id!==request.item.id||o.request.extension!==request.extension)throw failure('viewer_source_mismatch','Prepared model does not match the accepted native viewer intent');
    o.request=request;o.renderRequestId=message.id;o.libraryContext=libraryContext(request.source);o.window.setTitle?.(request.item.name+' - '+manifest.name);o.prepareResolve();o.prepareResolve=null;
    return await ownerResult(o);
   }
   for(const owner of [...owners.values()])if(owner.request.kind==='viewer'){if(owner.context===context)owner.context=null;close(owner);}
  }
  return await ownerResult(createOwner(request,context,message));
 }
 function libraryContext(source){return {libraryRoot,libraryName:path.basename(libraryRoot),relativeFolder:source?path.dirname(path.relative(libraryRoot,source)).replaceAll('\\','/').replace(/^\.$/,''):''};}
 function createOwner(request,context,message,waitingNative=false){
  const renderer=['fbx','glb'].includes(request.extension)?'three':'core';
  ensureViewer(context);const thumbnail=request.kind==='thumbnail',window=new BrowserWindow({show:false,width:thumbnail?800:1280,height:thumbnail?800:860,minWidth:thumbnail?undefined:900,minHeight:thumbnail?undefined:620,title:request.item.name+' - '+manifest.name,webPreferences:{preload:path.join(__dirname,'animation-browser-preload.cjs'),nodeIntegration:false,contextIsolation:true,sandbox:false,webviewTag:false,backgroundThrottling:false,offscreen:thumbnail,additionalArguments:renderer==='core'?['--billfish-core-viewport']:[]}});
  if(!thumbnail)window.setMenu(null);
  let resolve,reject;const result=new Promise((a,b)=>{resolve=a;reject=b;}),o={window,contentsId:window.webContents.id,request,renderer,builtin:true,sessionId:crypto.randomUUID(),resolve,reject,clipGeneration:0,initialized:false,initialEvents:[],closed:false,context,controller:context?.controller||new AbortController(),renderRequestId:message.id,loadingStage:'checking-source'};if(context)context.owner=o;o.libraryContext=libraryContext(request.source);o.loadingStage=waitingNative?'waiting-native':'checking-source';o.result=result;result.catch(()=>{});o.capabilities={coordinateProfiles:false,libraryAssociation:false,displayFacing:true,skeletonOverlay:false,visibility:false,currentFrameCover:!['hdr','exr'].includes(request.extension),cacheCommands:['hdr','exr'].includes(request.extension)?[]:['regenerate-thumbnail']};owners.set(o.contentsId,o);
  const domReady=thumbnail?Promise.resolve():new Promise((resolve,reject)=>{const closed=()=>reject(failure('preview_window_closed','Browser closed before DOM was ready'));window.once('closed',closed);window.webContents.once('dom-ready',()=>{window.removeListener('closed',closed);resolve();});});o.presentationReady=thumbnail?Promise.resolve():Promise.all([domReady,attachWindow(window,'viewer',request.background)]).then(async()=>{await new Promise(resolve=>setImmediate(resolve));ensureOwner(o,true);if(request.background)window.showInactive();else{window.show();window.focus();}});o.presentationReady.catch(reject);
  window.webContents.setWindowOpenHandler(()=>({action:'deny'}));window.webContents.on('will-navigate',(event,url)=>{if(url!==assets.entry)event.preventDefault();});window.webContents.on('will-attach-webview',event=>event.preventDefault());window.webContents.on('render-process-gone',(_e,d)=>reject(failure('preview_renderer_exited',d.reason)));window.webContents.on('did-fail-load',(_e,code,text)=>{if(code!==-3)reject(failure('preview_load_failed',text));});window.once('closed',()=>close(o));
  o.timer=setTimeout(()=>{const error=failure('preview_timeout','Animation browser did not render within 90 seconds');o.failure=error;o.loadingStage='error';o.controller.abort(error);o.window.webContents.send('billfish:animation-event',{type:'error',error:{code:error.code,message:error.message}});o.prepareReject?.(error);reject(error);},90000);
  const nativePrepared=waitingNative?new Promise((resolve,reject)=>{o.prepareResolve=resolve;o.prepareReject=reject;}):Promise.resolve();nativePrepared.catch(()=>{});
  o.initial=(async()=>{await o.presentationReady;ensureOwner(o);sendLoading(o);await nativePrepared;ensureOwner(o);const request=o.request;sendLoading(o,'checking-source');o.sourceFingerprint=await fingerprintFile(request.source,{signal:o.controller.signal});ensureOwner(o);sendLoading(o,'preparing-model');if(renderer==='core')return {sessionId:o.sessionId,source:request.source,previewMode:'model',libraryContext:o.libraryContext,model:{renderer:'core',path:request.source,format:request.extension},rows:[],scan:{state:'complete',completed:1,total:1},profile:{renderFacing:{yaw:0}},capabilities:o.capabilities,diagnostics:[]};const state=await service.openSession({sessionId:o.sessionId,source:request.source,itemId:request.item.id,libraryRoot,sourceFingerprint:o.sourceFingerprint,mode:request.kind,discover:!thumbnail},{signal:o.controller.signal});if(o.closed){await service.closeSession(o.sessionId);throw failure('preview_window_closed','Browser closed');}ensureOwner(o);return state;})();o.initial.catch(reject);
  window.loadURL(assets.entry).catch(reject);
  return o;
 }
 async function ownerResult(o){try{return await o.result;}catch(error){if(o.request.kind==='thumbnail'||['preview_superseded','preview_window_closed'].includes(error.code))close(o,error);else if(!o.closed){o.failure=error;o.loadingStage='error';if(!o.completedResult)o.controller.abort(error);o.window.webContents.send('billfish:animation-event',{type:'error',error:{code:error.code||'preview_failed',message:error.message}});}throw error;}finally{clearTimeout(o.timer);if(o.request.kind==='thumbnail')close(o);}}
 return {render,admitViewerIntent,failViewerIntent,cancelPreview,ownerForEvent,stagingRoot,receive(message){const p=pending.get(message.request_id);if(!p)return;pending.delete(message.request_id);clearTimeout(p.timer);if(message.error)p.reject(Object.assign(new Error(message.error.message),message.error));else p.resolve(message.data);},themeChanged(){},shutdown(){stopped=true;for(const context of viewerRequests.values())context.controller.abort();viewerRequests.clear();catalog.shutdown();for(const p of pending.values()){clearTimeout(p.timer);p.reject(failure('plugin_disabled','Browser stopped'));}pending.clear();for(const o of [...owners.values()])close(o);service.shutdown().catch(console.error);assets.shutdown();}};
}
module.exports={createAnimationBrowserRuntime};
