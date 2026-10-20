'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const {createAnimationBrowserService,encodeGlb}=require('../../animation_browser/backend/service.cjs');
const {readGlb}=require('../../animation_browser/backend/glb.cjs');
const {ProfileStore}=require('../../animation_browser/backend/profiles.cjs');
const {createFixtureCatalog}=require('./fake-catalog.cjs');
const fixtures=path.resolve('task-memory/task-billfish-animation-browser/assets/fixtures');

async function setup(t){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'billfish-library-mode-')),library=path.join(root,'library'),mesh=path.join(library,'characters','base.glb'),animation=path.join(library,'characters','animations','walk.glb'),catalog=createFixtureCatalog(),services=[];
 await fs.mkdir(path.dirname(mesh),{recursive:true});await fs.mkdir(path.dirname(animation),{recursive:true});
 await fs.copyFile(path.join(fixtures,'humanoid','character.glb'),mesh);await fs.copyFile(path.join(fixtures,'humanoid','animations','RootMotion.glb'),animation);
 catalog.register(library,mesh,'123');catalog.register(library,animation,'201');
 const profileRoot=path.join(library,'.bf','profiles'),make=(extra={})=>{const service=createAnimationBrowserService({cacheRoot:path.join(root,'cache'),profileRoot,parserPath:path.join(root,'unused.exe'),getLibraryCandidates:catalog.getLibraryCandidates,...extra});services.push(service);return service;};
 t.after(async()=>{for(const service of services)await service.shutdown();await fs.rm(root,{recursive:true,force:true});});
 return {root,library,mesh,animation,catalog,profileRoot,make};
}
async function open(service,library,source,sessionId='session',itemId='123'){
 await service.openSession({sessionId,source,libraryRoot:library,itemId});await service.sessions.get(sessionId).scanPromise;return service.state(service.sessions.get(sessionId));
}

test('catalog members in descendant folders are included while unindexed neighbors and obsolete external scope are ignored; manual preferences survive',async t=>{
 const {root,library,mesh,animation,catalog,profileRoot,make}=await setup(t),neighbor=path.join(path.dirname(mesh),'unindexed-neighbor.glb'),obsolete=path.join(root,'old-external-scope');
 await fs.copyFile(animation,neighbor);await fs.mkdir(obsolete);await fs.copyFile(animation,path.join(obsolete,'external.glb'));
 const key=JSON.stringify({kind:'mesh',itemId:'123',source:null});await new ProfileStore(profileRoot).update(key,{scope:obsolete,renderFacing:{yaw:45},listSettings:{sort:'duration'}});
 const service=make(),state=await open(service,library,mesh);
 assert.equal(state.rows.length,1);assert.equal(state.rows[0].source,animation);assert.equal(state.rows[0].origin,'library-animation');assert.equal(state.rows[0].sourceItemId,'201');
 assert.equal(state.profile.renderFacing.yaw,45);assert.equal(state.listSettings.sort,'duration');assert.equal(state.profile.scope,undefined);assert.ok(!state.rows.some(row=>row.source===neighbor||row.source.startsWith(obsolete)));
 await service.closeSession('session');const reopened=await open(service,library,mesh,'reopened');assert.equal(reopened.profile.renderFacing.yaw,45);assert.equal(reopened.listSettings.sort,'duration');assert.equal(reopened.rows.length,1);
 assert.equal((await catalog.getLibraryCandidates({libraryRoot:library,scopeRoot:library})).items.length,2);
});

