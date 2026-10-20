'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {fileURLToPath} = require('node:url');
const {fingerprintFile}=require('./preview-cover.cjs');

function failure(code,message,details) { return Object.assign(new Error(message),{code,details}); }
function within(root,target) {
  const relative = path.relative(path.resolve(root),path.resolve(target));
  return relative !== '..' && !relative.startsWith('..'+path.sep) && !path.isAbsolute(relative);
}
function entryPath(directory,value) {
  if (typeof value !== 'string' || !value || path.isAbsolute(value) || value.split(/[\\/]/).includes('..'))
    throw failure('invalid_preview_manifest','Preview hooks must use local package-relative paths');
  const resolved = path.resolve(directory,value);
  if (!within(directory,resolved) || !fs.statSync(resolved).isFile()) throw failure('invalid_preview_manifest','Preview hook does not exist');
  return resolved;
}
function resolvePreview(directory,manifest,extension,kind) {
  if (!['viewer','thumbnail'].includes(kind)) throw failure('invalid_arguments','Preview kind must be viewer or thumbnail');
  if (!/^[a-z0-9]+$/.test(extension)) throw failure('unsupported_preview_format','Invalid preview extension');
  const selected = manifest.billfish?.previewExtensions;
  if (selected !== undefined && (!Array.isArray(selected) || !selected.includes(extension)))
    throw failure('unsupported_preview_format','This extension is not enabled by the maintained package');
  let match;
  for (const [extensions,hooks] of Object.entries(manifest.preview || {})) {
    if (!extensions.split(',').map(value=>value.trim().toLowerCase()).includes(extension) || !hooks?.[kind]) continue;
    if (match) throw failure('ambiguous_preview_format','More than one hook declares this extension');
    match = hooks[kind];
  }
  if (!match || typeof match !== 'object') throw failure('unsupported_preview_format',`No ${kind} hook is declared for ${extension}`);
  return {entry:entryPath(directory,match.path),options:match};
}
function validatePreviewRequest(message,{directory,manifest,stagingRoot}) {
  if (!message || typeof message.id !== 'string' || !message.id || message.id.length > 256)
    throw failure('invalid_arguments','A preview request ID is required');
  if(message.background!==undefined && typeof message.background!=='boolean') throw failure('invalid_arguments','Preview background must be a boolean');
  if (!message.item || typeof message.item.id !== 'string' || !message.item.id || typeof message.item.path !== 'string' || !path.isAbsolute(message.item.path))
    throw failure('invalid_arguments','Preview requires a native Item ID and absolute original path');
  const source = path.resolve(message.item.path);
  if (!fs.statSync(source).isFile()) throw failure('preview_source_missing','The original model file does not exist');
  const extension = path.extname(source).slice(1).toLowerCase();
  const resolved = resolvePreview(directory,manifest,extension,message.kind);
  let dest;
  if (message.kind === 'thumbnail') {
    if (typeof message.dest !== 'string' || !path.isAbsolute(message.dest) || path.extname(message.dest).toLowerCase() !== '.png')
      throw failure('invalid_arguments','Thumbnail destination must be an absolute PNG staging path');
    dest = path.resolve(message.dest);
    if (!within(stagingRoot,dest) || path.resolve(stagingRoot) === dest || source === dest)
      throw failure('invalid_thumbnail_destination','Thumbnail destination is outside the session staging directory');
    if (fs.existsSync(dest)) throw failure('thumbnail_destination_exists','Thumbnail staging destination already exists');
  }
  return {...resolved,kind:message.kind,source,extension,dest,background:message.background===true,item:{id:message.item.id,name:message.item.name || path.basename(source)}};
}
function inspectPng(file) {
  const stat = fs.statSync(file);
  if (!stat.isFile() || stat.size < 33 || stat.size > 64*1024*1024) throw failure('invalid_thumbnail_output','Thumbnail output has an invalid byte length');
  const header=Buffer.alloc(24),fd=fs.openSync(file,'r');
  try { if (fs.readSync(fd,header,0,24,0)!==24) throw failure('invalid_thumbnail_output','Thumbnail PNG header is incomplete'); }
  finally { fs.closeSync(fd); }
  if (!header.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])) || header.toString('ascii',12,16)!=='IHDR')
    throw failure('invalid_thumbnail_output','Thumbnail output is not a PNG');
  const width=header.readUInt32BE(16),height=header.readUInt32BE(20);
  if (!width || !height || width>16384 || height>16384 || width*height>64*1024*1024)
    throw failure('invalid_thumbnail_output','Thumbnail PNG dimensions are invalid');
  return {width,height,bytes:stat.size};
}

