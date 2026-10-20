'use strict';
const {ipcRenderer, clipboard, nativeImage} = require('electron');
const fs = require('node:fs');
const path = require('node:path');
const {createEagle} = require('./eagle-sdk.cjs');
const {validate, validateSchema} = require('./schema.cjs');
const {createExtraModules} = require('./extra-modules.cjs');
const {createContextMenuApi} = require('./context-menu.cjs');
const {format} = require('node:util');
const callbacks = new Map();
let requestId = null;
const bootstrap = ipcRenderer.sendSync('billfish:bootstrap');
let backgroundWindow = bootstrap.background === true;
let previewCreating = bootstrap.preview?.kind === 'viewer';
let windowOwned = !bootstrap.windowOwnerRequired || bootstrap.windowOwnerAttached === true;
ipcRenderer.on('billfish:window-owned',()=>{windowOwned=true;});
ipcRenderer.on('billfish:window-owner-pending',()=>{windowOwned=false;});
const focusWindow = window.focus.bind(window);
window.focus = (...args) => { if (!backgroundWindow && windowOwned && !previewCreating) return focusWindow(...args); };
// Eagle documents i18next as a host-provided global. Use the public standalone
// upstream library with this plugin's own locale files, before page scripts run.
const i18next = require('./vendor/i18next.cjs').createInstance();
const manifest = bootstrap.plugin.manifest;
const fallbackLanguage = manifest.fallbackLanguage || 'en';
const resources = {};
for (const locale of new Set([...(manifest.languages || []), bootstrap.app.locale, fallbackLanguage])) {
  if (typeof locale !== 'string' || !/^[A-Za-z_-]+$/.test(locale)) continue;
  const file = path.join(bootstrap.plugin.path, '_locales', locale + '.json');
  if (fs.existsSync(file)) resources[locale] = {translation:JSON.parse(fs.readFileSync(file, 'utf8'))};
}
i18next.init({lng:bootstrap.app.locale, fallbackLng:fallbackLanguage, resources, initAsync:false,
  interpolation:{escapeValue:false}});
window.i18next = i18next;
window.nativeImage = nativeImage;
function register(name, callback) {
  if (typeof callback !== 'function') throw new TypeError('Plugin callback must be a function');
  if (!callbacks.has(name)) callbacks.set(name, []);
  callbacks.get(name).push(callback);
}
async function dispatch(event, payload) {
  const values = [];
  for (const callback of callbacks.get(event) || []) values.push(await callback(payload));
  return values;
}
async function invoke(channel, ...args) {
  const response = await ipcRenderer.invoke(channel, ...args);
  if (response && response.error) {
    const error = new Error(response.error.message); error.code = response.error.code; error.details = response.error.details || {};
    throw error;
  }
  return response.data;
}
function invokeSync(operation,args) {
  const response = ipcRenderer.sendSync('billfish:local-sync',operation,args);
  if (response.error) { const error = new Error(response.error.message); error.code = response.error.code; error.details = response.error.details || {}; throw error; }
  return response.data;
}
const contextMenuApi = createContextMenuApi({
  openNative:(menuId,template) => invokeSync('contextMenu.open',[menuId,template]),
  onClick:handler => ipcRenderer.on('billfish:context-menu-click',(_event,message) => handler(message)),
  onClose:handler => ipcRenderer.on('billfish:context-menu-close',(_event,menuId) => handler(menuId)),
  reportError:error => ipcRenderer.send('billfish:session-error',{code:error.code || 'plugin_error',message:error.message})
});
const sdk = createEagle({
  call:(operation, args = {}) => invoke('billfish:host-call', operation, args),
  local:(operation, args) => previewCreating && operation === 'window.focus' ? Promise.resolve(null) : invoke('billfish:local-call', operation, args),
  localSync:invokeSync,
  plugin:bootstrap.plugin,
  app:bootstrap.app,
  libraryRoot:bootstrap.libraryRoot,
  extraModules:createExtraModules(manifest.dependencies,bootstrap.dependencies || {}),
  logApi:Object.fromEntries(['debug','info','warn','error'].map(level => [level,(...values) => { invokeSync('log.write',[level,format(...values)]); }])),
  contextMenuApi,
  clipboardApi:Object.fromEntries(['readText','writeText','readImage','writeImage','readBuffer','writeBuffer','readHTML','writeHTML','has','clear'].map(name => [name,(...args) => clipboard[name](...args)])),
  register,
  progress:(completed, total, message = '') => {
    if (!requestId) throw new Error('Progress is available only inside a Billfish command');
    if (!Number.isInteger(completed) || !Number.isInteger(total) || total < 0 || completed < 0 || completed > total) throw new Error('progress requires 0 <= completed <= total');
    ipcRenderer.send('billfish:progress', {id:requestId, completed,total,message:String(message).slice(0,4096)});
  }
});
window.eagle = sdk.eagle;
window.billfish = sdk.billfish;

