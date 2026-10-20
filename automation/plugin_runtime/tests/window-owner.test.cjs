'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),{EventEmitter}=require('node:events');
const {createWindowOwnerBridge}=require('../window-owner.cjs');
function windowFixture(){const window=new EventEmitter();window.id=7;window.isDestroyed=()=>false;const handle=Buffer.alloc(8);handle.writeBigUInt64LE(0x123456789abcdef0n);window.getNativeWindowHandle=()=>handle;window.events=[];window.webContents={send:event=>window.events.push(event)};return window;}
test('native window ownership preserves the full HWND and requires matching native confirmation',async()=>{
 const window=windowFixture(),messages=[],bridge=createWindowOwnerBridge({required:true,send:value=>messages.push(value)});
 const pending=bridge.attach(window,'viewer',true);assert.equal(bridge.isAttached(window),false);
 const message=messages[0];assert.equal(message.hwnd,'1311768467463790320');assert.equal(message.kind,'viewer');assert.equal(message.background,true);
 bridge.receive({type:'window_owner_result',request_id:message.request_id,data:{attached:true,owner_hwnd:'123',window_hwnd:message.hwnd,owner_topmost:false}});
 const result=await pending;assert.equal(result.attached,true);assert.equal(bridge.isAttached(window),true);assert.ok(window.events.includes('billfish:window-owned'));
 const mismatch=bridge.attach(window,'main',false),next=messages[1];assert.equal(bridge.isAttached(window),false);bridge.receive({request_id:next.request_id,data:{attached:true,owner_hwnd:'123',window_hwnd:'999'}});await assert.rejects(mismatch,{code:'invalid_window_owner_response'});assert.equal(bridge.isAttached(window),false);
});
test('closed windows and native ownership failures reject instead of showing unowned windows',async()=>{
 const window=windowFixture(),messages=[],bridge=createWindowOwnerBridge({required:true,send:value=>messages.push(value)});
 const pending=bridge.attach(window,'main',false);window.emit('closed');await assert.rejects(pending,{code:'plugin_window_closed'});
 const failure=bridge.attach(window,'viewer',false);bridge.receive({request_id:messages[1].request_id,error:{code:'window_owner_unavailable',message:'Owner was closed'}});await assert.rejects(failure,{code:'window_owner_unavailable'});
 bridge.shutdown();await assert.rejects(bridge.attach(window,'viewer',false),{code:'plugin_disabled'});
});
test('standalone renderer probes do not claim an attached Billfish owner',async()=>{
 const bridge=createWindowOwnerBridge({required:false,send:()=>{throw new Error('Unexpected native request')}});
 const result=await bridge.attach(windowFixture(),'viewer',true);assert.deepEqual(result,{attached:false,standalone:true});
});
