'use strict';
// Installed only in the trusted Core preload world. The page receives no Worker
// factory or registration API; native workers retain their normal interface.
function installCoreWorkers({reserve,complete,release,onFailure}){
 const NativeWorker=window.Worker,create=window.URL.createObjectURL,revoke=window.URL.revokeObjectURL,blobs=new Map(),owned=new Set();let disposed=false;
 const createOwned=function(blob){const url=create.call(window.URL,blob);blobs.set(url,blob);return url;};
 const revokeOwned=function(url){blobs.delete(String(url));return revoke.call(window.URL,url);};
 function CoreWorker(url,options){
  if(!new.target)throw new TypeError('Worker must be constructed with new');if(disposed)throw new Error('The Core worker owner stopped');
  const blob=blobs.get(String(url));if(!blob||options?.type==='module')return new NativeWorker(url,options);
  const registration=reserve();if(!registration?.url||!registration?.token)throw new Error('Core worker reservation failed');
  const worker=new NativeWorker(registration.url,options);owned.add(worker);worker.addEventListener('error',event=>{if(!disposed)onFailure(new Error(event.message||'The 3D resource worker failed'));});
  blob.text().then(code=>{if(!disposed)return complete(registration.token,code);}).catch(error=>{if(!disposed)onFailure(error);});return worker;
 }
 CoreWorker.prototype=NativeWorker.prototype;Object.setPrototypeOf(CoreWorker,NativeWorker);window.URL.createObjectURL=createOwned;window.URL.revokeObjectURL=revokeOwned;window.Worker=CoreWorker;
 return {dispose(){if(disposed)return;disposed=true;let firstError;for(const worker of owned)try{worker.terminate();}catch(error){firstError||=error;}owned.clear();for(const url of blobs.keys())revoke.call(window.URL,url);blobs.clear();if(window.Worker===CoreWorker)window.Worker=NativeWorker;if(window.URL.createObjectURL===createOwned)window.URL.createObjectURL=create;if(window.URL.revokeObjectURL===revokeOwned)window.URL.revokeObjectURL=revoke;release();if(firstError)throw firstError;}};
}
module.exports={installCoreWorkers};
