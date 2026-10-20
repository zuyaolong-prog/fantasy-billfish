import test from 'node:test';
import assert from 'node:assert/strict';
import {PerspectiveCamera,Vector3} from '../../vendor/three/build/three.module.js';
import {WalkControls} from '../../ui/walk-controls.mjs';
import {readFile} from 'node:fs/promises';

const orbitSource=await readFile(new URL('../../vendor/three/addons/controls/OrbitControls.js',import.meta.url),'utf8');
const threeURL=new URL('../../vendor/three/build/three.module.js',import.meta.url).href;
const {OrbitControls}=await import(`data:text/javascript;base64,${Buffer.from(orbitSource.replace("from 'three'",`from '${threeURL}'`)).toString('base64')}`);

function send(target,type,props={}){const event=new Event(type,{cancelable:true});for(const [key,value] of Object.entries(props))Object.defineProperty(event,key,{value});target.dispatchEvent(event);return event;}
function fixture({defer=false,fail=false}={}){
  const document=new EventTarget(),window=new EventTarget(),element=new EventTarget(),errors=[],states=[];
  document.defaultView=window;document.pointerLockElement=null;document.hidden=false;document.activeElement=element;
  element.ownerDocument=document;element.tagName='CANVAS';element.focus=()=>{document.activeElement=element;};element.contains=target=>target===element;
  element.requestPointerLock=()=>{if(fail)throw Error('denied');if(!defer){document.pointerLockElement=element;send(document,'pointerlockchange');}};
  document.exitPointerLock=()=>{document.pointerLockElement=null;send(document,'pointerlockchange');};
  const camera=new PerspectiveCamera();camera.position.set(0,0,10);camera.lookAt(0,0,0);
  const controls={target:new Vector3(),enabled:true,enableDamping:true,update(){}};
  const walk=new WalkControls({camera,controls,element,speed:10,onError:error=>errors.push(error),onChange:state=>states.push(state)});
  const key=(code,extra={})=>send(document,'keydown',{code,target:element,...extra});
  return {walk,camera,controls,document,window,element,errors,states,key};
}
const close=(actual,expected)=>assert.ok(Math.abs(actual-expected)<1e-7,`${actual} != ${expected}`);

