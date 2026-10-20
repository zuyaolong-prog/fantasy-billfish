'use strict';
const fs=require('node:fs/promises'),nativeFs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {inspectGlb,loadGlbClip,readGeometryGlb}=require('./glb.cjs');
const {matchSkeleton,fingerprints,hash}=require('./matching.cjs');
const {ProfileStore,readJson,atomicJson,merge}=require('./profiles.cjs');
const {ParserJobs,aborted}=require('./parser-jobs.cjs');
const {applyModelBasis,convertRootClip}=require('./coordinates.cjs');
const {encodeClip,decodeClip}=require('./clip-codec.cjs');
const SCHEMA=2,NORMALIZATION='canonical-y-meters-2';
const SESSION_SCHEMA=6,CANDIDATE_POLICY='billfish-model-subtree-1';
const canon=p=>path.resolve(p),keyPath=p=>canon(p).toLowerCase();
function within(root,source){const relative=path.relative(canon(root),canon(source));return relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);}
function candidateScope(request,choice='current'){
 if(!['current','parent'].includes(choice))throw Object.assign(new Error('Animation directory choice must be current or parent'),{code:'INVALID_ANIMATION_SCOPE'});
 const current=path.dirname(canon(request.source)),parent=path.dirname(current),canSearchParent=keyPath(current)!==keyPath(request.libraryRoot)&&within(request.libraryRoot,parent);
 if(!within(request.libraryRoot,current))throw Object.assign(new Error('Model directory is outside the bound library'),{code:'INVALID_ANIMATION_SCOPE'});
 return {candidateScope:choice==='parent'&&canSearchParent?'parent':'current',scopeRoot:choice==='parent'&&canSearchParent?parent:current,canSearchParent};
}
const statRecord=(p,s)=>({path:canon(p),size:s.size,mtimeMs:s.mtimeMs,ctimeMs:s.ctimeMs,dev:s.dev,ino:s.ino});
const sameStat=(a,b)=>a&&b&&['size','mtimeMs','ctimeMs','dev','ino'].every(k=>a[k]===b[k]);
async function fingerprint(p){const stat=await fs.stat(p);if(!stat.isFile())throw new Error('Source is not a file');return statRecord(p,stat);}
async function digest(p,signal){if(signal?.aborted)throw aborted();const h=crypto.createHash('sha256');for await(const b of nativeFs.createReadStream(p,{signal,highWaterMark:1024*1024}))h.update(b);return h.digest('hex');}
function defaultProfile(metadata,value={}){const {scope,...saved}=value;return merge({interpretation:'auto',manualPreset:'maya',sourceAxes:null,sourceUnitMeters:null,jointBasisPolicy:{primary:'+X',secondary:'+Y',sourceKind:'default'},jointBasisOverrides:{},fileOverrides:{},fileOverridesByHash:{},fileOverridesByItemId:{},pathAssociations:{},renderFacing:{yaw:0},detected:metadata.coordinates||{},sourceSkeletonFingerprint:fingerprints(metadata),sourceKind:metadata.coordinates?.detected?'detected':'default',fallback:{preset:'maya',sourceAxes:{right:'+X',up:'+Y',front:'+Z'},sourceUnitMeters:0.01,sourceKind:'default'},associations:{},visibility:{enabled:true,showAll:false,show:{}}},saved);}
function validateProfile(p){if(p.interpretation!==undefined&&!['auto','maya','max','custom'].includes(p.interpretation))throw new Error('Unknown coordinate interpretation');if(p.sourceUnitMeters!==undefined&&p.sourceUnitMeters!==null&&(!Number.isFinite(p.sourceUnitMeters)||p.sourceUnitMeters<=0))throw new Error('Unit scale must be positive');if(p.sourceAxes){const a=p.sourceAxes;if(![a.right,a.up,a.front].every(x=>/^[+-][XYZ]$/.test(x))||new Set([a.right,a.up,a.front].map(x=>x[1])).size!==3)throw new Error('Coordinate axes must be three distinct signed axes');}}
class ClipLru {
 constructor(maxBytes){this.maxBytes=maxBytes;this.bytes=0;this.items=new Map();}
 get(key){const item=this.items.get(key);if(!item)return null;this.items.delete(key);this.items.set(key,item);return item.value;}
 put(key,value,bytes){if(bytes>this.maxBytes)return;const old=this.items.get(key);if(old){this.bytes-=old.bytes;this.items.delete(key);}this.items.set(key,{value,bytes});this.bytes+=bytes;while(this.bytes>this.maxBytes){const [k,v]=this.items.entries().next().value;this.items.delete(k);this.bytes-=v.bytes;}}
 clear(){this.items.clear();this.bytes=0;}
}
function createAnimationBrowserService(options){return new AnimationBrowserService(options);}
class AnimationBrowserService {
 constructor(options){for(const key of ['cacheRoot','profileRoot','parserPath'])if(!options[key]||!path.isAbsolute(options[key]))throw new Error(key+' must be absolute');this.options=options;this.sessions=new Map();this.profiles=new ProfileStore(options.profileRoot);this.parserVersion=options.parserVersion||'billfish-ufbx-3';this.jobs=new ParserJobs(options.parserPath,path.join(options.cacheRoot,'jobs'),options.concurrency||2);this.clips=new ClipLru(options.clipCacheBytes||64*1024*1024);this.stats={metadataHits:0,metadataParses:0,modelHits:0,clipHits:0,hashes:0,mappingComputations:0};this.operations=new Set();this.visibilityStates=new Map();this.persistedAssociations=new Map();this.persistedProfileHeads=new Map();this.catalogs=new Map();this.shutdownFlag=false;}
 emit(s,type,payload={}){if(!s.closed&&this.sessions.get(s.id)===s)this.options.onEvent?.(s.id,{type,...payload});}
 async readCatalog(scopeRoot,signal){
  if(signal?.aborted)throw aborted();
  if(typeof this.options.getLibraryCandidates!=='function')throw Object.assign(new Error('A trusted Billfish catalog provider is required'),{code:'CATALOG_REQUIRED'});
  if(typeof scopeRoot!=='string'||!path.isAbsolute(scopeRoot)||!within(this.libraryRoot,scopeRoot))throw Object.assign(new Error('A bound model directory scope is required'),{code:'INVALID_ANIMATION_SCOPE'});
  const value=await this.options.getLibraryCandidates({libraryRoot:this.libraryRoot,scopeRoot,signal});
  if(this.shutdownFlag)throw Object.assign(new Error('Service is shut down'),{code:'SERVICE_STOPPED'});if(signal?.aborted)throw aborted();
  if(value?.schemaVersion!==2||value.complete!==true||typeof value.scopeRoot!=='string'||keyPath(value.scopeRoot)!==keyPath(scopeRoot)||value.scopeMode!=='model-directory-recursive'||typeof value.revision!=='string'||!Array.isArray(value.items)||typeof value.libraryRoot!=='string'||keyPath(value.libraryRoot)!==keyPath(this.libraryRoot))throw Object.assign(new Error('Animation candidates are not a complete snapshot of the model directory subtree'),{code:'INVALID_CATALOG'});
  const byId=new Map();
  for(const item of value.items){
   if(!/^[1-9][0-9]*$/.test(String(item.id))||byId.has(String(item.id)))throw Object.assign(new Error('Catalog candidate identity is invalid or duplicated'),{code:'INVALID_CATALOG'});
   if(item.path){const relative=path.relative(scopeRoot,item.path);if(!path.isAbsolute(item.path)||relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative))throw Object.assign(new Error('Catalog candidate path is outside the model directory subtree'),{code:'INVALID_CATALOG'});}
   byId.set(String(item.id),{...item,id:String(item.id)});
  }
  const catalog={...value,byId};this.catalogs.set(keyPath(scopeRoot),catalog);
  for(const s of this.sessions.values()){
   if(!s.base||keyPath(s.request.scopeRoot)!==keyPath(scopeRoot))continue;s.catalog=catalog;
   const baseValid=!!this.catalogMember(s.request.itemId,s.request.source,catalog),rows=baseValid?s.rows.filter(row=>this.catalogMember(row.sourceItemId,row.source,catalog)):[];
   const changed=s.catalogBaseValid!==baseValid||rows.length!==s.rows.length;
   s.catalogBaseValid=baseValid;s.rows=rows;
   if(changed&&s.model&&s.profileState)this.emit(s,'state',this.state(s));
  }
  return catalog;
 }
 catalogMember(itemId,source,catalog){const item=catalog?.byId.get(String(itemId));return item&&item.exists===true&&!item.isLink&&!item.pathError&&item.path&&keyPath(item.path)===keyPath(source)?item:null;}
 requireCatalogBase(s,catalog=s.catalog){const item=this.catalogMember(s.request.itemId,s.request.source,catalog);if(!item)throw Object.assign(new Error('The selected model is no longer an available item in this Billfish library'),{code:'CATALOG_ITEM_UNAVAILABLE'});s.catalogBaseValid=true;return item;}

 rowEvent(s){return {rows:s.rows,libraryContext:this.libraryContext(s)};}
 libraryContext(s){const item=s.catalog?.byId.get(String(s.request.itemId));return {libraryRoot:s.request.libraryRoot,libraryName:path.basename(s.request.libraryRoot),relativeFolder:path.dirname(item?.relativePath||path.relative(s.request.libraryRoot,s.request.source)).replaceAll('\\','/'),candidateScope:s.request.candidateScope,scopeRoot:s.request.scopeRoot,relativeScope:path.relative(s.request.libraryRoot,s.request.scopeRoot).replaceAll('\\','/'),canSearchParent:candidateScope(s.request).canSearchParent};}

 async initializeLibrary(libraryRoot){
  if(this.shutdownFlag)throw Object.assign(new Error('Service is shut down'),{code:'SERVICE_STOPPED'});
  if(!path.isAbsolute(libraryRoot))throw Object.assign(new Error('Library root must be an absolute trusted directory'),{code:'INVALID_LIBRARY'});
  const root=canon(libraryRoot);if(this.libraryRoot&&keyPath(root)!==keyPath(this.libraryRoot))throw Object.assign(new Error('A backend profile store is scoped to one library'),{code:'INVALID_LIBRARY'});
  // Reserve the trusted library before the first asynchronous filesystem operation.
  this.libraryRoot=root;
  if(this.libraryInitializationPromise)return this.libraryInitializationPromise;
  const promise=this.initializeLibraryWork(root);this.libraryInitializationPromise=promise;
  try{return await promise;}finally{if(this.libraryInitializationPromise===promise)this.libraryInitializationPromise=null;}
 }
 async initializeLibraryWork(libraryRoot){
  let finish;const completion=new Promise(resolve=>{finish=resolve;});this.libraryInitializations||=new Set();this.libraryInitializations.add(completion);const running=()=>{if(this.shutdownFlag)throw Object.assign(new Error('Service stopped during library initialization'),{code:'SERVICE_STOPPED'});};
  try{running();
  running();
  if(!path.isAbsolute(libraryRoot))throw Object.assign(new Error('Library root must be an absolute trusted directory'),{code:'INVALID_LIBRARY'});
  if(this.libraryRoot&&keyPath(libraryRoot)!==keyPath(this.libraryRoot))throw Object.assign(new Error('A backend profile store is scoped to one library'),{code:'INVALID_LIBRARY'});
  let stat;try{stat=await fs.stat(libraryRoot);}catch(e){throw Object.assign(new Error('Library root is unavailable: '+e.message),{code:'INVALID_LIBRARY'});}
  running();if(!stat.isDirectory())throw Object.assign(new Error('Library root is not a directory'),{code:'INVALID_LIBRARY'});
  this.libraryRoot=canon(libraryRoot);this.libraryInitialized=true;this.libraryDiagnostics||=[];this.libraryWatchers||=new Map();this.libraryPending||=new Set();
  await this.restoreAssociations(this.libraryRoot);running();await this.watchProfiles();running();await this.updateLibraryWatchers();running();
  if(!this.libraryTimer){const interval=this.options.libraryReconcileIntervalMs||30000;this.libraryTimer=setInterval(()=>this.queueLibraryReconciliation(),interval);this.libraryTimer.unref?.();}
  const hiddenPaths=await this.publishVisibility(this.libraryRoot);running();await this.updateLibraryWatchers();running();const records=[...this.persistedAssociations.values()];
  return {libraryRoot:this.libraryRoot,hiddenPaths,restoredAssociations:records.reduce((total,r)=>total+r.rows.length,0),restoredTargets:records.length,watchedScopes:[...this.libraryWatchers.keys()],diagnostics:[...this.libraryDiagnostics],stats:{...this.stats,parser:{...this.jobs.stats}}};  }finally{this.libraryInitializations.delete(completion);finish();}
 }
 noteLibraryIssue(code,scope,error){this.libraryDiagnostics||=[];if(!this.libraryDiagnostics.some(d=>d.code===code&&d.scope===scope))this.libraryDiagnostics.push({code,scope,message:error.message});}

 async updateLibraryWatchers(){
  if(!this.libraryInitialized||this.shutdownFlag)return;
  const desired=new Map([[keyPath(path.join(this.libraryRoot,'.bf')),{directory:path.join(this.libraryRoot,'.bf'),catalog:true}]]),scopes=new Set([...this.persistedAssociations.values()].filter(record=>record.request.candidateScope==='current').map(record=>keyPath(record.request.scopeRoot)));
  for(const session of this.sessions.values())scopes.add(keyPath(session.request.scopeRoot));
  for(const key of this.catalogs.keys())if(!scopes.has(key))this.catalogs.delete(key);
  for(const catalog of this.catalogs.values()){desired.set(keyPath(catalog.scopeRoot),{directory:catalog.scopeRoot,catalog:false});for(const item of catalog.byId.values())if(item.path&&!item.isLink&&!item.pathError)desired.set(keyPath(path.dirname(item.path)),{directory:path.dirname(item.path),catalog:false});}
  for(const [key,watcher] of this.libraryWatchers)if(!desired.has(key)){watcher.close();this.libraryWatchers.delete(key);}
  for(const [key,{directory,catalog}] of desired){if(this.libraryWatchers.has(key))continue;try{
   const watcher=nativeFs.watch(directory,(_event,file)=>{
    if(this.shutdownFlag)return;
    if(file){const name=String(file).toLowerCase();if(catalog?!name.startsWith('billfish.db'):!['.fbx','.glb'].includes(path.extname(name)))return;}
    clearTimeout(this.libraryDebounce);this.libraryDebounce=setTimeout(()=>this.queueLibraryReconciliation(),150);
   });
   watcher.on('error',error=>{this.noteLibraryIssue('LIBRARY_WATCHER_FAILED',directory,error);watcher.close();this.libraryWatchers.delete(key);});this.libraryWatchers.set(key,watcher);
  }catch(error){this.noteLibraryIssue('LIBRARY_WATCHER_UNAVAILABLE',directory,error);}}
 }
 queueLibraryReconciliation(){
  if(this.shutdownFlag||!this.libraryInitialized)return;
  const promise=(async()=>{
   if(this.rebuilding)return;
   await this.refreshProfiles();await this.updateLibraryWatchers();
   await Promise.allSettled([...this.sessions.values()].filter(s=>!s.closed&&s.request.discover!==false&&s.request.mode!=='thumbnail'&&!['discovering','scanning'].includes(s.scan.state)).map(async s=>{const request=s.request,generation=s.generation;const catalog=await this.readCatalog(request.scopeRoot,s.controller.signal),valid=await this.scanReceiptValid(s,s.scanReceipt,catalog);if(s.closed||this.sessions.get(s.id)!==s||s.request!==request||s.generation!==generation)return;if(!valid)return this.startScan(s);}));
   await this.publishVisibility(this.libraryRoot);
  })();this.libraryPending.add(promise);
  promise.catch(error=>this.noteLibraryIssue('LIBRARY_RECONCILIATION_FAILED',this.libraryRoot,error)).finally(()=>this.libraryPending.delete(promise));
 }
 visibilityKey(libraryRoot){return JSON.stringify({kind:'library-visibility'});}
 visibilityFor(s){const global=this.visibilityStates.get(keyPath(s.request.libraryRoot));return {...s.profile.visibility,...(global?.value||{}),revision:global?.revision||null,conflicts:global?.conflicts||[],conflict:!!global?.conflicts.length};}
 state(s){return {sessionId:s.id,source:s.request.source,model:s.model,...this.rowEvent(s),scan:{...s.scan},profile:{...s.profile,visibility:this.visibilityFor(s),revision:s.profileState.revision,conflicts:s.profileState.conflicts},diagnostics:s.diagnostics,listSettings:s.listSettings,sourceFingerprint:s.base.fingerprint};}
 async metadata(source,{verify=false,force=false,signal}={}){
  if(signal?.aborted)throw aborted();const current=await fingerprint(source),entryPath=path.join(this.options.cacheRoot,'index',hash(keyPath(source))+'.json'),old=await readJson(entryPath);
  if(!force&&!verify&&old?.schemaVersion===SCHEMA&&old.parserVersion===this.parserVersion&&sameStat(old.fingerprint,current)){this.stats.metadataHits++;return old;}
  const sha256=await digest(source,signal);this.stats.hashes++;if(signal?.aborted)throw aborted();
  if(!force&&old?.schemaVersion===SCHEMA&&old.parserVersion===this.parserVersion&&old.fingerprint.sha256===sha256){const value={...old,fingerprint:{...current,sha256}};await atomicJson(entryPath,value);this.stats.metadataHits++;return value;}
  const contentPath=path.join(this.options.cacheRoot,'metadata',hash([sha256,this.parserVersion,SCHEMA])+'.json');let data=force?null:await readJson(contentPath);
  if(!data){data=path.extname(source).toLowerCase()==='.glb'?await inspectGlb(source,{signal,geometryOnly:true}):await this.jobs.run({operation:'inspect',source},signal);if(!Array.isArray(data.nodes)||!Array.isArray(data.clips))throw new Error('Parser metadata is missing hierarchy or clips');this.stats.metadataParses++;}
  const after=await fingerprint(source);if(!sameStat(current,after)){const e=new Error('Source changed while parsing');e.code='SOURCE_CHANGED';throw e;}
  if(await digest(source,signal)!==sha256){const e=new Error('Source bytes changed while parsing');e.code='SOURCE_CHANGED';throw e;}this.stats.hashes++;await atomicJson(contentPath,data);const value={schemaVersion:SCHEMA,parserVersion:this.parserVersion,fingerprint:{...current,sha256},metadata:data};await atomicJson(entryPath,value);return value;
 }
 profileKey({source,itemId,libraryRoot},contentHash){const owned=/^\d+$/.test(String(itemId));return JSON.stringify({kind:'mesh',itemId:owned?String(itemId):null,source:owned?null:contentHash||path.relative(libraryRoot,source).replaceAll('\\','/')});}

 async openSession(request,{signal}={}){
  const pending=()=>{if(signal?.aborted)throw aborted();};pending();
  if(this.rebuilding)await this.rebuilding;pending();
  if(this.shutdownFlag)throw new Error('Service is shut down');
  if(!request.sessionId||!path.isAbsolute(request.source)||!path.isAbsolute(request.libraryRoot)||!/^[1-9][0-9]*$/.test(String(request.itemId)))throw new Error('Invalid trusted session identity');
  if(!this.libraryInitialized)await this.initializeLibrary(request.libraryRoot);
  else if(keyPath(request.libraryRoot)!==keyPath(this.libraryRoot))throw new Error('A backend profile store is scoped to one library');
  pending();if(this.sessions.has(request.sessionId))await this.closeSession(request.sessionId);pending();
  const controller=new AbortController(),s={id:request.sessionId,request:{...request,itemId:String(request.itemId),source:canon(request.source),libraryRoot:canon(request.libraryRoot),...candidateScope(request)},controller,closed:false,rows:[],scan:{state:'idle',completed:0,total:0,errorCount:0},diagnostics:[],listSettings:{},generation:0};const abort=()=>controller.abort();signal?.addEventListener('abort',abort,{once:true});s.detachSignal=()=>signal?.removeEventListener('abort',abort);if(signal?.aborted)abort();this.sessions.set(s.id,s);const running=()=>{if(controller.signal.aborted||s.closed||this.sessions.get(s.id)!==s)throw aborted();};
  try{
   running();s.catalog=await this.readCatalog(s.request.scopeRoot,controller.signal);running();this.requireCatalogBase(s);
   this.emit(s,'progress',{scan:{state:'loading-model',completed:0,total:1,errorCount:0}});
   s.base=await this.metadata(s.request.source,{signal:controller.signal});running();
   if(request.sourceFingerprint?.sha256&&request.sourceFingerprint.sha256!==s.base.fingerprint.sha256)throw new Error('Trusted source fingerprint no longer matches source');
   if(request.sourceFingerprint?.bytes!==undefined&&String(request.sourceFingerprint.bytes)!==String(s.base.fingerprint.size))throw new Error('Trusted source size no longer matches source');
   if(!s.base.metadata.hasMeshes)throw new Error('The selected source has no renderable mesh');
   s.key=this.profileKey(s.request,s.base.fingerprint.sha256);s.profileState=await this.profiles.read(s.key);running();
   const visibility=await this.profiles.read(this.visibilityKey(s.request.libraryRoot));running();this.visibilityStates.set(keyPath(s.request.libraryRoot),visibility);
   s.profile=defaultProfile(s.base.metadata,s.profileState.value);s.listSettings=s.profile.listSettings||{};s.model=await this.model(s);running();
   s.rows=request.discover===false||request.mode==='thumbnail'?[]:await this.cachedRows(s);running();
   if(s.profileState.conflicts.length)this.emit(s,'profileConflict',{conflicts:s.profileState.conflicts});
   if(request.discover!==false&&request.mode!=='thumbnail'){await this.watchProfiles();running();if(!s.scanReceipt)this.startScan(s);else this.emit(s,'progress',{scan:{...s.scan}});}
   running();await this.publishVisibility(s.request.libraryRoot);running();return this.state(s);
  }catch(e){if(this.sessions.get(s.id)===s)await this.closeSession(s.id);else s.detachSignal?.();throw e;}
 }
 artifactKey(s,entry,clipIndex=null){return hash([entry.fingerprint.sha256,this.parserVersion,NORMALIZATION,clipIndex,clipIndex===null?null:'validated-clip-cache-4-binary-f64',this.parserProfile(s,entry.fingerprint.path||s.request.source),this.parserProfile(s),s.profile.jointBasisOverrides,fingerprints(s.base.metadata)]);}

 parserProfile(s,source=s.request.source){
  const relative=path.relative(s.request.libraryRoot,source).replaceAll('\\','/'),row=s.rows.find(r=>keyPath(r.source)===keyPath(source)),itemId=keyPath(source)===keyPath(s.request.source)?s.request.itemId:row?.sourceItemId||[...s.catalog?.byId.values()||[]].find(item=>item.path&&keyPath(item.path)===keyPath(source))?.id;
  const selected=merge(s.profile,s.profile.fileOverridesByItemId?.[itemId]||s.profile.fileOverrides?.[relative]||s.profile.fileOverridesByHash?.[(keyPath(source)===keyPath(s.request.source)?s.base.fingerprint.sha256:row?.sourceFingerprint.sha256)]||{}),result={interpretation:selected.interpretation};
  if(selected.sourceAxes)result.sourceAxes=selected.sourceAxes;if(selected.sourceUnitMeters)result.sourceUnitMeters=selected.sourceUnitMeters;return result;
 }
 async model(s){const dependencies=[];for(const uri of s.base.metadata.externalResources||[]){if(/^[a-z]+:/i.test(uri)||path.isAbsolute(uri))continue;const resource=path.resolve(path.dirname(s.request.source),decodeURIComponent(uri));try{dependencies.push(await fingerprint(resource));}catch{dependencies.push({path:resource,missing:true});}}const key=hash([this.artifactKey(s,s.base),dependencies,'progressive-glb-materials-1']),output=path.join(this.options.cacheRoot,'models',key+'.glb'),receipt=await readJson(output+'.json');if(receipt?.path===output){try{if(!sameStat(receipt.outputFingerprint,await fingerprint(output))||receipt.materialsPath&&!sameStat(receipt.materialsFingerprint,await fingerprint(receipt.materialsPath)))throw new Error('Model cache changed');this.stats.modelHits++;return {...receipt,policyKey:this.associationPolicyKey(s)};}catch{}}
  await fs.mkdir(path.dirname(output),{recursive:true});let result;
  if(s.base.metadata.format==='glb'){
   const doc=await readGeometryGlb(s.request.source,{signal:s.controller.signal});s.controller.signal.throwIfAborted();
   (doc.json.nodes||[]).forEach((n,i)=>{n.extras={...(n.extras||{}),billfishSourceId:String(i)};});
   applyModelBasis(doc,this.parserProfile(s));delete doc.json.animations;
   const materialsPath=output+'.materials.gltf';
   await fs.writeFile(materialsPath,JSON.stringify(doc.materialDocument),{signal:s.controller.signal});
   await fs.writeFile(output,encodeGlb(doc),{signal:s.controller.signal});s.controller.signal.throwIfAborted();
   result={path:output,format:'glb',materialsPath,imageResources:doc.imageResources,source:s.request.source,sourceFingerprint:s.base.fingerprint,nodeMap:Object.fromEntries(s.base.metadata.nodes.map((n,i)=>[n.id,[i]])),metadata:{...s.base.metadata,coordinates:{...s.base.metadata.coordinates,selected:this.parserProfile(s),canonical:{up:'+Y',unitMeters:1}}},warnings:[...(s.base.metadata.warnings||[])],progressiveMaterials:true};
  }
  else {result=await this.jobs.run({operation:'model',source:s.request.source,output,profile:this.parserProfile(s)},s.controller.signal);result={...result,format:'glb',metadata:result.metadata||s.base.metadata};}
  if(!sameStat(await fingerprint(s.request.source),s.base.fingerprint))throw new Error('Mesh changed during conversion');result.policyKey=this.associationPolicyKey(s);result.outputFingerprint=await fingerprint(output);if(result.materialsPath)result.materialsFingerprint=await fingerprint(result.materialsPath);result.dependencies=dependencies;await atomicJson(output+'.json',result);return result;
 }
 associationPolicyKey(s){return hash([this.parserVersion,NORMALIZATION,s.base.fingerprint.sha256,this.parserProfile(s),s.profile.fileOverrides,s.profile.fileOverridesByHash,s.profile.fileOverridesByItemId,s.profile.jointBasisOverrides]);}
 sessionCacheKey(s){return hash([keyPath(s.request.libraryRoot),s.key,keyPath(s.request.scopeRoot),s.request.candidateScope]);}

 async cachedRows(s){
  const stored=await readJson(path.join(this.options.cacheRoot,'sessions',this.sessionCacheKey(s)+'.json'));
  if(stored?.request?.scopeRoot!==s.request.scopeRoot||stored?.request?.candidateScope!==s.request.candidateScope||stored?.sessionSchema!==SESSION_SCHEMA||stored.candidatePolicy!==CANDIDATE_POLICY||stored.schemaVersion!==SCHEMA||stored.parserVersion!==this.parserVersion||stored.normalization!==NORMALIZATION||stored.policyKey!==this.associationPolicyKey(s)||stored.baseHash!==s.base.fingerprint.sha256)return [];
  const rows=[];for(const row of stored.rows||[]){if(!this.catalogMember(row.sourceItemId,row.source,s.catalog)||!s.base.metadata.hasSkinnedMeshes&&row.origin!=='embedded')continue;try{if(sameStat(row.sourceFingerprint,await fingerprint(row.source)))rows.push(this.applyAssociation(s,row));}catch{}}
  s.rowsPolicyKey=this.associationPolicyKey(s);
  if(rows.length===(stored.rows||[]).length&&await this.scanReceiptValid(s,stored.scanReceipt,s.catalog)){s.scanReceipt=stored.scanReceipt;s.catalogRevision=s.catalog.revision;s.scan={state:'complete',completed:stored.scanReceipt.files.length,total:stored.scanReceipt.files.length,errorCount:0};}return rows;
 }
 async scanReceiptValid(s,receipt,catalog){
  const request=s.request,generation=s.generation;
  if(!receipt||receipt.revision!==catalog.revision||receipt.policyKey!==this.associationPolicyKey(s)||!Array.isArray(receipt.files))return false;
  const files=[...catalog.byId.values()].filter(item=>this.catalogMember(item.id,item.path,catalog)&&(s.base.metadata.hasSkinnedMeshes||item.id===s.request.itemId));
  if(files.length!==receipt.files.length)return false;
  const known=new Map(receipt.files.map(item=>[item.id,item]));
  for(const item of files){const prior=known.get(item.id);if(!prior||keyPath(prior.source)!==keyPath(item.path))return false;try{if(!sameStat(prior.fingerprint,await fingerprint(item.path)))return false;}catch{return false;}}
  return !s.closed&&!s.controller.signal.aborted&&this.sessions.get(s.id)===s&&s.request===request&&s.generation===generation;
 }
 applyAssociation(s,row){const relative=s.request?path.relative(s.request.libraryRoot,row.source).replaceAll('\\','/'):row.relativePath;const manual=s.profile.associations[row.id]||s.profile.pathAssociations?.[relative+'#'+row.clipIndex]||{},association={enabled:row.compatibility==='full',source:'automatic',...manual},visibility=s.request?this.visibilityFor(s):s.profile.visibility,sourceKey=s.request?path.relative(s.request.libraryRoot,row.source).replaceAll('\\','/'):null;const hidden=!!(row.pureAnimation&&row.compatibility==='full'&&association.enabled&&visibility.enabled&&!visibility.showAll&&!visibility.show?.[row.id]&&!visibility.showSources?.[sourceKey]&&!visibility.conflict&&!s.profileState.conflicts.length);return {...row,association,hidden};}

 startScan(s,options={}){const promise=this.scan(s,options),generation=s.generation;s.scanPromise=promise.catch(e=>{if(s.generation===generation)this.scanError(s,e);if(e.code!=='ABORT_ERR')throw e;return this.state(s);});s.scanPromise.catch(()=>{});return s.scanPromise;}
 async scan(s,{verify=false,forceSource=null}={}){
  s.scanController?.abort();const controller=new AbortController();s.scanController=controller;
  const generation=++s.generation,signal=controller.signal;s.scanReceipt=null;s.scan={state:'discovering',completed:0,total:0,errorCount:0};this.emit(s,'progress',{scan:{...s.scan}});
  const catalog=await this.readCatalog(s.request.scopeRoot,signal);if(signal.aborted||generation!==s.generation)throw aborted();s.catalog=catalog;this.requireCatalogBase(s,catalog);
  if(!sameStat(s.base.fingerprint,await fingerprint(s.request.source))){
   s.rows=[];s.rowsPolicyKey=null;await this.publishVisibility(s.request.libraryRoot);
   const base=await this.metadata(s.request.source,{signal});if(signal.aborted||generation!==s.generation)throw aborted();
   s.base=base;s.profile=defaultProfile(base.metadata,s.profileState.value);const model=await this.model(s);
   if(signal.aborted||generation!==s.generation)throw aborted();s.model=model;this.emit(s,'state',this.state(s));
  }
  const reusable=s.rowsPolicyKey===this.associationPolicyKey(s)?new Map(s.rows.map(r=>[r.sourceItemId+'#'+r.sourceFingerprint.sha256+'#'+r.clipIndex,r])):new Map();
  const files=[...catalog.byId.values()].filter(item=>this.catalogMember(item.id,item.path,catalog)&&(s.base.metadata.hasSkinnedMeshes||item.id===s.request.itemId));
  s.scan={state:'scanning',completed:0,total:files.length,errorCount:0};const rows=[],scannedFiles=[];let next=0,lastRowsPublished=0;
  const publishRows=()=>{if(signal.aborted||s.closed||generation!==s.generation)return;s.rows=rows.map(row=>({...row,hidden:false}));s.rowsPolicyKey=this.associationPolicyKey(s);this.emit(s,'rows',this.rowEvent(s));lastRowsPublished=Date.now();};
  const worker=async()=>{while(next<files.length){const item=files[next++],source=item.path;try{
   const embedded=item.id===s.request.itemId,entry=embedded?s.base:await this.metadata(source,{verify,force:forceSource&&keyPath(source)===keyPath(forceSource),signal});
   if(signal.aborted||s.closed||generation!==s.generation)throw aborted();
   scannedFiles.push({id:item.id,source,fingerprint:entry.fingerprint});
   for(const clip of entry.metadata.clips){
    const prior=reusable.get(item.id+'#'+entry.fingerprint.sha256+'#'+clip.index);if(!prior)this.stats.mappingComputations++;
    const match=prior?{compatibility:prior.compatibility,mapping:prior.mapping,report:prior.report}:embedded?{compatibility:'full',mapping:Object.fromEntries(entry.metadata.nodes.map(n=>[n.id,n.id])),report:{compatibility:'full',source:'embedded',totalTracks:clip.tracks.length,matchedTracks:clip.tracks.length,retargeting:false}}:matchSkeleton(s.base.metadata,entry.metadata,clip);
    if(Object.keys(s.profile.jointBasisOverrides||{}).length){match.compatibility=match.compatibility==='incompatible'?'incompatible':'partial';match.report={...match.report,compatibility:match.compatibility,jointBasisOverride:{applied:false,reason:'Joint-local basis changes require explicit verified mapping; records alone do not retarget'}};}
    const relativePath=item.relativePath||path.relative(s.request.libraryRoot,source).replaceAll('\\','/');
    rows.push(this.applyAssociation(s,{id:hash([CANDIDATE_POLICY,item.id,clip.index]),sourceItemId:item.id,origin:embedded?'embedded':'library-animation',source,relativePath,folder:path.dirname(relativePath).replaceAll('\\','/'),name:clip.name||item.name,clipIndex:clip.index,duration:clip.duration,fps:clip.fps??null,format:entry.metadata.format,compatibility:match.compatibility,report:match.report,mapping:match.mapping,pureAnimation:!entry.metadata.hasMeshes,sourceFingerprint:entry.fingerprint,coordinates:entry.metadata.coordinates}));
   }
  }catch(e){if(e.code==='ABORT_ERR')throw e;s.scan.errorCount++;s.diagnostics.push({code:e.code||'INSPECT_FAILED',sourceItemId:item.id,source,message:e.message});this.emit(s,'warning',{warning:{sourceItemId:item.id,source,code:e.code||'INSPECT_FAILED',message:e.message}});}
  finally{if(!signal.aborted&&generation===s.generation){s.scan.completed++;if(rows.length&&(!lastRowsPublished||Date.now()-lastRowsPublished>=250))publishRows();this.emit(s,'progress',{scan:{...s.scan}});}}
  }};
  const results=await Promise.allSettled(Array.from({length:Math.min(this.jobs.concurrency,files.length)},worker));const failed=results.find(r=>r.status==='rejected');if(failed)throw failed.reason;
  if(signal.aborted||s.closed||generation!==s.generation)throw aborted();
  const current=await this.readCatalog(s.request.scopeRoot,signal);this.requireCatalogBase(s,current);
  if(signal.aborted||s.closed||generation!==s.generation)throw aborted();
  if(current.revision!==catalog.revision)return this.startScan(s,{verify,forceSource});
  const receipt={revision:catalog.revision,policyKey:this.associationPolicyKey(s),files:scannedFiles};
  const valid=s.scan.errorCount?false:await this.scanReceiptValid(s,receipt,current);
  if(signal.aborted||s.closed||this.sessions.get(s.id)!==s||generation!==s.generation)throw aborted();
  if(!s.scan.errorCount&&!valid)return this.startScan(s,{verify,forceSource});
  s.rows=rows.sort((a,b)=>a.relativePath.localeCompare(b.relativePath)||a.clipIndex-b.clipIndex);s.scan.state='complete';s.rowsPolicyKey=this.associationPolicyKey(s);s.catalogRevision=catalog.revision;
  s.scanReceipt=s.scan.errorCount?null:receipt;
  if(s.scanReceipt)await this.saveAssociations(s);this.emit(s,'rows',this.rowEvent(s));this.emit(s,'progress',{scan:{...s.scan}});await this.publishVisibility(s.request.libraryRoot);return this.state(s);
 }
 scanError(s,e){if(s.closed)return;if(e.code==='ABORT_ERR'){s.scan.state='cancelled';}else{s.scan.state='error';s.rows=[];s.diagnostics.push({code:e.code||'SCAN_FAILED',message:e.message});this.emit(s,'rows',this.rowEvent(s));this.publishVisibility(s.request.libraryRoot).catch(()=>{});}this.emit(s,'progress',{scan:{...s.scan}});}
 requestProfileRefresh(){if(this.profileRefreshScheduled||this.shutdownFlag)return;this.profileRefreshScheduled=true;queueMicrotask(()=>this.refreshProfiles().catch(()=>{}).finally(()=>{this.profileRefreshScheduled=false;}));}
 async watchProfiles(){
  if(this.profileWatcher||this.shutdownFlag)return;await fs.mkdir(this.options.profileRoot,{recursive:true});if(this.profileWatcher||this.shutdownFlag)return;
  try{this.profileWatcher=nativeFs.watch(this.options.profileRoot,{recursive:true},(_,file)=>{if(file&&!String(file).endsWith('.json'))return;clearTimeout(this.profileTimer);this.profileTimer=setTimeout(()=>this.refreshProfiles().catch(()=>{}),150);});this.profileWatcher.on('error',()=>{});}catch{}
 }
 async refreshProfiles(){
  if(this.shutdownFlag||this.rebuilding)return;
  const changedLibraries=new Set();
  for(const s of this.sessions.values()){
   if(s.closed||!s.key)continue;const current=await this.profiles.read(s.key),beforeHeads=s.profileState.heads.map(h=>h.revision).sort().join(','),afterHeads=current.heads.map(h=>h.revision).sort().join(',');if(beforeHeads===afterHeads)continue;
   changedLibraries.add(s.request.libraryRoot);s.profileState=current;s.profile=defaultProfile(s.base.metadata,current.value);s.listSettings=s.profile.listSettings||{};
   if(current.conflicts.length){s.scanController?.abort();s.rows=s.rows.map(r=>({...r,hidden:false}));this.emit(s,'profileConflict',{conflicts:current.conflicts});}
   else if(s.rowsPolicyKey!==this.associationPolicyKey(s)){s.rows=s.rows.map(r=>({...r,hidden:false}));const expectedRevision=s.profileState.revision,context={...s,profile:structuredClone(s.profile)},model=await this.model(context);if(s.profileState.revision!==expectedRevision)continue;s.model=model;this.startScan(s);}
   else s.rows=s.rows.map(r=>this.applyAssociation(s,r));
   this.emit(s,'state',this.state(s));
  }
  const activeKeys=new Set([...this.sessions.values()].filter(s=>!s.closed).map(s=>s.key));
  for(const record of this.persistedAssociations.values()){if(record.request.candidateScope!=='current'||activeKeys.has(record.key))continue;const current=await this.profiles.read(record.key),heads=current.heads.map(h=>h.revision).sort().join(',');if(this.persistedProfileHeads.get(record.cacheKey)!==heads){this.persistedProfileHeads.set(record.cacheKey,heads);changedLibraries.add(record.request.libraryRoot);}}
  for(const library of new Set([...(this.libraryInitialized?[this.libraryRoot]:[]),...[...this.sessions.values()].map(s=>s.request.libraryRoot)])){const current=await this.profiles.read(this.visibilityKey(library)),prior=this.visibilityStates.get(keyPath(library));if(!prior||current.heads.map(h=>h.revision).sort().join(',')!==prior.heads.map(h=>h.revision).sort().join(','))changedLibraries.add(library);}
  for(const library of changedLibraries)await this.publishVisibility(library);
 }
 async restoreAssociations(libraryRoot=this.libraryRoot){
  if(this.restored)return;this.restored=true;let files=[];
  try{files=await fs.readdir(path.join(this.options.cacheRoot,'sessions'));}catch(e){if(e.code!=='ENOENT')throw e;}
  for(const f of files.filter(x=>x.endsWith('.json'))){const value=await readJson(path.join(this.options.cacheRoot,'sessions',f));
   const valid=value?.sessionSchema===SESSION_SCHEMA&&value.candidatePolicy===CANDIDATE_POLICY&&value?.schemaVersion===SCHEMA&&value.cacheKey&&value.key&&typeof value.request?.libraryRoot==='string'&&path.isAbsolute(value.request.libraryRoot)&&typeof value.request?.source==='string'&&path.isAbsolute(value.request.source)&&value.baseFingerprint&&typeof value.request.scopeRoot==='string'&&path.isAbsolute(value.request.scopeRoot)&&['current','parent'].includes(value.request.candidateScope)&&within(value.request.libraryRoot,value.request.source)&&keyPath(value.request.scopeRoot)===keyPath(candidateScope(value.request,value.request.candidateScope).scopeRoot)&&Array.isArray(value.rows)&&value.rows.every(row=>typeof row?.source==='string'&&path.isAbsolute(row.source)&&row.sourceFingerprint&&typeof row.sourceItemId==='string'&&['embedded','library-animation'].includes(row.origin)&&['full','partial','incompatible'].includes(row.compatibility));
   if(!valid){if(this.libraryInitialized)this.noteLibraryIssue('INVALID_ASSOCIATION_CACHE',f,new Error('Derived association cache is invalid and remains visible'));continue;}
   if(value.request.candidateScope!=='current')continue;
   if(!libraryRoot||keyPath(value.request.libraryRoot)===keyPath(libraryRoot))this.persistedAssociations.set(value.cacheKey,value);
  }
 }


 async saveAssociations(s){
  if(!s.scanReceipt)return;
  const value={sessionSchema:SESSION_SCHEMA,candidatePolicy:CANDIDATE_POLICY,catalogRevision:s.catalogRevision,scanReceipt:s.scanReceipt,cacheKey:this.sessionCacheKey(s),schemaVersion:SCHEMA,parserVersion:this.parserVersion,normalization:NORMALIZATION,policyKey:s.rowsPolicyKey,key:s.key,request:s.request,baseHasSkinnedMeshes:!!s.base.metadata.hasSkinnedMeshes,baseHash:s.base.fingerprint.sha256,baseFingerprint:s.base.fingerprint,rows:s.rows};
  this.persistedAssociations.set(value.cacheKey,value);await atomicJson(path.join(this.options.cacheRoot,'sessions',value.cacheKey+'.json'),value);if(this.libraryInitialized)await this.updateLibraryWatchers();
 }
 async publishVisibility(libraryRoot){
  if(this.shutdownFlag)return [];
  this.visibilityStates.set(keyPath(libraryRoot),await this.profiles.read(this.visibilityKey(libraryRoot)));
  const globalHeads=this.visibilityStates.get(keyPath(libraryRoot));
  if(globalHeads.conflicts.length)for(const active of this.sessions.values())this.emit(active,'profileConflict',{scope:'visibility',conflicts:globalHeads.conflicts});
  // Parent is a per-window expansion. Historical parent caches must not hide
  // sibling files before that option is explicitly enabled in a live session.
  const paths=new Set(),candidates=new Map([...this.persistedAssociations].filter(([,record])=>record.request.candidateScope==='current')),catalogs=new Map();
  for(const s of this.sessions.values())if(s.key){for(const [key,record] of candidates)if(record.request.itemId===s.request.itemId)candidates.delete(key);candidates.set(this.sessionCacheKey(s),{key:s.key,request:s.request,baseHasSkinnedMeshes:!!s.base.metadata.hasSkinnedMeshes,baseFingerprint:s.base.fingerprint,rows:s.rows,parserVersion:this.parserVersion,normalization:NORMALIZATION,policyKey:s.rowsPolicyKey,active:s});}
  for(const record of candidates.values()){
   if(keyPath(record.request.libraryRoot)!==keyPath(libraryRoot)||!record.baseHasSkinnedMeshes)continue;
   if(record.active&&(record.active.scan.state!=='complete'||!record.active.scanReceipt))continue;
   const expected=candidateScope(record.request,record.request.candidateScope);if(keyPath(expected.scopeRoot)!==keyPath(record.request.scopeRoot))continue;
   let catalog;const scopeKey=keyPath(record.request.scopeRoot);
   try{if(!catalogs.has(scopeKey))catalogs.set(scopeKey,await this.readCatalog(record.request.scopeRoot));catalog=catalogs.get(scopeKey);}catch(error){
    this.catalogs.delete(scopeKey);this.noteLibraryIssue('CATALOG_UNAVAILABLE',record.request.scopeRoot,error);
    for(const s of this.sessions.values())if(keyPath(s.request.scopeRoot)===scopeKey){s.rows=[];s.catalogBaseValid=false;if(s.model&&s.profileState)this.emit(s,'state',this.state(s));}continue;
   }
   if(!this.catalogMember(record.request.itemId,record.request.source,catalog))continue;
   const ps=await this.profiles.read(record.key);this.persistedProfileHeads.set(record.cacheKey||this.sessionCacheKey({request:record.request,key:record.key}),ps.heads.map(h=>h.revision).sort().join(','));
   if(record.active&&(record.active.profileState.revision!==ps.revision||ps.conflicts.length)){if(ps.conflicts.length)this.emit(record.active,'profileConflict',{conflicts:ps.conflicts});else this.requestProfileRefresh();}
   if(ps.conflicts.length)continue;
   const profile=defaultProfile(record.active?.base.metadata||{nodes:[],coordinates:{}},ps.value),policySession={profile,base:record.active?.base||{fingerprint:record.baseFingerprint},request:record.request,rows:record.rows,catalog};
   if(record.parserVersion!==this.parserVersion||record.normalization!==NORMALIZATION||record.policyKey!==this.associationPolicyKey(policySession))continue;
   const visibility=merge(profile.visibility,globalHeads.value);if(globalHeads.conflicts.length||!visibility.enabled||visibility.showAll)continue;
   try{if(!sameStat(record.baseFingerprint,await fingerprint(record.request.source)))continue;}catch{continue;}
   for(const original of record.rows){
    if(!this.catalogMember(original.sourceItemId,original.source,catalog))continue;
    const row=this.applyAssociation({profile,profileState:ps,request:record.request},original);
    if(!row.hidden||record.rows.some(other=>other.sourceItemId===row.sourceItemId&&!this.applyAssociation({profile,profileState:ps,request:record.request},other).hidden))continue;
    try{if(sameStat(row.sourceFingerprint,await fingerprint(row.source)))paths.add(canon(row.source));}catch{}
   }
  }
  if([...candidates.values()].some(record=>record.active&&(record.active.closed||this.sessions.get(record.active.id)!==record.active||record.active.request!==record.request)))return this.publishVisibility(libraryRoot);
  const hiddenPaths=[...paths].sort();if(this.shutdownFlag)return [];
  this.options.onVisibilityChanged?.(libraryRoot,hiddenPaths);
  for(const s of this.sessions.values())if(s.profile)this.emit(s,'visibility',{hiddenPaths,settings:this.visibilityFor(s)});
  return hiddenPaths;
 }
 async loadClip(s,rowId,cachedKey){
  if(cachedKey!==undefined&&(typeof cachedKey!=='string'||!/^[a-f0-9]{64}$/.test(cachedKey)))throw new Error('Invalid renderer clip cache key');
  const request=s.request,generation=s.generation;
  const owned=()=>{if(s.closed||this.sessions.get(s.id)!==s||s.request!==request||s.generation!==generation)throw aborted();};
  await this.refreshProfiles();owned();
  const catalog=await this.readCatalog(s.request.scopeRoot,s.controller.signal);this.requireCatalogBase(s,catalog);
  if(s.profileState.conflicts.length)throw Object.assign(new Error('Resolve coordinate and association profile conflicts before loading an animation'),{code:'PROFILE_CONFLICT'});
  if(s.scanPromise&&s.rowsPolicyKey!==this.associationPolicyKey(s))await s.scanPromise;
  if(s.model.policyKey!==this.associationPolicyKey(s)||s.rowsPolicyKey!==this.associationPolicyKey(s))throw Object.assign(new Error('Coordinate changes are still being applied'),{code:'PROFILE_APPLY_PENDING'});
  const row=s.rows.find(r=>r.id===rowId);
  if(!row)throw Object.assign(new Error('Animation is no longer an available catalog member'),{code:'CATALOG_ITEM_UNAVAILABLE'});
  if(row.compatibility==='incompatible'||!row.association.enabled||(row.compatibility==='partial'&&!row.association.confirmed))throw new Error('Animation is not associated with this mesh');
  const checkFresh=async(withHash=false,current=catalog)=>{
   owned();this.requireCatalogBase(s,current);if(!this.catalogMember(row.sourceItemId,row.source,current))throw Object.assign(new Error('Animation was removed from the Billfish catalog'),{code:'CATALOG_ITEM_UNAVAILABLE'});
   if(!sameStat(await fingerprint(row.source),row.sourceFingerprint)||!sameStat(await fingerprint(s.request.source),s.base.fingerprint)||(withHash&&await digest(row.source,s.controller.signal)!==row.sourceFingerprint.sha256)){
    row.hidden=false;await this.publishVisibility(s.request.libraryRoot);throw Object.assign(new Error('Source changed during animation decode; refresh association'),{code:'SOURCE_CHANGED'});
   }
   owned();
  };
  await checkFresh();
  const context={...s,profile:structuredClone(s.profile)},key=this.artifactKey(context,{fingerprint:row.sourceFingerprint},row.clipIndex),resultFor=(clip,clipBuffer)=>({clip,clipBuffer,cacheKey:key,mapping:row.mapping,sourceFingerprint:row.sourceFingerprint,rowId:row.id,source:row.source,sourceItemId:row.sourceItemId,clipIndex:row.clipIndex});
  if(cachedKey===key)return {...resultFor(null),reused:true};
  const cached=this.clips.get(key);if(cached){this.stats.clipHits++;return resultFor(cached.clip,cached.payload);}
  const output=path.join(this.options.cacheRoot,'clips',key+'.clipb');let decoded=false,clip=null,payload;
  try{payload=await fs.readFile(output,{signal:s.controller.signal});}catch(e){if(e.code!=='ENOENT')throw e;}
  if(payload){try{clip=decodeClip(payload);validateClip(clip);}catch{clip=null;payload=null;await fs.unlink(output).catch(e=>{if(e.code!=='ENOENT')throw e;});}}
  if(!clip){
   await fs.mkdir(path.dirname(output),{recursive:true});const staging=path.join(this.options.cacheRoot,'jobs',crypto.randomUUID()+'.clipb');await fs.mkdir(path.dirname(staging),{recursive:true});
   try{
    if(row.format==='glb')clip=await loadGlbClip(row.source,row.clipIndex,{signal:s.controller.signal});
    else{await this.jobs.run({operation:'clip',source:row.source,clipIndex:row.clipIndex,output:staging,profile:this.parserProfile(context,row.source)},s.controller.signal);payload=await fs.readFile(staging,{signal:s.controller.signal});clip=decodeClip(payload);}
    const entry=await this.metadata(row.source,{signal:s.controller.signal});
    if(entry.fingerprint.sha256!==row.sourceFingerprint.sha256)throw Object.assign(new Error('Animation content changed during decode'),{code:'SOURCE_CHANGED'});
    const original=clip;clip=convertRootClip(clip,entry.metadata.nodes,row.format==='glb'?this.parserProfile(context,row.source):{},context.base.metadata.format==='glb'?this.parserProfile(context):{});
    if(original!==clip)payload=null;validateClip(clip);
    if(this.associationPolicyKey(s)!==this.associationPolicyKey(context))throw Object.assign(new Error('Coordinate policy changed during decode'),{code:'PROFILE_APPLY_PENDING'});
    decoded=true;
   }finally{await fs.unlink(staging).catch(e=>{if(e.code!=='ENOENT')throw e;});}
  }
  await this.refreshProfiles();owned();
  const after=await this.readCatalog(request.scopeRoot,s.controller.signal);await checkFresh(true,after);
  if(this.associationPolicyKey(s)!==this.associationPolicyKey(context))throw Object.assign(new Error('Coordinate policy changed during decode'),{code:'PROFILE_APPLY_PENDING'});
  payload||=encodeClip(clip);
  if(decoded){await fs.mkdir(path.dirname(output),{recursive:true});const temporary=output+'.'+crypto.randomUUID()+'.tmp';try{await fs.writeFile(temporary,payload);await fs.rename(temporary,output);}finally{await fs.unlink(temporary).catch(e=>{if(e.code!=='ENOENT')throw e;});}}
  owned();this.clips.put(key,{clip:decodeClip(payload),payload},payload.length);return resultFor(clip,payload);
 }

 async updateProfile(s,patch,parents){if('scope' in patch)throw new Error('External animation scope is not supported');validateProfile(patch);s.profileState=await this.profiles.update(s.key,patch,parents);s.profile=defaultProfile(s.base.metadata,s.profileState.value);s.rows=s.rows.map(r=>this.applyAssociation(s,r));s.listSettings=s.profile.listSettings||{};if(s.scan.state==='complete')await this.saveAssociations(s);this.emit(s,'state',this.state(s));if(s.profileState.conflicts.length)this.emit(s,'profileConflict',{conflicts:s.profileState.conflicts});await this.publishVisibility(s.request.libraryRoot);return this.state(s);}
 async call(sessionId,method,args={}){if(this.rebuilding)await this.rebuilding;const operation={sessionId,method};const promise=this.dispatch(sessionId,method,args);operation.promise=promise;this.operations.add(operation);try{return await promise;}finally{this.operations.delete(operation);}}
 async dispatch(sessionId,method,args={}){const s=this.sessions.get(sessionId);if(!s||s.closed)throw new Error('Unknown animation session');switch(method){
  case 'clip.load':return this.loadClip(s,args.rowId,args.cachedKey);
  case 'profile.get':{if(args.scope==='visibility'){const current=await this.profiles.read(this.visibilityKey(s.request.libraryRoot));this.visibilityStates.set(keyPath(s.request.libraryRoot),current);return {profile:{...this.visibilityFor(s),revision:current.revision,conflicts:current.conflicts}};}if(args.scope!=='file')return {profile:this.state(s).profile};const row=s.rows.find(r=>r.id===args.rowId);if(!row)throw new Error('Unknown animation row');const relative=path.relative(s.request.libraryRoot,row.source).replaceAll('\\','/'),override=s.profile.fileOverridesByItemId?.[row.sourceItemId]||s.profile.fileOverrides[relative]||s.profile.fileOverridesByHash[row.sourceFingerprint.sha256]||{};return {profile:merge(this.state(s).profile,override),override,sourceKind:Object.keys(override).length?'explicit':'character'};}
  case 'candidateScope.set':{
   if(!args||Object.keys(args).some(key=>key!=='mode')||!['current','parent'].includes(args.mode))throw Object.assign(new Error('Animation directory choice must be current or parent'),{code:'INVALID_ANIMATION_SCOPE'});
   const next=candidateScope(s.request,args.mode);if(next.candidateScope===s.request.candidateScope)return this.state(s);
   s.scanController?.abort();++s.generation;s.request={...s.request,...next};s.catalog=null;s.rows=[];s.rowsPolicyKey=null;s.catalogRevision=null;s.scan={state:'discovering',completed:0,total:0,errorCount:0};this.emit(s,'state',this.state(s));await this.publishVisibility(s.request.libraryRoot);return this.startScan(s);
  }
  case 'scan.refresh':return this.startScan(s,{verify:!!args.verify});
  case 'scan.cancel':s.scanController?.abort();return {cancelled:true};

  case 'profile.update':{let patch=args.patch||{};if(args.scope==='file'){const row=s.rows.find(r=>r.id===args.rowId);if(!row)throw new Error('Select an animation for a file override');validateProfile(patch);patch={fileOverridesByItemId:{[row.sourceItemId]:patch}};}const coordinateChange=['interpretation','sourceAxes','sourceUnitMeters','jointBasisOverrides','fileOverrides','fileOverridesByHash','fileOverridesByItemId'].some(k=>k in patch);await this.updateProfile(s,patch,args.parents||(args.parentRevision?[args.parentRevision]:undefined));if(coordinateChange){s.rows=s.rows.map(r=>({...r,hidden:false}));await this.publishVisibility(s.request.libraryRoot);s.model=await this.model(s);this.emit(s,'state',this.state(s));return this.startScan(s);}return this.state(s);}
  case 'profile.resolveConflict':{if(args.scope==='visibility'){const key=this.visibilityKey(s.request.libraryRoot),current=await this.profiles.read(key);if(!Array.isArray(args.parents)||current.heads.some(h=>!args.parents.includes(h.revision)))throw new Error('Visibility resolution must name every current head');const result=await this.profiles.update(key,args.patch||{},args.parents);this.visibilityStates.set(keyPath(s.request.libraryRoot),result);await this.publishVisibility(s.request.libraryRoot);this.emit(s,'state',this.state(s));return this.state(s);}s.profileState=await this.profiles.read(s.key);const heads=s.profileState.heads.map(r=>r.revision);if(!Array.isArray(args.parents)||heads.some(h=>!args.parents.includes(h)))throw new Error('Conflict resolution must name every current head');await this.updateProfile(s,args.patch||{},args.parents);s.model=await this.model(s);this.emit(s,'state',this.state(s));return this.startScan(s);}
  case 'association.update':{const row=s.rows.find(r=>r.id===args.rowId);if(!row)throw new Error('Unknown animation row');if(args.enabled&&row.compatibility==='incompatible')throw new Error('Incompatible skeletons cannot be retargeted automatically');if(args.enabled&&row.compatibility==='partial'&&!args.confirmed)throw new Error('Partial compatibility requires explicit confirmation');const association={enabled:!!args.enabled,confirmed:!!args.confirmed,source:'manual'};return this.updateProfile(s,{associations:{[args.rowId]:association},pathAssociations:{[path.relative(s.request.libraryRoot,row.source).replaceAll('\\','/')+'#'+row.clipIndex]:association}});}
  case 'visibility.update':{const patch={};if(args.enabled!==undefined)patch.enabled=!!args.enabled;if(args.showAll!==undefined)patch.showAll=!!args.showAll;if(args.rowId){const row=s.rows.find(r=>r.id===args.rowId);if(!row)throw new Error('Unknown animation row');patch.showSources={[path.relative(s.request.libraryRoot,row.source).replaceAll('\\','/')]:!!args.show};await this.updateProfile(s,{visibility:{show:{[args.rowId]:!!args.show}}});}const globalState=await this.profiles.update(this.visibilityKey(s.request.libraryRoot),patch,args.parents);this.visibilityStates.set(keyPath(s.request.libraryRoot),globalState);for(const active of this.sessions.values())if(keyPath(active.request.libraryRoot)===keyPath(s.request.libraryRoot)){active.rows=active.rows.map(r=>this.applyAssociation(active,r));this.emit(active,'state',this.state(active));}await this.publishVisibility(s.request.libraryRoot);return this.state(s);}
  case 'listSettings.update':return this.updateProfile(s,{listSettings:args});
  case 'diagnostics.get':return {diagnostics:[...s.diagnostics,...(this.libraryDiagnostics||[])],stats:{...this.stats,parser:{...this.jobs.stats},clipCache:{bytes:this.clips.bytes,entries:this.clips.items.size,maxBytes:this.clips.maxBytes}},conflicts:s.profileState.conflicts,rows:s.rows.map(r=>({id:r.id,source:r.source,sourceItemId:r.sourceItemId,origin:r.origin,report:r.report}))};
  case 'cache.command':return this.cacheCommand(s,args);
  default:throw new Error('Unknown animation browser method: '+method);
 }}
 async cacheCommand(s,args){switch(args.command){case 'refresh-current':return this.startScan(s);case 'verify-folder':{s.base=await this.metadata(s.request.source,{verify:true,signal:s.controller.signal});s.model=await this.model(s);this.emit(s,'state',this.state(s));return this.startScan(s,{verify:true});}case 'reparse-current':{const row=s.rows.find(r=>r.id===args.rowId);if(!row)throw new Error('Select an animation to reparse');this.clips.clear();for(const current of s.rows.filter(r=>keyPath(r.source)===keyPath(row.source))){const output=path.join(this.options.cacheRoot,'clips',this.artifactKey(s,{fingerprint:current.sourceFingerprint},current.clipIndex)+'.clipb');await fs.unlink(output).catch(e=>{if(e.code!=='ENOENT')throw e;});}if(keyPath(row.source)===keyPath(s.request.source)){s.base=await this.metadata(s.request.source,{force:true,signal:s.controller.signal});await fs.unlink(s.model.path+'.json').catch(()=>{});s.model=await this.model(s);this.emit(s,'state',this.state(s));}return this.startScan(s,{forceSource:row.source});}case 'regenerate-thumbnail':return {requiresHostAction:'regenerate-thumbnail',source:s.request.source,itemId:s.request.itemId};case 'rebuild-derived-cache':{const promise=this.rebuildDerivedCache(s);this.rebuilding=promise;try{return await promise;}finally{this.rebuilding=null;}}default:throw new Error('Unknown cache command');}}

 async rebuildDerivedCache(requesting){
  const sessions=[...this.sessions.values()];
  for(const s of sessions){s.controller.abort();s.scanController?.abort();clearTimeout(s.watchTimer);}
  await Promise.allSettled(sessions.map(s=>s.scanPromise).filter(Boolean));
  await Promise.allSettled([...this.operations].filter(o=>o.method!=='cache.command').map(o=>o.promise));
  await this.jobs.idle();
  this.clips.clear();this.persistedAssociations.clear();
  for(const s of sessions){s.rows=[];s.rowsPolicyKey=null;this.emit(s,'rows',this.rowEvent(s));}
  for(const library of new Set(sessions.map(s=>s.request.libraryRoot)))await this.publishVisibility(library);
  for(const dir of ['metadata','index','models','clips','sessions'])await fs.rm(path.join(this.options.cacheRoot,dir),{recursive:true,force:true});
  for(const s of sessions){if(s.closed)continue;s.controller=new AbortController();s.base=await this.metadata(s.request.source,{force:true,signal:s.controller.signal});s.model=await this.model(s);this.emit(s,'state',this.state(s));}
  await Promise.all(sessions.filter(s=>!s.closed&&s.request.discover!==false&&s.request.mode!=='thumbnail').map(s=>this.startScan(s)));
  return this.state(requesting);
 }
 async closeSession(id){const s=this.sessions.get(id);if(!s)return;s.closed=true;s.detachSignal?.();s.controller.abort();s.scanController?.abort();s.watcher?.close();clearTimeout(s.watchTimer);this.sessions.delete(id);if(this.libraryInitialized)await this.updateLibraryWatchers();await this.publishVisibility(s.request.libraryRoot);}
 async shutdown(){this.shutdownFlag=true;await Promise.allSettled([...(this.libraryInitializations||[])]);this.profileWatcher?.close();clearTimeout(this.profileTimer);clearInterval(this.libraryTimer);clearTimeout(this.libraryDebounce);for(const watcher of this.libraryWatchers?.values()||[])watcher.close();this.libraryWatchers?.clear();const scans=[...this.sessions.values()].map(s=>s.scanPromise).filter(Boolean);await Promise.all([...this.sessions.keys()].map(id=>this.closeSession(id)));await Promise.allSettled(scans);await Promise.allSettled([...this.operations].map(o=>o.promise));await this.jobs.idle();await Promise.allSettled([...(this.libraryPending||[])]);this.clips.clear();}
}
function validateClip(clip){if(clip?.version!==1||!Number.isFinite(clip.duration)||clip.duration<0||!Array.isArray(clip.tracks))throw new Error('Invalid normalized clip');for(const t of clip.tracks){if(!['translation','rotation','scale','weights'].includes(t.property)||!['LINEAR','STEP','CUBICSPLINE'].includes(t.interpolation)||!(Array.isArray(t.times)||t.times instanceof Float64Array)||!(Array.isArray(t.values)||t.values instanceof Float64Array)||!t.times.length||t.times.some((v,i)=>!Number.isFinite(v)||v<0||(i&&v<=t.times[i-1]))||t.values.some(v=>!Number.isFinite(v)))throw new Error('Invalid normalized animation track');const m=t.interpolation==='CUBICSPLINE'?3:1,w=t.property==='rotation'?4:t.property==='weights'?t.values.length/t.times.length/m:3;if(!Number.isInteger(w)||w<1||t.values.length!==t.times.length*m*w)throw new Error('Invalid animation track array lengths');}}
function encodeGlb({json,bin}){const bytes=Buffer.from(JSON.stringify(json)),j=Buffer.alloc(Math.ceil(bytes.length/4)*4,0x20);bytes.copy(j);const binLength=Math.ceil(bin.length/4)*4;const out=Buffer.alloc(12+8+j.length+(binLength?8+binLength:0));out.writeUInt32LE(0x46546c67,0);out.writeUInt32LE(2,4);out.writeUInt32LE(out.length,8);out.writeUInt32LE(j.length,12);out.writeUInt32LE(0x4e4f534a,16);j.copy(out,20);if(binLength){const o=20+j.length;out.writeUInt32LE(binLength,o);out.writeUInt32LE(0x004e4942,o+4);bin.copy(out,o+8);}return out;}
module.exports={createAnimationBrowserService,validateClip,encodeGlb,sameStat};
