'use strict';
const {isDeepStrictEqual} = require('node:util');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');
const {pathToFileURL} = require('node:url');

function failure(code, message, details) { const error = new Error(`${code}: ${message}`); error.code = code; if (details) error.details = details; return error; }
function unsupported(name) { throw failure('unsupported_api', name); }
const clone = value => JSON.parse(JSON.stringify(value));
const guarded = (name, values) => new Proxy(values, {get(target, key) {
  if (key === 'then' || key === 'toJSON' || typeof key === 'symbol') return target[key];
  if (!(key in target)) unsupported(`${name}.${key}`);
  return target[key];
}});

function createEagle({call, local, localSync, plugin, app, register, progress, clipboardApi, libraryRoot = '', logApi, contextMenuApi, extraModules}) {
  const appState = {...app};
  const library = {info:async () => call('library.info', {})};
  Object.defineProperty(library,'path',{enumerable:true,get:() => {
    if (!libraryRoot) throw failure('library_not_open','This plugin session has no open library');
    return libraryRoot;
  }});
  class Item {
    constructor(row) {
      this.id = String(row.id);
      this.name = path.parse(row.name || '').name;
      this.ext = row.ext || path.extname(row.name || '').slice(1);
      this.size = row.file_size;
      this.annotation = row.note || '';
      this.url = row.origin || '';
      this.tags = (row.tags || []).map(tag => tag.name);
      this.filePath = row.path;
      const imageExtension = path.extname(row.path || '').slice(1).toLowerCase();
      let originalAsThumbnail = false;
      if (!row.thumbnail_path && row.path && ['jpg','jpeg','png','webp','gif','bmp','ico','avif','svg'].includes(imageExtension)) {
        try { fs.accessSync(row.path,fs.constants.R_OK); originalAsThumbnail = fs.statSync(row.path).isFile(); } catch { /* Missing/unreadable originals cannot become a thumbnail. */ }
      }
      this.noThumbnail = originalAsThumbnail;
      this.thumbnailPath = row.thumbnail_path || (originalAsThumbnail ? row.path : null);
      this.thumbnailURL = this.thumbnailPath ? pathToFileURL(this.thumbnailPath).href : null;
      this.fileURL = this.filePath ? pathToFileURL(this.filePath).href : null;
      this.width = row.width;
      this.height = row.height;
      Object.defineProperty(this, '_baseline', {value:clone(this), writable:true});
    }
    async replaceFile(sourcePath) {
      if (this.id !== this._baseline.id) throw failure('unsupported_write', 'Item.id');
      const itemId = id(this._baseline.id);
      if (typeof sourcePath !== 'string' || !sourcePath || sourcePath.includes('\u0000') || !path.isAbsolute(sourcePath))
        throw failure('invalid_arguments', 'replaceFile requires an absolute local file path');
      const result = await call('items.replace_file', {item_id:itemId,source_path:sourcePath,dry_run:false});
      const details = {item_id:itemId,replace_result:result};
      if (!result || result.completion !== 'native_replace_persisted')
        throw failure('replace_not_confirmed', 'The host did not confirm a persisted replacement; do not retry the write automatically', details);
      if (result.item_id !== itemId || !result.item || result.item.id !== itemId ||
          result.analysis_ready !== true || result.source_safe_to_delete !== true ||
          typeof result.item.path !== 'string' || !path.isAbsolute(result.item.path) ||
          !Number.isSafeInteger(result.item.file_size) || result.item.file_size <= 0 ||
          !Number.isSafeInteger(result.item.width) || result.item.width <= 0 ||
          !Number.isSafeInteger(result.item.height) || result.item.height <= 0)
        throw failure('invalid_host_response', 'Replacement confirmation is missing its matching refreshed Item or completed analysis', details);
      const refreshed = new Item(result.item);
      // Replacement refreshes media content only. Pending note, URL and tag
      // edits must remain dirty for save(), including edits made while awaiting.
      for (const key of ['name','ext','size','filePath','fileURL','width','height','noThumbnail','thumbnailPath','thumbnailURL']) {
        this[key] = refreshed[key];
        this._baseline[key] = refreshed[key];
      }
      return true;
    }
    async save() {
      const submitted = clone(this);
      const baseline = clone(this._baseline);
      // Preflight all dirty fields before any host write, including in-place arrays.
      const dirty = [...new Set([...Object.keys(submitted), ...Object.keys(baseline)])]
        .filter(key => !isDeepStrictEqual(submitted[key], baseline[key]));
      for (const key of dirty) if (!['annotation','url','tags'].includes(key)) throw failure('unsupported_write', `Item.${key}`);
      if (!dirty.length) return true;
      for (const key of ['annotation','url']) if (dirty.includes(key) && typeof submitted[key] !== 'string') throw failure('invalid_arguments', `${key} must be a string`);
      if (dirty.includes('tags') && (!Array.isArray(submitted.tags) || submitted.tags.some(tag => typeof tag !== 'string' || !tag.length || tag.length > 256) || new Set(submitted.tags).size !== submitted.tags.length)) throw failure('invalid_arguments', 'tags must contain unique non-empty names');
      const completed = [];
      try {
        let existingTags;
        if (dirty.includes('tags')) {
          existingTags = (await call('tags.list', {})).items;
          for (const name of new Set([...submitted.tags, ...baseline.tags])) {
            const matches = existingTags.filter(tag => tag.name === name);
            if (matches.length > 1) throw failure('ambiguous_tag_name', `Multiple native tags named ${name}`);
          }
        }
        const patch = {item_id:submitted.id};
        if (dirty.includes('annotation')) patch.note = submitted.annotation;
        if (dirty.includes('url')) patch.origin = submitted.url;
        if (Object.keys(patch).length > 1) {
          await call('items.update', {updates:[patch], dry_run:false}); completed.push('items.update');
        }
        if (dirty.includes('tags')) {
          for (const name of submitted.tags) if (!existingTags.some(tag => tag.name === name)) {
            await call('tags.create', {name, pid:'0', dry_run:false}); completed.push(`tags.create:${name}`);
            existingTags = (await call('tags.list', {})).items;
            if (existingTags.filter(tag => tag.name === name).length !== 1) throw failure('ambiguous_tag_name', `Cannot uniquely resolve created tag ${name}`);
          }
          const toIds = names => names.map(name => {
            const matches = existingTags.filter(tag => tag.name === name);
            if (matches.length !== 1) throw failure('tag_not_found', `Cannot uniquely resolve tag ${name}`);
            return String(matches[0].id);
          });
          const add = toIds(submitted.tags.filter(name => !baseline.tags.includes(name)));
          const remove = toIds(baseline.tags.filter(name => !submitted.tags.includes(name)));
          if (add.length) { await call('tags.assign', {item_ids:[submitted.id],tag_ids:add,mode:'add',dry_run:false}); completed.push('tags.assign:add'); }
          if (remove.length) { await call('tags.assign', {item_ids:[submitted.id],tag_ids:remove,mode:'remove',dry_run:false}); completed.push('tags.assign:remove'); }
        }
        this._baseline = submitted;
        return true;
      } catch (error) {
        if (completed.length) throw failure('partial_update', error.message, {completed_operations:completed, cause_code:error.code || 'host_error'});
        throw error;
      }
    }
  }
  function id(value) {
    if (typeof value !== 'string' || !/^[1-9][0-9]{0,18}$/.test(value) || BigInt(value) > 9223372036854775807n) throw failure('invalid_arguments', 'Billfish item IDs must remain positive signed 64-bit decimal strings');
    return value;
  }
  const byId = async value => new Item(await call('items.get', {item_id:id(value)}));
  function importNameStem(value) {
    // Eagle's name is a human display title (its own examples omit the file
    // extension). The private SDK path carries a safe stem; the host appends
    // the actual source suffix only after the local/downloaded file is known.
    let stem = value.replace(/[\u0000-\u001f<>:"/\\|?*]/g,'_').trim().replace(/[ .]+$/g,'');
    const device = stem.split('.')[0].trimEnd();
    if (/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/i.test(device)) stem = '_' + stem;
    // Leave space for common suffixes. The host applies the final exact
    // 255-UTF-16-unit filename budget using the real suffix, never the URL.
    stem = stem.slice(0,240);
    if (/[\uD800-\uDBFF]$/.test(stem)) stem = stem.slice(0,-1);
    stem = stem.replace(/[ .]+$/g,'') || '_';
    // Trimming a truncated run of spaces can expose a previously hidden device
    // name, so the final stem must satisfy the same rule as the initial title.
    if (/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/i.test(stem.split('.')[0].trimEnd())) stem = '_' + stem;
    return stem;
  }
  function importOptions(options) {
    if (!options || typeof options !== 'object' || Array.isArray(options)) throw failure('invalid_arguments','Import options must be an object');
    const supported = new Set(['name','website','folders']);
    for (const key of Object.keys(options)) if (!supported.has(key)) throw failure('unsupported_import_option',`Import option ${key} is not supported; no file was imported`);
    const values = {};
    if (Object.hasOwn(options,'name') && options.name !== undefined) {
      if (typeof options.name !== 'string') throw failure('invalid_arguments','Import name must be a string');
      if (options.name.trim()) values.name = importNameStem(options.name);
    }
    if (Object.hasOwn(options,'website') && options.website !== undefined) {
      if (typeof options.website !== 'string') throw failure('invalid_arguments','Import website must be a string');
      values.origin = options.website;
    }
    if (Object.hasOwn(options,'folders') && options.folders !== undefined) {
      if (!Array.isArray(options.folders)) throw failure('invalid_arguments','Import folders must be an array');
      if (options.folders.length > 1) throw failure('unsupported_import_option','Billfish import supports at most one native folder ID; no file was imported');
      if (options.folders.length) values.folder_id = options.folders[0] === '0' ? '0' : id(options.folders[0]);
    }
    return values;
  }
  async function importItem(operation,source,options = {}) {
    const arguments_ = importOptions(options);
    if (typeof source !== 'string' || !source) throw failure('invalid_arguments','Import source must be a non-empty string');
    if (operation === 'items.import_url') {
      let parsed;
      try { parsed = new URL(source); } catch { throw failure('invalid_arguments','Import URL must be valid HTTP or HTTPS'); }
      if (!['http:','https:'].includes(parsed.protocol)) throw failure('unsupported_import_source','addFromURL currently supports HTTP and HTTPS only');
      arguments_.url = source;
    } else {
      if (!path.isAbsolute(source)) throw failure('invalid_arguments','addFromPath requires an absolute local file path');
      arguments_.source_path = source;
    }
    // The host owns download, staging and native import. In particular, the
    // renderer never writes the destination file or announces an accepted job
    // as an imported Item.
    const result = await call(operation,arguments_);
    const details = {operation,import_result:result};
    if (result && typeof result.item_id === 'string') details.item_id = result.item_id;
    if (!result || result.completion !== 'native_import_persisted') throw failure('import_not_confirmed','The host did not confirm a persisted native import; do not retry the write automatically',details);
    let createdId;
    try { createdId = id(result.item_id); } catch { throw failure('invalid_host_response','Native import returned an invalid item ID',details); }
    if (!Array.isArray(result.item_ids) || result.item_ids.length !== 1 || result.item_ids[0] !== createdId ||
        !result.item || result.item.id !== createdId || typeof result.analysis_ready !== 'boolean')
      throw failure('invalid_host_response','Native import confirmation is missing its matching hydrated Item or analysis state',details);
    return createdId;
  }
  const eagle = guarded('eagle', {
    plugin,
    isDev:false,
    app:guarded('eagle.app', Object.assign(appState, {isDarkColors:() => appState.isDark,getPath:async name => local('app.getPath',[name])})),
    item:guarded('eagle.item', {
      getById:byId,
      addFromURL:(url,options) => importItem('items.import_url',url,options),
      addFromPath:(sourcePath,options) => importItem('items.import_path',sourcePath,options),
      get:async options => {
        if (!options || typeof options !== 'object' || Object.keys(options).length !== 1 || !Array.isArray(options.ids) || options.ids.length > 500) throw failure('unsupported_query', 'Only get({ids:[string IDs]}) is supported');
        return Promise.all(options.ids.map(byId));
      },
      getSelected:async () => {
        const selected = await call('items.selected', {});
        if (!Array.isArray(selected.items)) throw failure('protocol_error', 'items.selected did not return native selected items');
        return selected.items.map(row => new Item(row));
      }
    }),
    library:guarded('eagle.library', library),
    log:guarded('eagle.log',logApi || {}),
    os:guarded('eagle.os',Object.fromEntries(['tmpdir','version','type','release','arch'].map(name => [name,() => os[name]()]))),
    extraModule:guarded('eagle.extraModule',extraModules || {}),
    contextMenu:guarded('eagle.contextMenu',contextMenuApi || {}),
    window:guarded('eagle.window', Object.assign(Object.fromEntries(['show','hide','close','focus','minimize','maximize','unmaximize','getPosition','setPosition','getSize','setSize','setOpacity','getOpacity','setAlwaysOnTop','isAlwaysOnTop','isVisible','isMinimized','setTitle'].map(name => [name,(...args) => local('window.' + name,args)])),{
      setReferer:url => { if (!localSync) unsupported('eagle.window.setReferer'); localSync('window.setReferer',[url]); }
    })),
    screen:guarded('eagle.screen', {getCursorScreenPoint:() => local('screen.getCursorScreenPoint',[])}),
    shell:guarded('eagle.shell', Object.fromEntries(['openExternal','openPath','showItemInFolder'].map(name => [name,(...args) => local('shell.'+name,args)]))),
    dialog:guarded('eagle.dialog', Object.fromEntries(['showMessageBox','showOpenDialog','showSaveDialog','showErrorBox'].map(name => [name,(...args) => local('dialog.'+name,args)]))),
    notification:guarded('eagle.notification',{show:options => local('notification.show',[options])}),
    clipboard:guarded('eagle.clipboard', clipboardApi || {})
  });
  for (const [name,event] of Object.entries({onPluginCreate:'create',onPluginRun:'run',onPluginShow:'show',onPluginHide:'hide',onPluginBeforeExit:'beforeExit',onThemeChanged:'themeChanged'})) eagle[name] = callback => register(event,callback);
  return {eagle, billfish:{call, progress, registerCommand:(name,callback) => register('command:'+name,callback)}, Item,
    updateTheme:theme => { appState.theme = theme; appState.isDark = theme === 'DARK'; }};
}

module.exports = {createEagle, failure};