test('a non-skinned Mesh exposes only its own embedded clips and lazy clip playback',async t=>{
 const {library,mesh,catalog,make}=await setup(t),document=await readGlb(path.join(fixtures,'morph','character-with-morph-clip.glb'));
 for(const node of document.json.nodes||[])delete node.skin;delete document.json.skins;
 for(const primitive of (document.json.meshes||[]).flatMap(mesh=>mesh.primitives||[])){delete primitive.attributes.JOINTS_0;delete primitive.attributes.WEIGHTS_0;}
 assert.ok(document.json.animations?.length);await fs.writeFile(mesh,encodeGlb(document));
 const service=make(),state=await open(service,library,mesh);
 const output=await readGlb(state.model.path);assert.ok(output.json.meshes.length);assert.ok(state.rows.length>=1);assert.ok(state.rows.every(row=>row.origin==='embedded'&&row.sourceItemId==='123'&&row.source===mesh));
 const loaded=await service.call('session','clip.load',{rowId:state.rows[0].id});assert.ok(loaded.clip.tracks.length);
 assert.equal((await catalog.getLibraryCandidates({libraryRoot:library,scopeRoot:library})).items.length,2);
});

test('a skinned Mesh automatically exposes compatible indexed library animations',async t=>{
 const {library,mesh,animation,make}=await setup(t),service=make(),state=await open(service,library,mesh);
 assert.equal(state.rows.length,1);assert.equal(state.rows[0].compatibility,'full');assert.equal(state.rows[0].origin,'library-animation');assert.equal(state.rows[0].sourceItemId,'201');assert.equal(state.rows[0].source,animation);
 const clip=await service.call('session','clip.load',{rowId:state.rows[0].id});assert.ok(clip.clip.tracks.length);assert.deepEqual(await service.publishVisibility(library),[animation]);
});

test('removing catalog membership rejects a previously loaded clip and unhides the still-existing file',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t),service=make(),state=await open(service,library,mesh),row=state.rows[0];
 await service.call('session','clip.load',{rowId:row.id});assert.deepEqual(await service.publishVisibility(library),[animation]);
 catalog.remove(library,'201');assert.equal((await fs.stat(animation)).isFile(),true);
 await assert.rejects(service.call('session','clip.load',{rowId:row.id}));assert.deepEqual(await service.publishVisibility(library),[]);
 const refreshed=await service.call('session','scan.refresh');assert.ok(refreshed.model.path);assert.equal(refreshed.rows.length,0);
});

test('a Mesh without any catalog animation keeps its model and an empty animation list',async t=>{
 const {library,mesh,catalog,make}=await setup(t);catalog.remove(library,'201');
 const service=make(),state=await open(service,library,mesh);assert.ok(state.model.path);assert.equal(state.rows.length,0);assert.ok((await readGlb(state.model.path)).json.meshes.length>0);
});

test('base native item ID and source path must identify the same current catalog member',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t),service=make();
 catalog.rename(library,'123',path.join(library,'characters','renamed.glb'));
 await assert.rejects(open(service,library,mesh));
 catalog.rename(library,'123',mesh);catalog.remove(library,'123');assert.equal((await fs.stat(mesh)).isFile(),true);
 await assert.rejects(open(service,library,mesh,'missing'));
 await assert.rejects(open(service,library,animation,'wrong-id','123'));
});

test('missing or failed catalog providers cannot fall back to walking neighboring files',async t=>{
 const {library,mesh,make}=await setup(t);
 const missing=make({getLibraryCandidates:undefined});await assert.rejects(open(missing,library,mesh));
 const failed=make({getLibraryCandidates:async()=>{throw Object.assign(new Error('Catalog unavailable'),{code:'CATALOG_UNAVAILABLE'});}});await assert.rejects(open(failed,library,mesh,'failed'));
});

