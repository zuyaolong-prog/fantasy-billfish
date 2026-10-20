import test from 'node:test';
import assert from 'node:assert/strict';
import {PerspectiveCamera,Vector3} from '../../vendor/three/build/three.module.js';
import {WalkControls} from '../../ui/walk-controls.mjs';
function fire(target,type){target.dispatchEvent(new Event(type));}
function fixture(request){
 const document=new EventTarget(),window=new EventTarget(),element=new EventTarget(),requests=[],errors=[];
 document.defaultView=window;document.pointerLockElement=null;element.ownerDocument=document;element.focus=()=>{};
 element.requestPointerLock=options=>{requests.push(options);return request?.(options);};
 document.exitPointerLock=()=>{document.pointerLockElement=null;fire(document,'pointerlockchange');};
 const camera=new PerspectiveCamera();camera.position.set(0,0,10);camera.lookAt(0,0,0);
 const controls={enabled:true,enableDamping:true,target:new Vector3(),update(){}};
 const walk=new WalkControls({camera,controls,element,onError:error=>errors.push(error)});
 return{document,element,camera,controls,walk,requests,errors,lock(){document.pointerLockElement=element;fire(document,'pointerlockchange');}};
}
const unsupported=()=>Object.assign(new Error('The options asked for are not supported on this platform'),{name:'NotSupportedError'});

test('enter requests Raw Input in the original gesture and waits for actual Pointer Lock',async()=>{
 const f=fixture(()=>Promise.resolve()),position=f.camera.position.clone(),quaternion=f.camera.quaternion.toArray();
 assert.equal(f.walk.enter(),true);assert.deepEqual(f.requests,[{unadjustedMovement:true}]);assert.equal(f.walk.pending,true);assert.equal(f.walk.active,false);assert.equal(f.controls.enabled,true);
 await Promise.resolve();assert.equal(f.walk.active,false);f.lock();assert.equal(f.walk.active,true);assert.equal(f.controls.enabled,false);assert.deepEqual(f.camera.position,position);assert.deepEqual(f.camera.quaternion.toArray(),quaternion);f.walk.dispose();
});
test('unsupported Raw Input fails visibly without retrying adjusted input',async()=>{
 const f=fixture(()=>Promise.reject(unsupported()));f.walk.enter();await Promise.resolve();
 assert.equal(f.walk.pending,false);assert.equal(f.walk.active,false);assert.equal(f.controls.enabled,true);assert.equal(f.requests.length,1);assert.match(f.errors[0].message,/不支持.*Raw Input/);
 fire(f.document,'pointerlockerror');assert.equal(f.errors.length,1);f.walk.dispose();
});
test('synchronous unsupported Raw Input also fails without an adjusted fallback',()=>{
 const f=fixture(()=>{throw unsupported();});assert.equal(f.walk.enter(),false);assert.equal(f.walk.pending,false);assert.equal(f.walk.active,false);assert.equal(f.requests.length,1);assert.match(f.errors[0].message,/不支持.*Raw Input/);f.walk.dispose();
});
test('permission or activation denial reports its actual reason and never claims entry',async()=>{
 const f=fixture(()=>Promise.reject(Object.assign(new Error('User activation required'),{name:'NotAllowedError'})));f.walk.enter();await Promise.resolve();
 assert.equal(f.walk.active,false);assert.equal(f.walk.pending,false);assert.equal(f.controls.enabled,true);assert.equal(f.requests.length,1);assert.match(f.errors[0].message,/Raw Input.*User activation required/);f.walk.dispose();
});
test('pointerlockerror-only failure is visible and clears the pending request',()=>{
 const f=fixture();f.walk.enter();fire(f.document,'pointerlockerror');assert.equal(f.walk.pending,false);assert.equal(f.walk.active,false);assert.equal(f.controls.enabled,true);assert.equal(f.requests.length,1);assert.equal(f.errors.length,1);assert.match(f.errors[0].message,/Raw Input/);f.walk.dispose();
});
test('event-before-promise rejection does not duplicate errors or trigger fallback',async()=>{
 let reject;const f=fixture(()=>new Promise((_,r)=>reject=r));f.walk.enter();fire(f.document,'pointerlockerror');reject(unsupported());await Promise.resolve();
 assert.equal(f.errors.length,1);assert.equal(f.requests.length,1);assert.equal(f.walk.active,false);assert.equal(f.walk.pending,false);f.walk.dispose();
});
test('a large legitimate raw delta remains intact rather than being clamped or dropped',()=>{
 const f=fixture();f.walk.enter();f.lock();const yaw=f.walk.rotation.y,position=f.camera.position.clone();f.walk.mouseMove({movementX:900,movementY:0});
 assert.ok(Math.abs(f.walk.rotation.y-(yaw-900*.002))<1e-12);assert.deepEqual(f.camera.position,position);assert.equal(f.walk.sensitivity,.002);assert.equal(f.walk.boost,5);f.walk.dispose();
});
test('the request preserves browser security and does not acquire lock from a failed gesture',async()=>{
 const f=fixture(()=>Promise.reject(Object.assign(new Error('User exited the lock before the request completed'),{name:'SecurityError'})));f.walk.enter();await Promise.resolve();
 assert.equal(f.document.pointerLockElement,null);assert.equal(f.walk.active,false);assert.equal(f.requests.length,1);assert.match(f.errors[0].message,/User exited/);f.walk.dispose();
});
