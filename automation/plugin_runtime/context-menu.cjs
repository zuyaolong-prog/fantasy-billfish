'use strict';
const {failure} = require('./eagle-sdk.cjs');

// Functions stay in the renderer; only the native menu template and opaque
// callback IDs cross IPC. The popup lifetime owns those callback references.
function createContextMenuApi({openNative, onClick, onClose, reportError}) {
  let sequence = 0;
  const menus = new Map();
  onClick(({menuId,callbackId}) => {
    const callback = menus.get(menuId)?.get(callbackId);
    if (callback) Promise.resolve().then(() => callback()).catch(reportError);
  });
  onClose(menuId => menus.delete(menuId));
  return {open(menuItems) {
    const menuId = String(++sequence), callbacks = new Map();
    function serialize(items,depth = 0) {
      if (!Array.isArray(items) || depth > 16) throw failure('invalid_arguments','contextMenu.open expects menu arrays with depth <= 16');
      return items.map(item => {
        if (!item || typeof item !== 'object' || Array.isArray(item)) throw failure('invalid_arguments','Each context menu item must be an object');
        const result = {};
        for (const [key,value] of Object.entries(item)) {
          if (key === 'click') {
            if (typeof value !== 'function') throw failure('invalid_arguments','Menu click must be a function');
            const callbackId = String(callbacks.size + 1); callbacks.set(callbackId,value); result.callbackId = callbackId;
          } else if (key === 'submenu') result.submenu = serialize(value,depth + 1);
          else if (['id','label','type','role','accelerator','sublabel','toolTip'].includes(key)) {
            if (typeof value !== 'string') throw failure('invalid_arguments',`Menu ${key} must be a string`);
            result[key] = value;
          } else if (['enabled','visible','checked'].includes(key)) {
            if (typeof value !== 'boolean') throw failure('invalid_arguments',`Menu ${key} must be boolean`);
            result[key] = value;
          } else throw failure('unsupported_api',`contextMenu.open item field ${key}`);
        }
        return result;
      });
    }
    const template = serialize(menuItems);
    menus.set(menuId,callbacks);
    try { openNative(menuId,template); }
    catch(error) { menus.delete(menuId); throw error; }
    // Eagle's open() is a void operation; clicks arrive as later callbacks.
  }};
}

module.exports = {createContextMenuApi};
