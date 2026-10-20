'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),path=require('node:path'),os=require('node:os'),{EventEmitter}=require('node:events'),{PassThrough}=require('node:stream');
const {createLibraryCatalogReader}=require('../animation-browser-catalog.cjs');
const installRoot=path.resolve(os.tmpdir(),'billfish-catalog-install'),libraryRoot=path.resolve(os.tmpdir(),'billfish-catalog-library'),scopeRoot=path.join(libraryRoot,'model');
const snapshot=()=>({schemaVersion:2,libraryRoot,scopeRoot,scopeMode:'model-directory-recursive',complete:true,revision:'a'.repeat(64),items:[{id:'55',name:'角色.glb',folderId:'2',path:path.join(scopeRoot,'角色.glb'),relativePath:'角色.glb',exists:true,isLink:false,pathError:null}]});
function helper(responder=()=>({ok:true,data:snapshot()})){
 const observed={calls:[],requests:[],children:[],kills:0};
 observed.spawn=(...args)=>{observed.calls.push(args);const child=new EventEmitter();for(const name of ['stdin','stdout','stderr'])child[name]=new PassThrough();child.kill=()=>{observed.kills++;setImmediate(()=>child.emit('close',1));};let input='';child.stdin.on('data',chunk=>{input+=chunk.toString('utf8');let end;while((end=input.indexOf('\n'))>=0){const request=JSON.parse(input.slice(0,end));input=input.slice(end+1);observed.requests.push(request);const response=responder(request,child,observed);if(response!==undefined)queueMicrotask(()=>child.stdout.write(JSON.stringify({requestId:request.requestId,...response})+'\n'));}});observed.children.push(child);return child;};return observed;
}
const request=reader=>reader.getLibraryCandidates({libraryRoot,scopeRoot});
test('reader reuses one pinned read-only helper and validates every complete UTF-8 snapshot',async t=>{
 const probe=helper(),reader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:probe.spawn});t.after(()=>reader.shutdown());assert.equal((await request(reader)).items[0].name,'角色.glb');await request(reader);
 assert.equal(probe.calls.length,1);assert.deepEqual(probe.calls[0][1],['-I',path.join(installRoot,'automation','animation_catalog_helper.py'),libraryRoot]);assert.equal(probe.calls[0][2].windowsHide,true);assert.deepEqual(probe.requests.map(r=>r.scopeRoot),[scopeRoot,scopeRoot]);assert.notEqual(probe.requests[0].requestId,probe.requests[1].requestId);
 await assert.rejects(reader.getLibraryCandidates({libraryRoot:path.dirname(libraryRoot),scopeRoot}),{code:'invalid_library_context'});await assert.rejects(reader.getLibraryCandidates({libraryRoot}),{code:'invalid_animation_scope'});await assert.rejects(reader.getLibraryCandidates({libraryRoot,scopeRoot:path.dirname(libraryRoot)}),{code:'invalid_animation_scope'});assert.equal(probe.calls.length,1);
});
test('incomplete, foreign or duplicate snapshots fail rather than becoming empty results',async t=>{
 for(const data of [{...snapshot(),complete:false},{...snapshot(),scopeRoot:libraryRoot},{...snapshot(),libraryRoot:path.dirname(libraryRoot)},{...snapshot(),items:[{...snapshot().items[0],path:path.join(libraryRoot,'sibling.glb')}]},{...snapshot(),items:[...snapshot().items,...snapshot().items]}]){
  const probe=helper(()=>({ok:true,data})),reader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:probe.spawn});t.after(()=>reader.shutdown());await assert.rejects(request(reader),{code:'invalid_library_catalog'});
 }
});
test('typed query errors affect one request and do not restart the worker',async t=>{
 let count=0;const probe=helper(()=>++count===1?{ok:false,error:{code:'library_not_found',message:'Missing database'}}:{ok:true,data:snapshot()}),reader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:probe.spawn});t.after(()=>reader.shutdown());await assert.rejects(request(reader),{code:'library_not_found'});assert.equal((await request(reader)).complete,true);assert.equal(probe.calls.length,1);
});
test('aborting one request ignores its late reply while the concurrent request completes',async t=>{
 const probe=helper(()=>undefined),reader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:probe.spawn});t.after(()=>reader.shutdown());const controller=new AbortController(),first=reader.getLibraryCandidates({libraryRoot,scopeRoot,signal:controller.signal}),second=request(reader);controller.abort();await assert.rejects(first,{code:'ABORT_ERR'});
 for(const call of probe.requests)probe.children[0].stdout.write(JSON.stringify({requestId:call.requestId,ok:true,data:snapshot()})+'\n');assert.equal((await second).complete,true);assert.equal(probe.kills,0);
});
test('malformed output rejects all pending requests and only a new request starts a new worker',async t=>{
 const probe=helper(()=>undefined),reader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:probe.spawn});t.after(()=>reader.shutdown());const first=request(reader),second=request(reader),both=Promise.allSettled([first,second]);probe.children[0].stdout.write('{broken}\n');const results=await both;assert.ok(results.every(r=>r.status==='rejected'&&r.reason.code==='invalid_library_catalog'));assert.equal(probe.calls.length,1);
 const next=request(reader),call=probe.requests.at(-1);probe.children.at(-1).stdout.write(JSON.stringify({requestId:call.requestId,ok:true,data:snapshot()})+'\n');await next;assert.equal(probe.calls.length,2);
});
test('timeout, exit, output bound, request bound and shutdown reject explicitly',async t=>{
 const timed=helper(()=>undefined),timedReader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:timed.spawn,timeoutMs:15});t.after(()=>timedReader.shutdown());await assert.rejects(request(timedReader),{code:'library_catalog_timeout'});
 const exited=helper(()=>undefined),exitReader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:exited.spawn});t.after(()=>exitReader.shutdown());const exiting=request(exitReader);exited.children[0].emit('close',9);await assert.rejects(exiting,{code:'library_catalog_failed'});
 const large=helper(()=>undefined),largeReader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:large.spawn,maxOutputBytes:16});t.after(()=>largeReader.shutdown());const oversized=request(largeReader);large.children[0].stdout.write('x'.repeat(17));await assert.rejects(oversized,{code:'library_catalog_too_large'});
 const small=helper(),smallReader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:small.spawn,maxRequestBytes:16});t.after(()=>smallReader.shutdown());await assert.rejects(request(smallReader),{code:'library_catalog_request_too_large'});assert.equal(small.calls.length,0);
 const stopped=helper(()=>undefined),stoppedReader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:stopped.spawn});const pending=request(stoppedReader);stoppedReader.shutdown();await assert.rejects(pending,{code:'ABORT_ERR'});await assert.rejects(request(stoppedReader),{code:'ABORT_ERR'});
});
test('split multibyte UTF-8 and multiple responses in one chunk preserve request identities',async t=>{
 const probe=helper(()=>undefined),reader=createLibraryCatalogReader({installRoot,libraryRoot,spawnProcess:probe.spawn});t.after(()=>reader.shutdown());const first=request(reader),second=request(reader),body=Buffer.from(probe.requests.map(r=>JSON.stringify({requestId:r.requestId,ok:true,data:snapshot()})+'\n').join('')),cut=body.indexOf(Buffer.from('角'))+1;probe.children[0].stdout.write(body.subarray(0,cut));probe.children[0].stdout.write(body.subarray(cut));const results=await Promise.all([first,second]);assert.ok(results.every(r=>r.items[0].name==='角色.glb'));
});