if(!bootstrap.preview && bootstrap.plugin.manifest.id==='89fb801f-7761-4de6-b7ce-3785a084edaf' && bootstrap.plugin.manifest.billfish?.previewEngine==='eagle-3d-core'){
  window.addEventListener('load',()=>{
    const appRoot=document.getElementById('svelte');if(!appRoot)return;
    // Keep this local feature notice outside the vendor-owned hydrated tree.
    document.body.style.cssText+=';display:flex;flex-direction:column;height:100vh';
    appRoot.style.cssText+=';flex:1;min-height:0;height:0;width:100%';
    const notice=document.createElement('aside');notice.id='billfish-local-animation-notice';notice.setAttribute('role','status');
    notice.style.cssText='flex:0 0 auto;padding:8px 12px;border-bottom:1px solid #637a9e;background:#303c50;color:#edf3ff;font:12px/1.6 "Microsoft YaHei",sans-serif;-webkit-app-region:no-drag';
    const title=document.createElement('strong');title.textContent='动画控制已由 Billfish 提供';
    const text=document.createElement('p');text.textContent='打开带动画的模型即可使用播放、动画切换和当前帧封面.';text.style.cssText='margin:2px 0 0;white-space:normal';
    notice.append(title,text);appRoot.before(notice);
  },{once:true});
}

ipcRenderer.on('billfish:background-error',(_event,message) => {
  const display=()=>{
    // The original 3D viewer already exposes its loading failure in its page.
    if (bootstrap.preview && document.querySelector('.error-output > p')?.textContent.trim()) return;
    document.getElementById('billfish-background-error')?.remove();
    const panel=document.createElement('section');panel.id='billfish-background-error';panel.setAttribute('role','alertdialog');panel.setAttribute('aria-modal','false');panel.setAttribute('aria-label',message.title);
    panel.style.cssText='position:fixed;z-index:2147483647;inset:24px;max-height:calc(100vh - 48px);overflow:auto;padding:20px;background:#242528;color:#f8f9fb;border:1px solid #777;border-radius:8px;font:14px/1.5 sans-serif;user-select:text';
    const title=document.createElement('h2');title.textContent=message.title;title.style.cssText='font-size:18px;margin:0 0 12px';
    const text=document.createElement('pre');text.textContent=message.message;text.style.cssText='white-space:pre-wrap;word-break:break-word';
    const close=document.createElement('button');close.type='button';close.textContent='关闭';close.style.cssText='padding:6px 18px;border:1px solid #aaa;border-radius:4px;cursor:pointer';close.addEventListener('click',()=>panel.remove());
    panel.append(title,text,close);document.body.appendChild(panel);
  };
  if(document.body)display();else window.addEventListener('DOMContentLoaded',display,{once:true});
});

ipcRenderer.on('billfish:event', async (_event, event, payload) => {
  if (event === 'themeChanged') sdk.updateTheme(payload);
  try { await dispatch(event, payload); }
  catch (error) { ipcRenderer.send('billfish:session-error', {code:error.code || 'plugin_error',message:error.message}); }
});
ipcRenderer.on('billfish:run', async (_event, message) => {
  backgroundWindow = message.background === true;
  requestId = message.id;
  try {
    const custom = (bootstrap.plugin.manifest.billfish || {}).commands;
    if (custom) {
      const declaration = custom.find(command => command.id === message.command);
      if (!declaration) throw new Error('Unknown command');
      validateSchema(declaration.input_schema); validate(message.arguments, declaration.input_schema);
      const handlers = callbacks.get('command:' + message.command) || [];
      if (handlers.length !== 1) throw new Error('Declare exactly one billfish.registerCommand handler per command');
      const data = await handlers[0](message.arguments);
      if (data !== undefined && (data === null || typeof data !== 'object' || Array.isArray(data))) throw new Error('Billfish command must return an object or undefined');
      ipcRenderer.send('billfish:result', {type:'result',id:message.id,data:data || {}});
    } else {
      // Eagle Run remains a lifecycle event; it does not describe the lifetime of UI work.
      await dispatch('run', {args:message.arguments || {}});
      ipcRenderer.send('billfish:result', {type:'result',id:message.id,data:{opened:true,
        completion:bootstrap.plugin.manifest.main.serviceMode && message.command !== 'open' ? 'plugin_service_started' : 'plugin_window_opened'}});
    }
  } catch (error) {
    ipcRenderer.send('billfish:result', {type:'error',id:message.id,code:error.code || 'plugin_error',message:String(error.message).slice(0,4096),details:error.details || {}});
  } finally { requestId = null; }
});
ipcRenderer.on('billfish:shutdown', async () => {
  try { await dispatch('beforeExit'); }
  catch (error) { console.error(error); }
  finally { ipcRenderer.send('billfish:shutdown-ready'); }
});
window.addEventListener('load', async () => {
  if (bootstrap.preview) return;
  try { await dispatch('create', bootstrap.plugin); ipcRenderer.send('billfish:ready'); }
  catch (error) { ipcRenderer.send('billfish:startup-error', {message:String(error.message).slice(0,4096)}); }
}, {once:true});
module.exports = {dispatch,bootstrap,finishPreviewCreate:()=>{previewCreating=false;}};