test('current subtree is the default, parent expands exactly once, and reopening cannot revive parent cache rows or hiding',async t=>{
 const {root,library,mesh,animation,catalog,make}=await setup(t),folder=path.join(library,'characters','FBX'),base=path.join(folder,'SK_Dog.glb'),child=path.join(folder,'motions','AS_Run.glb'),sibling=path.join(library,'characters','Animations','DifferentName.glb'),outside=path.join(library,'other','invalid.glb');
 for(const file of [base,child,sibling,outside])await fs.mkdir(path.dirname(file),{recursive:true});await fs.rename(mesh,base);await fs.rename(animation,child);await fs.copyFile(child,sibling);await fs.writeFile(outside,'invalid bytes must never be inspected');
 catalog.rename(library,'123',base);catalog.rename(library,'201',child);catalog.register(library,sibling,'202');catalog.register(library,outside,'203');
 const service=make(),state=await open(service,library,base);assert.equal(state.libraryContext.candidateScope,'current');assert.equal(state.libraryContext.scopeRoot,folder);assert.equal(state.scan.total,2);assert.deepEqual(state.rows.map(row=>row.source),[child]);
 const parent=await service.call('session','candidateScope.set',{mode:'parent'});assert.equal(parent.libraryContext.scopeRoot,path.dirname(folder));assert.equal(parent.scan.total,3);assert.equal(parent.scan.errorCount,0);assert.deepEqual(new Set(parent.rows.map(row=>row.source)),new Set([child,sibling]));
 const siblingRow=parent.rows.find(row=>row.source===sibling);await service.call('session','clip.load',{rowId:siblingRow.id});assert.deepEqual(new Set(await service.publishVisibility(library)),new Set([child,sibling]));
 const repeated=await service.call('session','candidateScope.set',{mode:'parent'});assert.equal(repeated.libraryContext.scopeRoot,path.dirname(folder));await assert.rejects(service.call('session','candidateScope.set',{mode:'parent',scopeRoot:library}),{code:'INVALID_ANIMATION_SCOPE'});
 const current=await service.call('session','candidateScope.set',{mode:'current'});assert.deepEqual(current.rows.map(row=>row.source),[child]);assert.equal(current.scan.total,2);await assert.rejects(service.call('session','clip.load',{rowId:siblingRow.id}));assert.deepEqual(await service.publishVisibility(library),[child]);
 await service.call('session','candidateScope.set',{mode:'parent'});await service.closeSession('session');const reopened=await open(service,library,base,'reopened');assert.equal(reopened.libraryContext.candidateScope,'current');assert.deepEqual(reopened.rows.map(row=>row.source),[child]);assert.deepEqual(await service.publishVisibility(library),[child]);assert.ok(service.stats.metadataHits>0);
 await service.shutdown();const restarted=make(),initial=await restarted.initializeLibrary(library);assert.deepEqual(initial.hiddenPaths,[child]);assert.ok(!initial.hiddenPaths.includes(sibling));
});

test('moving a model with its native ID derives a new directory and cannot reuse the old directory rows',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t),service=make();await open(service,library,mesh);await service.closeSession('session');const moved=path.join(library,'moved','base.glb');await fs.mkdir(path.dirname(moved));await fs.rename(mesh,moved);catalog.rename(library,'123',moved);
 const reopened=await open(service,library,moved,'moved');assert.equal(reopened.libraryContext.scopeRoot,path.dirname(moved));assert.equal(reopened.scan.total,1);assert.deepEqual(reopened.rows,[]);assert.deepEqual(await service.publishVisibility(library),[]);assert.equal((await fs.stat(animation)).isFile(),true);
});

test('a late parent catalog response is cancelled and cannot publish sibling hiding after switching back to current',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t),sibling=path.join(library,'sibling.glb');await fs.copyFile(animation,sibling);catalog.register(library,sibling,'202');let release,started,delay=false;const entered=new Promise(resolve=>started=resolve),gate=new Promise(resolve=>release=resolve),publications=[];
 const service=make({getLibraryCandidates:async request=>{if(delay&&request.scopeRoot===library){delay=false;started();await gate;}return catalog.getLibraryCandidates(request);},onVisibilityChanged:(_root,paths)=>publications.push(paths)});await open(service,library,mesh);delay=true;const parent=service.call('session','candidateScope.set',{mode:'parent'});await entered;const current=await service.call('session','candidateScope.set',{mode:'current'});const boundary=publications.length;release();await parent;
 assert.equal(current.libraryContext.candidateScope,'current');assert.ok(publications.slice(boundary).every(paths=>!paths.includes(sibling)));assert.deepEqual(service.state(service.sessions.get('session')).rows.map(row=>row.source),[animation]);
});

