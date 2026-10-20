'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {fingerprintFile,createPreviewCoverBridge}=require('../preview-cover.cjs'),{inspectPng}=require('../preview-runtime.cjs');
test('source fingerprint covers every byte across multiple stream chunks',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-cover-hash-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));const source=path.join(root,'source.glb'),bytes=Buffer.alloc(2*1024*1024+17);for(let index=0;index<bytes.length;index++)bytes[index]=(index*31+17)%256;fs.writeFileSync(source,bytes);const expected=require('node:crypto').createHash('sha256').update(bytes).digest('hex'),actual=await fingerprintFile(source);assert.equal(actual.sha256,expected);assert.equal(actual.bytes,String(bytes.length));assert.equal(Object.isFrozen(actual),true);
});
const png=Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a0S8AAAAASUVORK5CYII=','base64'),pose={clip_index:1,clip_name:'Turn',time:.5,fps:30};
async function fixture(t){const root=fs.mkdtempSync(path.join(os.tmpdir(),'billfish-pose-cover-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));const source=path.join(root,'model.glb');fs.writeFileSync(source,'original model');const owner={request:{kind:'viewer',item:{id:'17'},source},sourceFingerprint:await fingerprintFile(source)};const nativeImage={createFromPath:()=>({isEmpty:()=>false,getSize:()=>({width:1,height:1})})};return {root,source,owner,nativeImage,stagingRoot:path.join(root,'staging'),inspectPng};}
function confirmed(message){return {completion:'native_custom_cover_verified',item_id:'17',cover_path:'X:/library/.bf/cover/17.png',cover_tid:'2',image:{width:1,height:1,codec:'PNG'},source_unchanged:true,metadata_unchanged:true,pose:message.pose};}
test('selected-frame requests bind trusted item/source and the load-time immutable identity',async t=>{
 const f=await fixture(t);fs.writeFileSync(f.source,'replaced model');let sent,bridge;
 bridge=createPreviewCoverBridge({...f,send:message=>{sent=message;setImmediate(()=>bridge.receive({request_id:message.request_id,data:confirmed(message)}));}});
 await bridge.publish(f.owner,{png,pose,item:{id:'999',path:'X:/forged'},source:{sha256:'0'.repeat(64)},dest:'X:/forged.png'});
 assert.deepEqual(sent.item,{id:'17',path:f.source});assert.deepEqual(sent.source,f.owner.sourceFingerprint);assert.notEqual(sent.source.sha256,(await fingerprintFile(f.source)).sha256);assert.equal(Object.isFrozen(f.owner.sourceFingerprint),true);assert.equal(fs.existsSync(sent.dest),false);
});
test('native failure or timeout preserves staging that an asynchronous native operation may still use',async t=>{
 const f=await fixture(t);let sent,bridge;bridge=createPreviewCoverBridge({...f,send:message=>{sent=message;setImmediate(()=>bridge.receive({request_id:message.request_id,error:{code:'native_cover_failed',message:'native failed'}}));}});
 await assert.rejects(bridge.publish(f.owner,{png,pose}),error=>error.code==='native_cover_failed'&&error.details.staging_path===sent.dest);assert.equal(fs.existsSync(sent.dest),true);
 const timeout=createPreviewCoverBridge({...f,timeoutMs:20,send:message=>{sent=message;}});await assert.rejects(timeout.publish(f.owner,{png,pose}),{code:'preview_cover_timeout'});assert.equal(fs.existsSync(sent.dest),true);
});
test('incomplete native confirmations never claim a persistent cover and keep their staged frame',async t=>{
 const f=await fixture(t);let sent,bridge;bridge=createPreviewCoverBridge({...f,send:message=>{sent=message;setImmediate(()=>bridge.receive({request_id:message.request_id,data:{...confirmed(message),source_unchanged:false}}));}});
 await assert.rejects(bridge.publish(f.owner,{png,pose}),{code:'cover_not_confirmed'});assert.equal(fs.existsSync(sent.dest),true);
});
test('invalid frames and a session stopped during staging never enter the native queue',async t=>{
 const f=await fixture(t);let count=0;const bridge=createPreviewCoverBridge({...f,send:()=>count++});await assert.rejects(bridge.publish(f.owner,{png:Buffer.alloc(40),pose}),{code:'invalid_thumbnail_output'});assert.equal(count,0);
 const pending=bridge.publish(f.owner,{png,pose});bridge.shutdown();await assert.rejects(pending,{code:'plugin_disabled'});assert.equal(count,0);
});

test('built-in base cover and external animation cover require the exact trusted native source identity',async t=>{
 const f=await fixture(t);f.owner.builtin=true;
 const animation=path.join(f.root,'walk.fbx');fs.writeFileSync(animation,'external animation');
 f.owner.currentClip={rowId:'walk-row',source:animation,clipIndex:2,nativeFingerprint:await fingerprintFile(animation)};
 let sent,bridge;bridge=createPreviewCoverBridge({...f,send:message=>{sent=message;setImmediate(()=>bridge.receive({request_id:message.request_id,data:{...confirmed(message),...(message.animation_source?{animation_source:{fingerprint:{mtime_ns:message.animation_source.fingerprint.mtime_ns,bytes:message.animation_source.fingerprint.bytes,sha256:message.animation_source.fingerprint.sha256},rowId:message.animation_source.rowId,clip_index:message.animation_source.clip_index,path:message.animation_source.path}}:{})}}));}});
 await bridge.publish(f.owner,{png,pose:{kind:'base',time:0}});assert.equal(sent.animation_source,undefined);
 const clipPose={kind:'clip',rowId:'walk-row',clip_index:2,clip_name:'Walk',time:0.5,fps:null};
 await bridge.publish(f.owner,{png,pose:clipPose,animation_source:{path:'forged'}});assert.equal(sent.animation_source.path,animation);assert.deepEqual(sent.animation_source.fingerprint,f.owner.currentClip.nativeFingerprint);
 await assert.rejects(bridge.publish(f.owner,{png,pose:{...clipPose,rowId:'another-row'}}),{code:'invalid_arguments'});
 bridge=createPreviewCoverBridge({...f,send:message=>setImmediate(()=>bridge.receive({request_id:message.request_id,data:{...confirmed(message),animation_source:{...message.animation_source,path:'forged'}}}))});
 await assert.rejects(bridge.publish(f.owner,{png,pose:clipPose}),{code:'cover_not_confirmed'});
});

test('source fingerprinting can abort its active stream without changing cover callers',async t=>{
 const f=await fixture(t),controller=new AbortController(),original=fs.createReadStream;let stream;fs.writeFileSync(f.source,Buffer.alloc(128*1024,7));fs.createReadStream=(file,options)=>{stream=original(file,{...options,highWaterMark:16});stream.once('data',()=>controller.abort());return stream;};try{await assert.rejects(fingerprintFile(f.source,{signal:controller.signal}),error=>error.code==='ABORT_ERR');assert.equal(stream.destroyed,true);}finally{fs.createReadStream=original;}const result=await fingerprintFile(f.source);assert.equal(result.bytes,String(128*1024));
});
