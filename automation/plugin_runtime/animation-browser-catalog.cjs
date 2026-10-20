'use strict';
const path=require('node:path'),{spawn}=require('node:child_process'),{TextDecoder}=require('node:util');
function failure(code,message,details){return Object.assign(new Error(message),{code,details});}
const canonical=value=>path.resolve(value).toLowerCase();
function validateSnapshot(data,libraryRoot,scopeRoot){
 if(!data||data.schemaVersion!==2||data.complete!==true||typeof data.libraryRoot!=='string'||canonical(data.libraryRoot)!==canonical(libraryRoot)||typeof data.scopeRoot!=='string'||canonical(data.scopeRoot)!==canonical(scopeRoot)||data.scopeMode!=='model-directory-recursive'||typeof data.revision!=='string'||!data.revision||!Array.isArray(data.items))throw failure('invalid_library_catalog','The helper did not return a complete snapshot of the bound library');
 const ids=new Set();for(const item of data.items){
  if(!item||typeof item.id!=='string'||!/^\d+$/.test(item.id)||ids.has(item.id)||typeof item.exists!=='boolean'||typeof item.isLink!=='boolean')throw failure('invalid_library_catalog','The library snapshot has an invalid or duplicate item identity');ids.add(item.id);
  if(item.path!==null&&item.path!==undefined){
   if(typeof item.path!=='string'||!path.isAbsolute(item.path)||!['.fbx','.glb'].includes(path.extname(item.path).toLowerCase()))throw failure('invalid_library_catalog','A library candidate has an invalid source path');
   const relative=path.relative(path.resolve(scopeRoot),path.resolve(item.path));if(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative))throw failure('invalid_library_catalog','A library candidate is outside the model directory subtree');
  }else if(item.exists&&!item.isLink&&!item.pathError)throw failure('invalid_library_catalog','An existing owned candidate is missing its source path');
 }
 return data;
}
function createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess=spawn,timeoutMs=30000,maxOutputBytes=64*1024*1024,maxRequestBytes=8*1024*1024}){
 if(!path.isAbsolute(installRoot)||!path.isAbsolute(libraryRoot))throw failure('invalid_library_context','Catalog helper roots must be absolute');
 const boundRoot=path.resolve(libraryRoot),children=new Set();let stopped=false,worker=null,nextId=1;
 function retire(current,error){
  if(worker===current)worker=null;
  if(current.retired)return;current.retired=true;current.buffer=Buffer.alloc(0);
  for(const entry of current.pending.values())entry.finish(error);current.pending.clear();current.child.kill();
 }
 function ensureWorker(){
  if(worker)return worker;
  const child=spawnProcess(path.join(installRoot,'tools','python','python.exe'),['-I',path.join(installRoot,'automation','animation_catalog_helper.py'),boundRoot],{cwd:installRoot,windowsHide:true,stdio:['pipe','pipe','pipe']});
  const current={child,pending:new Map(),buffer:Buffer.alloc(0),diagnostic:'',retired:false};worker=current;children.add(child);
  child.stderr.on('data',chunk=>{current.diagnostic=(current.diagnostic+chunk.toString('utf8')).slice(-8192);});
  child.stdin.on('error',error=>retire(current,failure('library_catalog_failed',error.message)));
  child.once('error',error=>{children.delete(child);retire(current,failure('library_catalog_failed',error.message));});
  child.once('close',code=>{children.delete(child);retire(current,failure('library_catalog_failed',current.diagnostic||`Catalog helper exited (${code})`));});
  child.stdout.on('data',chunk=>{
   if(current.retired)return;
   // Bound each complete response and the unfinished line independently, even
   // when a stream chunk contains several replies.
   let offset=0;
   while(offset<chunk.length&&!current.retired){
    const newline=chunk.indexOf(10,offset),end=newline<0?chunk.length:newline;
    if(current.buffer.length+end-offset>maxOutputBytes){retire(current,failure('library_catalog_too_large','The complete library catalog snapshot exceeds its bounded response size'));return;}
    current.buffer=Buffer.concat([current.buffer,chunk.subarray(offset,end)]);offset=newline<0?chunk.length:newline+1;
    if(newline<0)break;
    const line=current.buffer;current.buffer=Buffer.alloc(0);
    try{
     const response=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(line)),entry=current.pending.get(response?.requestId);
     if(!entry||typeof response.ok!=='boolean')throw failure('invalid_library_catalog','The catalog helper returned an invalid response identity');
     if(entry.cancelled){current.pending.delete(response.requestId);entry.finish();continue;}
     if(!response.ok){if(typeof response.error?.code!=='string'||typeof response.error?.message!=='string')throw failure('invalid_library_catalog','The catalog helper returned an invalid error');current.pending.delete(response.requestId);entry.finish(failure(response.error.code,response.error.message,response.error.details));}
     else {const data=validateSnapshot(response.data,boundRoot,entry.scopeRoot);current.pending.delete(response.requestId);entry.finish(null,data);}
    }catch(error){retire(current,error.code?error:failure('invalid_library_catalog',error.message));}
   }
  });
  return current;
 }
 function getLibraryCandidates({libraryRoot:requestedRoot,scopeRoot,signal}={}){
  if(stopped)return Promise.reject(failure('ABORT_ERR','The library catalog reader stopped'));
  if(typeof requestedRoot!=='string'||!path.isAbsolute(requestedRoot)||canonical(requestedRoot)!==canonical(boundRoot))return Promise.reject(failure('invalid_library_context','The catalog request belongs to another library'));
  if(typeof scopeRoot!=='string'||!path.isAbsolute(scopeRoot))return Promise.reject(failure('invalid_animation_scope','A fixed model directory is required'));
  const scopeRelative=path.relative(boundRoot,path.resolve(scopeRoot));if(scopeRelative==='..'||scopeRelative.startsWith('..'+path.sep)||path.isAbsolute(scopeRelative))return Promise.reject(failure('invalid_animation_scope','The model directory is outside the bound library'));
  if(signal?.aborted)return Promise.reject(failure('ABORT_ERR','Library catalog request cancelled'));
  scopeRoot=path.resolve(scopeRoot);
  const requestId=String(nextId++),payload=Buffer.from(JSON.stringify({requestId,scopeRoot})+'\n','utf8');
  if(payload.length>maxRequestBytes)return Promise.reject(failure('library_catalog_request_too_large','The catalog request exceeds its bounded size'));
  return new Promise((resolve,reject)=>{
   let current;try{current=ensureWorker();}catch(error){reject(failure('library_catalog_failed',error.message));return;}
   let settled=false;
   const finish=(error,data)=>{clearTimeout(timer);signal?.removeEventListener('abort',cancel);if(settled)return;settled=true;if(error)reject(error);else resolve(data);};
   const cancel=()=>{entry.cancelled=true;signal?.removeEventListener('abort',cancel);if(!settled){settled=true;reject(failure('ABORT_ERR','Library catalog request cancelled'));}};
   const timer=setTimeout(()=>retire(current,failure('library_catalog_timeout','The library catalog helper exceeded its deadline')),timeoutMs);
   const entry={scopeRoot,cancelled:false,finish};current.pending.set(requestId,entry);
   signal?.addEventListener('abort',cancel,{once:true});
   current.child.stdin.write(payload);if(signal?.aborted)cancel();
  });
 }
 return {getLibraryCandidates,shutdown(){stopped=true;if(worker)retire(worker,failure('ABORT_ERR','The library catalog reader stopped'));for(const child of children)child.kill();}};
}
module.exports={createLibraryCatalogReader};