test('old whole-library session records cannot restore sibling rows or native hiding',async t=>{
 const {root,library,mesh,make}=await setup(t),seed=make();await open(seed,library,mesh);await seed.shutdown();const directory=path.join(root,'cache','sessions'),files=await fs.readdir(directory);assert.equal(files.length,1);const file=path.join(directory,files[0]),record=JSON.parse(await fs.readFile(file,'utf8'));record.sessionSchema=4;record.candidatePolicy='billfish-catalog-1';await fs.writeFile(file,JSON.stringify(record));
 const service=make(),initial=await service.initializeLibrary(library);assert.equal(initial.restoredTargets,0);assert.deepEqual(initial.hiddenPaths,[]);const reopened=await open(service,library,mesh,'reopened');assert.equal(reopened.libraryContext.candidateScope,'current');assert.equal(reopened.scan.total,2);
});

test('a late parent visibility response cannot hide sibling animations after its viewer closes',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t),sibling=path.join(library,'sibling.glb');await fs.copyFile(animation,sibling);catalog.register(library,sibling,'202');let release,started,delay=false;const entered=new Promise(resolve=>started=resolve),gate=new Promise(resolve=>release=resolve),publications=[];
 const service=make({getLibraryCandidates:async request=>{if(delay&&request.scopeRoot===library){delay=false;started();await gate;}return catalog.getLibraryCandidates(request);},onVisibilityChanged:(_root,paths)=>publications.push(paths)});await open(service,library,mesh);await service.call('session','candidateScope.set',{mode:'parent'});delay=true;const publishing=service.publishVisibility(library);await entered;await service.closeSession('session');const boundary=publications.length;release();await publishing;
 assert.equal(service.sessions.size,0);assert.ok(publications.slice(boundary).every(paths=>!paths.includes(sibling)));assert.deepEqual(publications.at(-1),[animation]);
});

test('warm reopen and periodic reconciliation verify every candidate without rescanning unchanged files',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t),zero=path.join(path.dirname(animation),'zero.glb');await fs.copyFile(mesh,zero);catalog.register(library,zero,'202');
 const service=make({libraryReconcileIntervalMs:600000});let scans=0;const scan=service.scan.bind(service);service.scan=(...args)=>{scans++;return scan(...args);};
 let state=await open(service,library,mesh);assert.equal(state.rows.length,1);assert.equal(scans,1);const s=service.sessions.get('session');assert.equal(s.scanReceipt.files.length,3);
 await service.closeSession('session');state=await open(service,library,mesh,'reopened');assert.equal(scans,1);assert.equal(state.scan.state,'complete');assert.equal(state.rows.length,1);
 service.queueLibraryReconciliation();await Promise.all([...service.libraryPending]);assert.equal(scans,1);
 await fs.copyFile(animation,zero);service.queueLibraryReconciliation();await Promise.all([...service.libraryPending]);assert.equal(scans,2);assert.equal(service.state(service.sessions.get('reopened')).rows.length,2);
});
test('each independent warm clip selection reads one fresh catalog and still rejects changed source bytes',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t);let snapshots=0;const service=make({getLibraryCandidates:async args=>{snapshots++;return catalog.getLibraryCandidates(args);},libraryReconcileIntervalMs:600000});
 const state=await open(service,library,mesh),s=service.sessions.get('session'),row=state.rows[0];await service.loadClip(s,row.id);const before=snapshots;await service.loadClip(s,row.id);assert.equal(snapshots-before,1);
 await fs.appendFile(animation,Buffer.from('changed'));await assert.rejects(service.loadClip(s,row.id),{code:'SOURCE_CHANGED'});
});
test('a verified partial row is selectable before a later candidate finishes, with no incomplete hiding or receipt',async t=>{
 const {library,mesh,animation,catalog,make}=await setup(t),later=path.join(path.dirname(animation),'later.glb');await fs.copyFile(animation,later);catalog.register(library,later,'202');
 let release,partial;const blocked=new Promise(resolve=>release=resolve),ready=new Promise(resolve=>partial=resolve);const service=make({concurrency:1,onEvent:(id,event)=>{if(event.type==='rows'&&event.rows.length&&service.sessions.get(id).scan.state==='scanning')partial(event);}}),metadata=service.metadata.bind(service);service.metadata=async(source,options)=>{if(source===later)await blocked;return metadata(source,options);};
 await service.openSession({sessionId:'session',source:mesh,libraryRoot:library,itemId:'123'});const event=await ready,s=service.sessions.get('session');assert.equal(s.scan.state,'scanning');assert.equal(s.scanReceipt,null);assert.equal(event.rows.every(row=>!row.hidden),true);const clip=await service.loadClip(s,event.rows[0].id);assert.ok(clip.clip);release();await s.scanPromise;assert.equal(s.scan.state,'complete');assert.equal(s.scanReceipt.files.length,3);
});