function createPreviewRuntime({BrowserWindow,ipcMain,nativeImage,directory,manifest,stagingRoot,bootstrap,attachWindow=async()=>({attached:false,standalone:true})}) {
  const windows = new Map(), jobs = new Map();
  let stopping = false;
  const root = path.resolve(stagingRoot);
  function ownerForEvent(event) {
    const owner=windows.get(event.sender.id);
    return owner && !owner.window.isDestroyed() && event.senderFrame===event.sender.mainFrame ? owner : null;
  }
  function close(owner) { if (!owner.window.isDestroyed()) owner.window.destroy(); }
  ipcMain.on('billfish:preview-result',(event,message) => {
    const owner=ownerForEvent(event);
    if (!owner || !jobs.has(owner.requestId)) return;
    const job=jobs.get(owner.requestId);
    if (message?.error) job.reject(failure(message.error.code || 'preview_failed',message.error.message || 'Preview renderer failed',message.error.details));
    else job.resolve(message?.data || {});
  });
  function render(message) {
    if (stopping) return Promise.reject(failure('plugin_disabled','Plugin preview session is stopping'));
    let request;
    try { request=validatePreviewRequest(message,{directory,manifest,stagingRoot:root}); }
    catch(error) { return Promise.reject(error); }
    if (jobs.has(message.id)) return Promise.reject(failure('duplicate_request','Preview request ID is already active'));
    if (request.kind==='thumbnail') fs.mkdirSync(path.dirname(request.dest),{recursive:true});
    const thumbnail=request.kind==='thumbnail';
    const sourceReady=!thumbnail?fingerprintFile(request.source):Promise.resolve(null);
    const window=new BrowserWindow({show:false,width:thumbnail?1000:1000,height:thumbnail?1000:780,title:`${request.item.name} - ${manifest.name}`,
      webPreferences:{preload:path.join(__dirname,'preview-preload.cjs'),nodeIntegration:true,contextIsolation:false,sandbox:false,backgroundThrottling:false,offscreen:thumbnail}});
    const owner={window,requestId:message.id,request,bootstrap:{...bootstrap(),background:request.background,preview:{...request,engine:manifest.billfish?.previewEngine || null}}};
    owner.ownerReady=thumbnail?Promise.resolve():attachWindow(window,'viewer',request.background);
    // A normal hidden native window throttles frame/PNG callbacks even with
    // backgroundThrottling disabled. Show the original loading UI once its DOM
    // exists and Billfish has attached ownership, then keep the real render
    // barrier below. Thumbnail workers remain hidden.
    const domReady=new Promise(resolve=>window.webContents.once('dom-ready',resolve));
    owner.presentationReady=thumbnail?Promise.resolve():Promise.all([domReady,owner.ownerReady]).then(()=>{
      if(window.isDestroyed())throw failure('preview_window_closed','Preview window closed before it could be shown');
      if(request.background)window.showInactive();else{window.show();window.focus();}
    });
    owner.presentationReady.catch(error=>jobs.get(message.id)?.reject(error));
    const contentsId=window.webContents.id;
    windows.set(contentsId,owner);
    window.webContents.on('will-navigate',(event,url)=>{
      try { if (!url.startsWith('file:') || !within(directory,fileURLToPath(url))) event.preventDefault(); }
      catch { event.preventDefault(); }
    });
    window.webContents.setWindowOpenHandler(()=>({action:'deny'}));
    window.webContents.on('will-attach-webview',event=>event.preventDefault());
    const result=new Promise((resolve,reject)=>{
      const timeout=setTimeout(()=>reject(failure('preview_timeout',`${request.kind} did not finish rendering within 90 seconds`)),90000);
      jobs.set(message.id,{resolve,reject});
      window.webContents.on('render-process-gone',(_event,details)=>reject(failure('preview_renderer_exited',`Preview renderer exited: ${details.reason}`)));
      window.webContents.on('preload-error',(_event,_preload,error)=>reject(failure('preview_preload_failed',error.message)));
      window.webContents.on('did-fail-load',(_event,code,description)=>{if(code!==-3)reject(failure('preview_load_failed',description));});
      window.once('closed',()=>{
        windows.delete(contentsId);
        if(jobs.has(message.id)) reject(failure('preview_window_closed','Preview window closed before rendering completed'));
      });
      const page=thumbnail?path.join(__dirname,'preview-host.html'):request.entry;
      const query=thumbnail?{}:{id:request.item.id,theme:owner.bootstrap.app.isDark?'dark':'light',lang:owner.bootstrap.app.locale,path:request.source};
      sourceReady.then(source=>{owner.sourceFingerprint=source;return window.loadFile(page,{query});}).catch(reject);
      // Retain the timeout only until this request is settled, including output validation.
      owner.clearTimeout=()=>clearTimeout(timeout);
    });
    return result.then(async data=>{
      const common={plugin_id:manifest.id,item_id:request.item.id,path:request.source,extension:request.extension};
      if(thumbnail){
        const png=inspectPng(request.dest),decoded=nativeImage.createFromPath(request.dest);
        if(decoded.isEmpty()) throw failure('invalid_thumbnail_output','Thumbnail PNG could not be decoded');
        const actual=decoded.getSize();
        if(actual.width!==png.width || actual.height!==png.height) throw failure('invalid_thumbnail_output','Decoded thumbnail dimensions do not match its PNG header');
        return {...common,completion:'plugin_thumbnail_rendered',dest:request.dest,...png,item:data.item || {id:request.item.id}};
      }
      if (!['plugin_viewer_loaded','plugin_viewer_rendered'].includes(data.completion)) throw failure('invalid_preview_response','Viewer did not report its loading boundary');
      await owner.presentationReady;
      return {...common,completion:data.completion,window_id:window.id,...(Number.isInteger(data.animation_count)&&data.animation_count>=0?{animation_count:data.animation_count}:{}),...(Number.isInteger(data.resource_warning_count)&&data.resource_warning_count>=0?{resource_warning_count:data.resource_warning_count}:{})};
    }).catch(async error=>{
      if(!thumbnail && error.details?.viewer_error_visible && !window.isDestroyed()){
        try{await owner.presentationReady;}catch(attachError){close(owner);throw attachError;}
        error.details={...error.details,window_id:window.id};
      }else close(owner);
      throw error;
    }).finally(()=>{
      jobs.delete(message.id);owner.clearTimeout();
      if(thumbnail)close(owner);
    });
  }
  return {render,ownerForEvent,themeChanged(theme){for(const owner of windows.values())if(!owner.window.isDestroyed())owner.window.webContents.send('billfish:event','themeChanged',theme);},
    shutdown(){stopping=true;for(const job of jobs.values())job.reject(failure('plugin_disabled','Plugin session stopped'));for(const owner of windows.values())close(owner);windows.clear();},
    stagingRoot:root};
}
module.exports={createPreviewRuntime,resolvePreview,validatePreviewRequest,inspectPng,within};