test('movement is per second, stops on release, and Shift multiplies by five',()=>{
  const f=fixture();f.walk.enter();assert.equal(f.walk.active,true);assert.equal(f.controls.enabled,false);
  f.key('KeyW');f.walk.update(.1);close(f.camera.position.z,9);
  send(f.document,'keyup',{code:'KeyW'});f.walk.update(.1);close(f.camera.position.z,9);
  f.key('KeyW');f.key('ShiftLeft');f.walk.update(.1);close(f.camera.position.z,4);f.walk.dispose();
});
test('Shift held through the entry shortcut accelerates without pressing Shift again',()=>{
  const f=fixture();f.key('Backquote',{shiftKey:true});f.key('KeyW',{shiftKey:true});f.walk.update(.1);close(f.camera.position.z,5);
  send(f.document,'keyup',{code:'ShiftLeft',shiftKey:false});f.walk.update(.1);close(f.camera.position.z,4);f.walk.dispose();
});
test('diagonal motion preserves speed, Q/E use world vertical',()=>{
  const f=fixture();f.walk.enter();f.key('KeyW');f.key('KeyD');f.walk.update(.1);close(f.camera.position.distanceTo(new Vector3(0,0,10)),1);
  f.walk.keys.clear();f.key('KeyE');f.walk.update(.1);close(f.camera.position.y,1);f.walk.dispose();
});
test('mouse yaw and pitch steer movement and stay short of vertical singularity',()=>{
  const f=fixture();f.walk.enter();send(f.document,'mousemove',{movementX:-Math.PI/2/.002,movementY:0});f.key('KeyW');f.walk.update(.1);close(f.camera.position.x,-1);
  send(f.document,'mousemove',{movementX:0,movementY:-1e9});assert.ok(f.walk.rotation.x<Math.PI/2);f.walk.dispose();
});
test('cancel restores camera position, orientation, Orbit target and enabled state',()=>{
  const f=fixture(),position=f.camera.position.clone(),quaternion=f.camera.quaternion.clone(),target=f.controls.target.clone();
  f.walk.enter();f.key('KeyD');f.walk.update(.1);send(f.document,'mousemove',{movementX:50,movementY:20});f.key('Escape');
  assert.equal(f.walk.active,false);assert.deepEqual(f.camera.position,position);assert.deepEqual(f.camera.quaternion.toArray(),quaternion.toArray());assert.deepEqual(f.controls.target,target);assert.equal(f.controls.enabled,true);f.walk.dispose();
});
test('confirm retains viewpoint and moves Orbit target forward without residual momentum',()=>{
  const f=fixture();f.walk.enter();f.key('KeyD');f.walk.update(.1);send(f.document,'mousemove',{movementX:30,movementY:20});
  const position=f.camera.position.clone(),quaternion=f.camera.quaternion.clone();
  f.controls.update=()=>{f.camera.position.x+=100;};f.key('Enter');
  assert.deepEqual(f.camera.position,position);assert.deepEqual(f.camera.quaternion.toArray(),quaternion.toArray());
  close(f.controls.target.distanceTo(f.camera.position),10);f.walk.dispose();
});
test('blur and pointer lock loss clear held keys and stop the controller',()=>{
  const f=fixture();f.walk.enter();f.key('KeyW');send(f.window,'blur');const position=f.camera.position.clone();f.walk.update(.1);assert.deepEqual(f.camera.position,position);assert.equal(f.walk.keys.size,0);
  f.walk.enter();f.key('KeyW');f.document.pointerLockElement=null;send(f.document,'pointerlockchange');assert.equal(f.walk.active,false);assert.equal(f.walk.keys.size,0);f.walk.dispose();
});
test('Pointer Lock failure is visible, never disables Orbit or claims active',()=>{
  const f=fixture({fail:true});assert.equal(f.walk.enter(),false);assert.equal(f.walk.active,false);assert.equal(f.walk.pending,false);assert.equal(f.errors.length,1);assert.equal(f.controls.enabled,true);f.walk.dispose();
});
test('pending lock cancelled by blur cannot reactivate from a late browser event',()=>{
  const f=fixture({defer:true});f.walk.enter();assert.equal(f.walk.pending,true);assert.equal(f.walk.active,false);send(f.window,'blur');
  f.document.pointerLockElement=f.element;send(f.document,'pointerlockchange');assert.equal(f.walk.active,false);assert.equal(f.document.pointerLockElement,null);f.walk.dispose();
});
test('shortcut is restricted to focused viewport, modal Space cannot reach animation handler',()=>{
  const f=fixture();f.key('Backquote',{shiftKey:true,target:{tagName:'INPUT'}});assert.equal(f.walk.active,false);
  f.key('Backquote',{shiftKey:true});assert.equal(f.walk.active,true);let reached=0;f.document.addEventListener('keydown',()=>reached++);
  const event=f.key('Space');assert.equal(event.defaultPrevented,true);assert.equal(reached,0);f.walk.dispose();
});
test('wheel changes base speed, mouse buttons confirm and cancel',()=>{
  const f=fixture();f.walk.enter();send(f.element,'wheel',{deltaY:-100});assert.ok(f.walk.speed>10);f.key('KeyW');f.walk.update(.1);
  send(f.document,'mousedown',{button:0});assert.equal(f.walk.active,false);const position=f.camera.position.clone();
  f.walk.enter();f.key('KeyW');f.walk.update(.1);send(f.document,'mousedown',{button:2});assert.deepEqual(f.camera.position,position);f.walk.dispose();
});
test('dispose detaches inputs and restores pointer lock and Orbit',()=>{
  const f=fixture();f.walk.enter();f.walk.dispose();f.key('Backquote',{shiftKey:true});assert.equal(f.walk.active,false);assert.equal(f.document.pointerLockElement,null);assert.equal(f.controls.enabled,true);assert.equal(f.walk.enter(),false);
});

test('real OrbitControls resumes without a camera jump after confirm or cancel',()=>{
  for(const cancel of [false,true]){
    const f=fixture();f.walk.dispose();f.element.style={};f.element.getRootNode=()=>f.document;
    const controls=new OrbitControls(f.camera,f.element);controls.enableDamping=true;
    const walk=new WalkControls({camera:f.camera,controls,element:f.element,speed:10});
    // Leave actual Orbit damping momentum pending when entering Walk.
    controls._sphericalDelta.theta=.2;controls.update();
    walk.enter();send(f.document,'keydown',{code:'KeyW',target:f.element});walk.update(.1);
    send(f.document,'mousemove',{movementX:40,movementY:20});walk.exit({cancel});
    const position=f.camera.position.clone(),quaternion=f.camera.quaternion.clone();
    controls.update();close(f.camera.position.distanceTo(position),0);close(f.camera.quaternion.angleTo(quaternion),0);
    walk.dispose();controls.dispose();
  }
});

test('browser Escape lock loss cancels even when browser withholds keydown',()=>{
  const f=fixture(),position=f.camera.position.clone();f.walk.enter();f.key('KeyW');f.walk.update(.1);
  f.document.pointerLockElement=null;send(f.document,'pointerlockchange');assert.deepEqual(f.camera.position,position);f.walk.dispose();
});

test('asynchronous Pointer Lock rejection is visible and clears pending state',async()=>{
  const f=fixture({defer:true});f.element.requestPointerLock=()=>Promise.reject(Error('denied'));f.walk.enter();await Promise.resolve();
  assert.equal(f.walk.pending,false);assert.equal(f.walk.active,false);assert.equal(f.errors.length,1);f.walk.dispose();
});