test('closing a viewer during reconciliation receipt verification cannot restart its scan',async t=>{
 const {library,mesh,make}=await setup(t),service=make({libraryReconcileIntervalMs:600000});await open(service,library,mesh);for(const watcher of service.libraryWatchers.values())watcher.close();service.libraryWatchers.clear();service.profileWatcher?.close();clearTimeout(service.libraryDebounce);
 let release,entered;const blocked=new Promise(resolve=>release=resolve),ready=new Promise(resolve=>entered=resolve),validate=service.scanReceiptValid.bind(service);service.scanReceiptValid=async(...args)=>{entered();await blocked;return validate(...args);};let starts=0;const scan=service.startScan.bind(service);service.startScan=(...args)=>{starts++;return scan(...args);};
 service.queueLibraryReconciliation();await ready;await service.closeSession('session');release();await Promise.all([...service.libraryPending]);assert.equal(starts,0);assert.equal(service.sessions.size,0);
});
test('incomplete scans never persist associations or hide successfully inspected animation files',async t=>{
 const {root,library,mesh,animation,catalog,make}=await setup(t),bad=path.join(path.dirname(animation),'bad.glb');await fs.writeFile(bad,'invalid');catalog.register(library,bad,'202');const hidden=[],service=make({onVisibilityChanged:(_library,paths)=>hidden.push(paths)});let state=await open(service,library,mesh);const s=service.sessions.get('session');assert.equal(state.scan.errorCount,1);assert.equal(s.scanReceipt,null);assert.equal(hidden.at(-1).includes(animation),false);await service.updateProfile(s,{renderFacing:{yaw:20}});assert.equal(service.persistedAssociations.size,0);assert.equal((await fs.readdir(path.join(root,'cache','sessions')).catch(()=>[])).length,0);
});

test('a persisted model profile change reconciles immediately without a viewer or periodic timer',async t=>{
 const {library,mesh,animation,make}=await setup(t),hidden=[],service=make({onVisibilityChanged:(_root,paths)=>hidden.push(paths),libraryReconcileIntervalMs:600000});await open(service,library,mesh);const key=service.sessions.get('session').key;await service.closeSession('session');clearInterval(service.libraryTimer);service.profileWatcher?.close();clearTimeout(service.profileTimer);assert.equal(hidden.at(-1).includes(animation),true);await service.profiles.update(key,{visibility:{showAll:true}});await service.refreshProfiles();assert.equal(hidden.at(-1).includes(animation),false);
});

test('late close visibility publication safely overlaps a new session before its profile initializes',async t=>{
 const {library,mesh,make}=await setup(t),service=make();await open(service,library,mesh);let releaseClose,enterClose,releaseModel,enterModel;const closeGate=new Promise(resolve=>releaseClose=resolve),closeReady=new Promise(resolve=>enterClose=resolve),modelGate=new Promise(resolve=>releaseModel=resolve),modelReady=new Promise(resolve=>enterModel=resolve),read=service.readCatalog.bind(service),metadata=service.metadata.bind(service);let closing=true;
 service.readCatalog=async(...args)=>{if(closing){closing=false;enterClose();await closeGate;}return read(...args);};service.metadata=async(...args)=>{enterModel();await modelGate;return metadata(...args);};const closed=service.closeSession('session');await closeReady;const reopened=service.openSession({sessionId:'new',source:mesh,libraryRoot:library,itemId:'123'});await modelReady;assert.equal(service.sessions.get('new').profile,undefined);releaseClose();await closed;releaseModel();await reopened;await service.sessions.get('new').scanPromise;
});

