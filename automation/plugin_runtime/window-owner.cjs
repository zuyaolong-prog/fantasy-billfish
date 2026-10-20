'use strict';

function createWindowOwnerBridge({required,send,timeoutMs=10000}) {
  const pending=new Map(),attached=new WeakSet();let sequence=0,stopped=false;
  function error(code,message){return Object.assign(new Error(message),{code});}
  function settle(id,value,failure){const entry=pending.get(id);if(!entry)return;pending.delete(id);clearTimeout(entry.timeout);entry.window.removeListener('closed',entry.onClosed);if(failure)entry.reject(failure);else{attached.add(entry.window);if(!entry.window.isDestroyed())entry.window.webContents.send('billfish:window-owned');entry.resolve(value);}}
  function attach(window,kind,background){
    if(!required)return Promise.resolve({attached:false,standalone:true});
    if(stopped)return Promise.reject(error('plugin_disabled','Plugin window ownership session stopped'));
    if(!['main','viewer'].includes(kind)||typeof background!=='boolean')return Promise.reject(error('invalid_arguments','Window owner request is invalid'));
    const handle=window.getNativeWindowHandle();
    const hwnd=(handle.length===8?handle.readBigUInt64LE():BigInt(handle.readUInt32LE())).toString();
    if(!/^[1-9][0-9]*$/.test(hwnd))return Promise.reject(error('invalid_window_handle','Plugin window has no valid native handle'));
    attached.delete(window);window.webContents.send('billfish:window-owner-pending');
    const request_id='window-'+(++sequence);
    const result=new Promise((resolve,reject)=>{
      const onClosed=()=>settle(request_id,null,error('plugin_window_closed','Plugin window closed before its Billfish owner was attached'));
      const timeout=setTimeout(()=>settle(request_id,null,error('window_owner_timeout','Billfish did not attach the plugin window owner within 10 seconds')),timeoutMs);
      pending.set(request_id,{window,hwnd,resolve,reject,onClosed,timeout});window.once('closed',onClosed);
      try{send({type:'window_owner',request_id,window_id:window.id,hwnd,kind,background});}
      catch(failure){settle(request_id,null,failure);}
    });
    // Model loading and native ownership run concurrently; retain the rejection
    // for the show/focus barrier without an unhandled rejection in the meantime.
    result.catch(()=>{});return result;
  }
  return {required:!!required,attach,isAttached:window=>!!window&&attached.has(window),
    receive(message){
      const entry=pending.get(message.request_id);if(!entry)return;
      if(message.error){settle(message.request_id,null,error(message.error.code||'window_owner_failed',message.error.message||'Billfish could not attach the plugin window'));return;}
      const data=message.data;
      if(!data||data.attached!==true||data.window_hwnd!==entry.hwnd||typeof data.owner_hwnd!=='string'||!/^[1-9][0-9]*$/.test(data.owner_hwnd)){
        settle(message.request_id,null,error('invalid_window_owner_response','Billfish did not confirm this native window owner'));return;
      }
      settle(message.request_id,data);
    },
    shutdown(){stopped=true;for(const id of [...pending.keys()])settle(id,null,error('plugin_disabled','Plugin window ownership session stopped'));}};
}
module.exports={createWindowOwnerBridge};
