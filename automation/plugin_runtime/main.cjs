'use strict';
const {app, BrowserWindow, ipcMain, nativeTheme, screen, shell, dialog, Menu, Notification, nativeImage, protocol, clipboard} = require('electron');
const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const net = require('node:net');
const {pathToFileURL, fileURLToPath} = require('node:url');
const {validateSchema} = require('./schema.cjs');
const {createPreviewRuntime} = require('./preview-runtime.cjs');
const {createWindowOwnerBridge} = require('./window-owner.cjs');
const {createPreviewCoverBridge}=require('./preview-cover.cjs');
const MAX_MESSAGE = 4 * 1024 * 1024;
const directory = path.resolve(process.argv[2]);
const builtin=process.env.BILLFISH_ANIMATION_BROWSER_BUILTIN==='1';
const rawManifest = JSON.parse(builtin?process.env.BILLFISH_BUILTIN_DESCRIPTOR:fs.readFileSync(path.join(directory,'manifest.json'),'utf8'));
if(builtin){
  const installRoot=process.env.BILLFISH_INSTALL_ROOT;
  if(!installRoot||path.resolve(directory).toLowerCase()!==path.resolve(installRoot,'automation','animation_browser').toLowerCase()||rawManifest.id!=='billfish.animation-browser'||rawManifest.billfish?.previewEngine!=='billfish-animation-browser')throw new Error('Invalid built-in animation browser identity');
  protocol.registerSchemesAsPrivileged([{scheme:'billfish-animation',privileges:{standard:true,secure:true,supportFetchAPI:true,corsEnabled:true}}]);
}
const dependencyRoots = JSON.parse(process.env.BILLFISH_PLUGIN_DEPENDENCIES || '{}');
const libraryRoot = process.env.BILLFISH_PLUGIN_LIBRARY_ROOT || '';
const permissions = new Set(rawManifest.billfish?.permissions || ['library.info','items.search','items.get','items.selected','folders.list','tags.list','items.update','tags.create','tags.assign','items.import_path','items.import_url','items.replace_file']);
const pending = new Map();
let window, sequence = 0, stopping = false, active = null, rendererReadyResolve = null, closingWindow = false, allowClose = false;
let referer = null, requestSession = null;
let previews = null, windowBackground = false;
let previewCovers=null;
let mainOwnerReady=Promise.resolve();
const windowOwners=createWindowOwnerBridge({required:process.env.BILLFISH_PLUGIN_WINDOW_OWNER_REQUIRED==='1',send});
const pipeName = process.argv[3];
if (typeof pipeName !== 'string' || !/^BillfishPlugin-[a-f0-9-]+$/.test(pipeName)) throw new Error('A private Billfish plugin pipe is required');
const connection = net.createConnection('\\\\.\\pipe\\' + pipeName);
connection.on('error',error => { console.error('Plugin IPC connection failed:',error.message); app.exit(1); });
function send(message) {
  const line = JSON.stringify(message);
  if (Buffer.byteLength(line) > MAX_MESSAGE) throw new Error('Plugin result exceeds 4 MiB');
  connection.write(line + '\n');
}
for (const name of ['log','info','warn','debug','error']) console[name] = (...args) => process.stderr.write(require('node:util').format(...args) + '\n');
function fail(error) { return {error:{code:error.code || 'plugin_error',message:String(error.message).slice(0,4096),details:error.details || {}}}; }
function eventWindow(event) {
  if (window && !window.isDestroyed() && event.sender === window.webContents && event.senderFrame === window.webContents.mainFrame) return window;
  return previews?.ownerForEvent(event)?.window || null;
}
function trusted(event) { return !!eventWindow(event); }
function sourceFile(value) {
  if (typeof value !== 'string' || !value.startsWith('file:')) return false;
  try { const relative = path.relative(directory,fileURLToPath(value)); return relative !== '..' && !relative.startsWith('..'+path.sep) && !path.isAbsolute(relative); }
  catch { return false; }
}
function lifecycle(name,payload) { if (window && !window.isDestroyed()) window.webContents.send('billfish:event',name,payload); }
function hostCall(operation,args) {
  if (stopping) return Promise.reject(Object.assign(new Error('Plugin session is stopping'),{code:'plugin_disabled'}));
  if (!permissions.has(operation)) return Promise.reject(Object.assign(new Error(`Host operation is not permitted: ${operation}`),{code:'permission_denied'}));
  const call_id = String(++sequence);
  return new Promise((resolve,reject) => {
    pending.set(call_id,{resolve,reject});
    try { send({type:'host_call',call_id,operation,arguments:args}); }
    catch (error) { pending.delete(call_id); reject(Object.assign(error,{code:'request_too_large'})); }
  });
}
function stop() {
  if (stopping) return;
  stopping = true;
  previews?.shutdown();
  previewCovers?.shutdown();
  windowOwners.shutdown();
  for (const value of pending.values()) value.reject(Object.assign(new Error('Plugin disabled'),{code:'plugin_disabled'}));
  pending.clear();
  if (window && !window.isDestroyed()) window.webContents.send('billfish:shutdown'); else app.quit();
  setTimeout(() => app.exit(0),1800).unref();
}
if (process.env.BILLFISH_PLUGIN_PROFILE) {
  fs.mkdirSync(process.env.BILLFISH_PLUGIN_PROFILE,{recursive:true});
  app.setPath('userData',process.env.BILLFISH_PLUGIN_PROFILE);
}
app.on('window-all-closed',() => { /* Enabled session can reopen its window on next Run. */ });
app.on('before-quit',() => { stopping = true; });
app.whenReady().then(async () => {
  if(builtin){
    if(!libraryRoot||!path.isAbsolute(libraryRoot))throw new Error('A fixed native library root is required');
    const stagingRoot=process.env.BILLFISH_PLUGIN_PREVIEW_ROOT||path.join(app.getPath('userData'),'preview-staging');
    previewCovers=createPreviewCoverBridge({send,stagingRoot,nativeImage,inspectPng:require('./preview-runtime.cjs').inspectPng});
    previews=require('./animation-browser-runtime.cjs').createAnimationBrowserRuntime({app,BrowserWindow,ipcMain,nativeImage,protocol,dialog,shell,clipboard,directory,manifest:rawManifest,libraryRoot,stagingRoot,attachWindow:(target,kind,background)=>windowOwners.attach(target,kind,background),send,publishCover:(owner,payload)=>previewCovers.publish(owner,payload)});
    const input=readline.createInterface({input:connection,crlfDelay:Infinity});
    input.on('line',async line=>{try{
      if(Buffer.byteLength(line)>MAX_MESSAGE)throw new Error('Host protocol message exceeds 4 MiB');const message=JSON.parse(line);
      if(message.type==='window_owner_result')windowOwners.receive(message);
      else if(message.type==='preview_cover_result')previewCovers.receive(message);
      else if(['animation_visibility_result','animation_thumbnail_result'].includes(message.type))previews.receive(message);
      else if(message.type==='preview_intent')previews.admitViewerIntent(message.id,message.feedback);
      else if(message.type==='preview_intent_error')previews.failViewerIntent(message.id,message);
      else if(message.type==='preview_cancel')previews.cancelPreview(message.id);
      else if(message.type==='shutdown')stop();
      else if(message.type==='preview'){try{send({type:'result',id:message.id,data:await previews.render(message)});}catch(error){send({type:'error',id:message.id,...fail(error).error});}}
      else if(message.type==='run')send({type:'error',id:message.id,code:'source_required',message:'Open a native mesh item to use the animation browser'});
      else throw new Error('Unexpected built-in host message');
    }catch(error){send({type:'startup_error',message:error.message});stop();}});
    input.on('close',stop);send({type:'ready',capabilities:{preview:true,previewProtocol:1}});return;
  }
  const main = rawManifest.main;
  if (!main || typeof main.url !== 'string' || path.isAbsolute(main.url) || main.url.split(/[\\/]/).includes('..')) throw new Error('A local main.url entry is required');
  for (const command of rawManifest.billfish?.commands || []) validateSchema(command.input_schema);
  // Keep localization within the plugin, without modifying the original package.
  let manifest = structuredClone(rawManifest);
  if (/^\{\{.+\}\}$/.test(manifest.name)) {
    const key = manifest.name.slice(2,-2).split('.');
    for (const locale of ['zh_CN',manifest.fallbackLanguage || 'en']) {
      try { let value = JSON.parse(fs.readFileSync(path.join(directory,'_locales',locale+'.json'),'utf8')); for (const part of key) value = value[part]; if (typeof value === 'string') { manifest.name = value; break; } } catch { /* Fall back to the declared name. */ }
    }
  }
  const plugin = {path:directory,manifest};
  function bootstrap() {
    return {plugin,libraryRoot,background:windowBackground,dependencies:dependencyRoots,app:{name:'Billfish',version:process.env.BILLFISH_APP_VERSION || `Billfish plugin runtime ${require('./package.json').version}`,
      build:process.env.BILLFISH_APP_BUILD || `electron-${process.versions.electron}`,theme:nativeTheme.shouldUseDarkColors?'DARK':'LIGHT',isDark:nativeTheme.shouldUseDarkColors,
      platform:process.platform,arch:process.arch,isWindows:process.platform==='win32',isMac:process.platform==='darwin',runningUnderARM64Translation:!!app.runningUnderARM64Translation,locale:'zh_CN',userDataPath:app.getPath('userData')}};
  }
  previews=createPreviewRuntime({app,BrowserWindow,ipcMain,nativeImage,directory,manifest,
    stagingRoot:process.env.BILLFISH_PLUGIN_PREVIEW_ROOT || path.join(app.getPath('userData'),'preview-staging'),bootstrap,attachWindow:(target,kind,background)=>windowOwners.attach(target,kind,background)});
  previewCovers=createPreviewCoverBridge({send,stagingRoot:previews.stagingRoot,nativeImage,inspectPng:require('./preview-runtime.cjs').inspectPng});
  ipcMain.handle('billfish:preview-cover',async(event,payload)=>{
    const owner=previews.ownerForEvent(event);
    if(!owner||stopping)return fail(Object.assign(new Error('Only the current model viewer can set its cover'),{code:'permission_denied'}));
    try{return{data:await previewCovers.publish(owner,payload)};}catch(error){return fail(error);}
  });
  const settings = {width:main.width || 640,height:main.height || 480,show:false,title:manifest.name,
    webPreferences:{preload:path.join(__dirname,'preload.cjs'),nodeIntegration:true,contextIsolation:false,sandbox:false,webviewTag:true,backgroundThrottling:false}};
  for (const key of ['resizable','minWidth','minHeight','maxWidth','maxHeight','frame','alwaysOnTop','opacity','transparent']) if (main[key] !== undefined) settings[key] = main[key];
  function createWindow() {
    closingWindow = false;
    referer = null;
    window = new BrowserWindow(settings);
    mainOwnerReady=windowOwners.attach(window,'main',windowBackground);
    if (requestSession !== window.webContents.session) {
      requestSession = window.webContents.session;
      requestSession.webRequest.onBeforeSendHeaders({urls:['http://*/*','https://*/*']},(details,callback) => {
        const headers = {...details.requestHeaders};
        if (referer) {
          for (const key of Object.keys(headers)) if (key.toLowerCase() === 'referer') delete headers[key];
          headers.Referer = referer;
        }
        callback({requestHeaders:headers});
      });
    }
    window.webContents.on('will-navigate',(event,url) => { if (!sourceFile(url)) event.preventDefault(); });
    window.webContents.setWindowOpenHandler(() => ({action:'deny'}));
    window.webContents.on('will-attach-webview',(event,webPreferences,params) => {
      // The plugin's own preload retains Node support; remote pages receive neither Node nor Billfish's preload/SDK.
      const preload = webPreferences.preload || params.preload;
      if (preload && !sourceFile(preload.startsWith('file:') ? preload : pathToFileURL(preload).href)) { event.preventDefault(); return; }
      webPreferences.nodeIntegration = false;
      webPreferences.contextIsolation = true;
      webPreferences.sandbox = false;
      webPreferences.webviewTag = false;
      delete webPreferences.preloadURL;
    });
    window.webContents.on('did-fail-load',(_event,code,message) => { if (code !== -3) send({type:'startup_error',message:`HTML load failed: ${message}`}); });
    window.webContents.on('render-process-gone',(_event,details) => { send({type:'startup_error',message:`Plugin renderer exited: ${details.reason}`}); app.exit(1); });
    window.on('show',() => lifecycle('show'));
    window.on('hide',() => lifecycle('hide'));
    window.on('close',event => {
      if (stopping) return;
      if (allowClose) return;
      event.preventDefault();
      if (main.serviceMode) { window.hide(); return; }
      if (!closingWindow) { closingWindow = true; window.webContents.send('billfish:shutdown'); }
    });
    window.on('closed',() => {
      if (active) { send({type:'error',id:active,code:'plugin_window_closed',message:'Plugin window closed before its run callback completed'}); active = null; }
      window = null; closingWindow = false;
    });
    const ready=new Promise((resolve,reject) => {
      rendererReadyResolve = resolve;
      window.loadFile(path.join(directory,main.url)).catch(reject);
    });
    return Promise.all([ready,mainOwnerReady]);
  }
  ipcMain.on('billfish:bootstrap',event => {
    if (!trusted(event)) return;
    event.returnValue = {...(previews.ownerForEvent(event)?.bootstrap || bootstrap()),windowOwnerRequired:windowOwners.required,windowOwnerAttached:windowOwners.isAttached(eventWindow(event))};
  });
  ipcMain.handle('billfish:host-call',async (event,operation,args) => {
    if (!trusted(event)) return fail(Object.assign(new Error('Only the plugin main frame can call Billfish'),{code:'permission_denied'}));
    try { return {data:await hostCall(operation,args)}; } catch (error) { return fail(error); }
  });
  ipcMain.handle('billfish:local-call',async (event,operation,args) => {
    if (!trusted(event) || stopping) return fail(Object.assign(new Error('Plugin session unavailable'),{code:'plugin_disabled'}));
    try {
      const [group,method] = operation.split('.');
      let data;
      const targetWindow=eventWindow(event);
      const windows = new Set(['show','hide','close','focus','minimize','maximize','unmaximize','getPosition','setPosition','getSize','setSize','setOpacity','getOpacity','setAlwaysOnTop','isAlwaysOnTop','isVisible','isMinimized','setTitle']);
      const background=previews.ownerForEvent(event)?.request.background ?? windowBackground;
      if(group==='window' && ['show','focus','maximize'].includes(method))await(previews.ownerForEvent(event)?.ownerReady || mainOwnerReady);
      if (operation==='window.focus' && background) data=null;
      else if (operation==='window.show' && background) data=targetWindow.showInactive();
      else if (group === 'window' && windows.has(method)) data = targetWindow[method](...args);
      else if (operation === 'screen.getCursorScreenPoint') data = screen.getCursorScreenPoint();
      else if (group === 'shell' && ['openExternal','openPath','showItemInFolder'].includes(method)) data = await shell[method](...args);
      else if (operation === 'dialog.showErrorBox') {
        if(typeof args[0]!=='string'||typeof args[1]!=='string')throw Object.assign(new Error('Error title and message must be strings'),{code:'invalid_arguments'});
        if(background)event.sender.send('billfish:background-error',{title:args[0],message:args[1]});
        else data=dialog.showErrorBox(...args);
      }
      else if (group === 'dialog' && ['showMessageBox','showOpenDialog','showSaveDialog'].includes(method)) data = await dialog[method](targetWindow,...args);
      else if (operation === 'notification.show') {
        if (!Notification.isSupported()) throw Object.assign(new Error('Native notifications are unavailable'),{code:'notification_unavailable'});
        const options=args[0];
        if (!options || typeof options.title!=='string' || typeof options.body!=='string') throw Object.assign(new Error('Notification title and body are required'),{code:'invalid_arguments'});
        const notification=new Notification({title:options.title,body:options.body});notification.show();
        if (Number.isFinite(options.duration) && options.duration>0) setTimeout(()=>notification.close(),options.duration).unref();
      }
      else if (operation === 'app.getPath') {
        const names = new Set(['home','appData','userData','temp','exe','desktop','documents','downloads','music','pictures','videos','recent']);
        if (!names.has(args[0])) throw Object.assign(new Error('Unsupported app.getPath name'),{code:'invalid_arguments'});
        data = app.getPath(args[0]);
      }
      else throw Object.assign(new Error(`Unsupported local API ${operation}`),{code:'unsupported_api'});
      return {data:data === undefined ? null : data};
    } catch (error) { return fail(error); }
  });
  ipcMain.on('billfish:local-sync',(event,operation,args) => {
    if (!trusted(event) || stopping) { event.returnValue = fail(Object.assign(new Error('Plugin session unavailable'),{code:'plugin_disabled'})); return; }
    try {
      if (operation === 'log.write') {
        const [level,message] = args;
        if (!['debug','info','warn','error'].includes(level) || typeof message !== 'string') throw Object.assign(new Error('Invalid log entry'),{code:'invalid_arguments'});
        const logs = path.join(app.getPath('userData'),'logs'); fs.mkdirSync(logs,{recursive:true});
        fs.appendFileSync(path.join(logs,'plugin.log'),`${new Date().toISOString()} [${level}] [${rawManifest.id}] ${message}\n`,'utf8');
      } else if (operation === 'window.setReferer') {
        if (typeof args[0] !== 'string') throw Object.assign(new Error('Referer must be an HTTP or HTTPS URL string'),{code:'invalid_arguments'});
        const parsed = new URL(args[0]);
        if (!['http:','https:'].includes(parsed.protocol)) throw Object.assign(new Error('Referer must use HTTP or HTTPS'),{code:'invalid_arguments'});
        referer = parsed.href;
      } else if (operation === 'contextMenu.open') {
        const [menuId,template] = args, owner = event.sender;
        function build(items) {
          return items.map(item => {
            const value = {...item}; delete value.callbackId;
            if (item.submenu) value.submenu = build(item.submenu);
            if (item.callbackId) value.click = () => { if (!owner.isDestroyed()) owner.send('billfish:context-menu-click',{menuId,callbackId:item.callbackId}); };
            return value;
          });
        }
        const menu = Menu.buildFromTemplate(build(template));
        menu.popup({window:eventWindow(event),callback:() => { if (!owner.isDestroyed()) owner.send('billfish:context-menu-close',menuId); }});
      } else throw Object.assign(new Error(`Unsupported synchronous API ${operation}`),{code:'unsupported_api'});
      event.returnValue = {data:null};
    } catch(error) { event.returnValue = fail(error); }
  });
  ipcMain.on('billfish:ready',event => { if (trusted(event) && rendererReadyResolve) { const resolve = rendererReadyResolve; rendererReadyResolve = null; resolve(); } });
  ipcMain.on('billfish:startup-error',(event,error) => { if (trusted(event)) send({type:'startup_error',message:error.message}); });
  ipcMain.on('billfish:session-error',(event,error) => { if (trusted(event)) console.error(error); });
  ipcMain.on('billfish:progress',(event,message) => { if (trusted(event) && active && message.id === active) send({type:'progress',...message}); });
  ipcMain.on('billfish:result',(event,message) => {
    if (!trusted(event) || message.id !== active) return;
    active = null;
    try { send(message); }
    catch (error) { send({type:'error',id:message.id,code:'response_too_large',message:error.message}); }
  });
  ipcMain.on('billfish:shutdown-ready',event => {
    if (!trusted(event)) return;
    if (stopping) app.exit(0);
    else if (closingWindow) {
      // A normal close still passes through the plugin page's onbeforeunload.
      allowClose = true; window.close(); allowClose = false; closingWindow = false;
    }
  });
  nativeTheme.on('updated',() => {const theme=nativeTheme.shouldUseDarkColors?'DARK':'LIGHT';lifecycle('themeChanged',theme);previews.themeChanged(theme);});
  const input = readline.createInterface({input:connection,crlfDelay:Infinity});
  input.on('line',async line => {
    try {
      if (Buffer.byteLength(line) > MAX_MESSAGE) throw new Error('Host protocol message exceeds 4 MiB');
      const message = JSON.parse(line);
      if (message.type === 'host_result') {
        const value = pending.get(message.call_id); if (!value) return; pending.delete(message.call_id);
        if (message.error) value.reject(Object.assign(new Error(message.error.message),{code:message.error.code,details:message.error.details || {}})); else value.resolve(message.data);
      } else if(message.type==='preview_cover_result')previewCovers.receive(message);
      else if(message.type==='window_owner_result')windowOwners.receive(message);
      else if (message.type === 'shutdown') stop();
      else if (message.type === 'preview') {
        try { send({type:'result',id:message.id,data:await previews.render(message)}); }
        catch(error) { const response=fail(error);send({type:'error',id:message.id,...response.error}); }
      }
      else if (message.type === 'run') {
        if (stopping || active) throw new Error('Plugin session is busy or stopping');
        if(message.background!==undefined && typeof message.background!=='boolean')throw new Error('Run background must be a boolean');
        windowBackground=message.background===true;
        active = message.id;
        if (!window || window.isDestroyed()) await createWindow();
        else {mainOwnerReady=windowOwners.attach(window,'main',windowBackground);await mainOwnerReady;}
        if (!rawManifest.billfish?.commands && (!main.serviceMode || message.command === 'open')) { if(windowBackground)window.showInactive();else{window.show();window.focus();} }
        window.webContents.send('billfish:run',message);
      } else throw new Error('Unexpected host message');
    } catch (error) { send({type:'startup_error',message:error.message}); stop(); }
  });
  input.on('close',stop);
  if (main.serviceMode || rawManifest.billfish?.commands) await createWindow();
  send({type:'ready',capabilities:{preview:true,previewProtocol:1}});
}).catch(error => { send({type:'startup_error',message:error.message}); app.exit(1); });