test('an unavailable persisted model cannot cause unchanged profile refreshes to repeatedly publish visibility',async t=>{
 const {library,mesh,make}=await setup(t),service=make({libraryReconcileIntervalMs:600000});await open(service,library,mesh);const key=service.sessions.get('session').key;await service.closeSession('session');service.profileWatcher?.close();clearTimeout(service.profileTimer);clearTimeout(service.libraryDebounce);for(const watcher of service.libraryWatchers.values())watcher.close();service.libraryWatchers.clear();await fs.unlink(mesh);await service.profiles.update(key,{visibility:{showAll:true}});let publications=0;const publish=service.publishVisibility.bind(service);service.publishVisibility=(...args)=>{publications++;return publish(...args);};await service.refreshProfiles();const changed=publications;assert.equal(changed,1);await service.refreshProfiles();assert.equal(publications,changed);
});

test('renderer cached-key acknowledgement stays fresh without reading a decoded payload',async t=>{
 const {library,mesh,animation,make}=await setup(t),service=make();const state=await open(service,library,mesh),s=service.sessions.get('session'),row=state.rows[0],loaded=await service.call('session','clip.load',{rowId:row.id});service.clips.clear();await fs.unlink(path.join(service.options.cacheRoot,'clips',loaded.cacheKey+'.clipb'));const ack=await service.call('session','clip.load',{rowId:row.id,cachedKey:loaded.cacheKey});assert.equal(ack.reused,true);assert.equal(ack.clip,null);assert.equal(ack.cacheKey,loaded.cacheKey);assert.equal(ack.source,animation);await fs.appendFile(animation,Buffer.from('changed'));await assert.rejects(service.call('session','clip.load',{rowId:row.id,cachedKey:loaded.cacheKey}),{code:'SOURCE_CHANGED'});
});

test('an aborted initialization waiter cannot register a late viewer session',async t=>{
 const {library,mesh,make}=await setup(t),service=make();let release,entered;const gate=new Promise(resolve=>release=resolve),ready=new Promise(resolve=>entered=resolve),initialize=service.initializeLibrary.bind(service);let calls=0;service.initializeLibrary=async(...args)=>{if(++calls===1){entered();await gate;}return initialize(...args);};const controller=new AbortController(),pending=service.openSession({sessionId:'old',source:mesh,libraryRoot:library,itemId:'123',discover:false},{signal:controller.signal}).then(value=>({value}),error=>({error}));await ready;controller.abort();await service.openSession({sessionId:'new',source:mesh,libraryRoot:library,itemId:'123',discover:false});release();assert.equal((await pending).error.code,'ABORT_ERR');assert.equal(service.sessions.has('old'),false);assert.equal(service.sessions.has('new'),true);
});
test('a late cancelled open cannot close a newer session with the same ID',async t=>{
 const {library,mesh,make}=await setup(t),service=make();let release,entered;const gate=new Promise(resolve=>release=resolve),ready=new Promise(resolve=>entered=resolve),metadata=service.metadata.bind(service);let calls=0;service.metadata=async(...args)=>{const data=await metadata(...args);if(++calls===1){entered();await gate;}return data;};const controller=new AbortController(),pending=service.openSession({sessionId:'same',source:mesh,libraryRoot:library,itemId:'123',discover:false},{signal:controller.signal}).then(value=>({value}),error=>({error}));await ready;controller.abort();await service.openSession({sessionId:'same',source:mesh,libraryRoot:library,itemId:'123',discover:false});const current=service.sessions.get('same');release();assert.equal((await pending).error.code,'ABORT_ERR');assert.equal(service.sessions.get('same'),current);assert.equal(current.closed,false);
});
