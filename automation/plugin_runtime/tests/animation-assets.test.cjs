'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {createAnimationAssets}=require('../animation-browser-assets.cjs');
test('progressive image tokens serve exact original ranges, reject changed sources, and revoke with their model',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-progressive-assets-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const directory=path.join(root,'app'),cacheRoot=path.join(root,'cache'),libraryRoot=path.join(root,'library');fs.mkdirSync(cacheRoot);fs.mkdirSync(libraryRoot);const source=path.join(libraryRoot,'source.glb'),modelPath=path.join(cacheRoot,'geometry.glb'),materialsPath=modelPath+'.materials.gltf';fs.writeFileSync(source,'headerORIGINALIMAGEtail');fs.writeFileSync(modelPath,'geometry only');fs.writeFileSync(materialsPath,'{"images":[{"uri":"images/0"}]}');
 let handler;const assets=createAnimationAssets({directory,cacheRoot,protocol:{handle(_scheme,fn){handler=fn;},unhandle(){}}}),stat=fs.statSync(source),sourceFingerprint=Object.fromEntries(['size','mtimeMs','ctimeMs','dev','ino'].map(key=>[key,stat[key]]));
 const registration=assets.register({path:modelPath,materialsPath,progressiveMaterials:true,source,sourceFingerprint,imageResources:[{index:0,offset:6,length:13,mimeType:'image/png'}]},libraryRoot),imageUrl=new URL('images/0',registration.materialsUrl).href;
 const image=await handler({url:imageUrl,method:'GET'});assert.equal(image.status,200);assert.equal(image.headers.get('Content-Type'),'image/png');assert.equal(await image.text(),'ORIGINALIMAGE');assert.equal((await handler({url:registration.materialsUrl,method:'GET'})).status,200);
 assert.equal((await handler({url:new URL('images/1',registration.materialsUrl).href,method:'GET'})).status,409);assert.throws(()=>assets.register({path:modelPath,materialsPath,progressiveMaterials:true,source:path.join(root,'outside.glb'),sourceFingerprint,imageResources:[]},libraryRoot),/bound model/);
 fs.appendFileSync(source,'changed');assert.equal((await handler({url:imageUrl,method:'GET'})).status,409);assets.revoke(registration.token);assert.equal((await handler({url:imageUrl,method:'GET'})).status,404);assets.shutdown();
});
test('animation asset protocol exposes shipped UI and only registered cache output roots, then revokes them',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-animation-assets-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const directory=path.join(root,'app'),cacheRoot=path.join(root,'cache');fs.mkdirSync(path.join(directory,'ui'),{recursive:true});fs.mkdirSync(cacheRoot);fs.writeFileSync(path.join(directory,'ui','index.html'),'<script type="importmap">{}</script>');const model=path.join(cacheRoot,'model.glb');fs.writeFileSync(model,'test mesh');
 let handler;const assets=createAnimationAssets({directory,cacheRoot,protocol:{handle(_scheme,fn){handler=fn;},unhandle(){}}});
 const page=await handler({url:assets.entry,method:'GET'});assert.equal(page.status,200);assert.match(await page.text(),/<script nonce="[^"]+" type="importmap">/);assert.match(page.headers.get('Content-Security-Policy'),/default-src 'none'/);
 assert.throws(()=>assets.register({path:path.join(root,'outside.glb')}),/derived cache/);
 const token=assets.register({path:model});assert.equal(await (await handler({url:token.url,method:'GET'})).text(),'test mesh');
 assert.equal((await handler({url:'billfish-animation://app/backend/service.cjs',method:'GET'})).status,403);
 assert.equal((await handler({url:token.url,method:'POST'})).status,403);assets.revoke(token.token);assert.equal((await handler({url:token.url,method:'GET'})).status,404);assets.shutdown();
});

test('Core source tokens keep relative model dependencies inside the bound library and never serve library scripts',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-core-source-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));const directory=path.join(root,'app'),library=path.join(root,'library');fs.mkdirSync(path.join(library,'models'),{recursive:true});fs.mkdirSync(path.join(library,'textures'));fs.writeFileSync(path.join(library,'models','model.gltf'),'{}');fs.writeFileSync(path.join(library,'textures','base.png'),'texture');fs.writeFileSync(path.join(library,'models','untrusted.js'),'throw 1');let handler;
 const assets=createAnimationAssets({directory,cacheRoot:path.join(root,'cache'),protocol:{handle(_scheme,fn){handler=fn;},unhandle(){}}}),source=assets.registerSource(path.join(library,'models','model.gltf'),library);
 assert.equal((await handler({url:source.url,method:'GET'})).status,200);assert.equal(await(await handler({url:new URL('../textures/base.png',source.url).href,method:'GET'})).text(),'texture');assert.equal((await handler({url:new URL('untrusted.js',source.url).href,method:'GET'})).status,403);
 const tokenBase=source.url.slice(0,source.url.indexOf('/models/'));assert.equal((await handler({url:tokenBase+'/%2e%2e%2foutside.glb',method:'GET'})).status,403);assets.revoke(source.token);assert.equal((await handler({url:source.url,method:'GET'})).status,404);assets.shutdown();
});

test('an owned Core worker receives its exact script and worker-only CSP while the UI stays strict',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-core-worker-assets-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));fs.mkdirSync(path.join(root,'ui'));fs.writeFileSync(path.join(root,'ui','index.html'),'<html></html>');let handler;const assets=createAnimationAssets({directory:root,cacheRoot:path.join(root,'cache'),protocol:{handle(_scheme,fn){handler=fn;},unhandle(){}}});
 const worker=assets.reserveWorker(),source='self.onmessage = () => new Function("return 1")();',pending=handler({url:worker.url,method:'GET'});assets.completeWorker(worker.token,source);const reply=await pending;assert.equal(await reply.text(),source);assert.match(reply.headers.get('Content-Security-Policy'),/'unsafe-eval'/);
 const page=await handler({url:assets.entry,method:'GET'});assert.doesNotMatch(page.headers.get('Content-Security-Policy'),/'unsafe-eval'/);assert.throws(()=>assets.completeWorker('unowned',source),/Invalid owned Core worker/);assets.revoke(worker.token);assert.equal((await handler({url:worker.url,method:'GET'})).status,404);
 const cancelled=assets.reserveWorker(),waiting=handler({url:cancelled.url,method:'GET'});assets.revoke(cancelled.token);assert.equal((await waiting).status,404);assets.shutdown();
});
