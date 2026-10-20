import { Euler, Vector3 } from '../vendor/three/build/three.module.js';

const movementKeys = new Set(['KeyW','KeyA','KeyS','KeyD','KeyE','KeyQ','ShiftLeft','ShiftRight']);
const blockedTarget = target => target?.isContentEditable || /^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(target?.tagName || '') || !!target?.closest?.('.drawer');

// The owner calls update(deltaSeconds) instead of OrbitControls.update() while active.
export class WalkControls {
  constructor({camera,controls,element,onChange=()=>{},onError=()=>{},speed=1,boost=5,sensitivity=0.002}) {
    Object.assign(this,{camera,controls,element,onChange,onError,boost,sensitivity});
    this.document=element.ownerDocument;this.window=this.document.defaultView;
    this.active=false;this.pending=false;this.disposed=false;this.keys=new Set();
    this.rotation=new Euler(0,0,0,'YXZ');this.direction=new Vector3();this.right=new Vector3();this.motion=new Vector3();this.up=new Vector3(0,1,0);
    this.setSpeed(speed);this.listeners=[];
    this.listen(this.document,'pointerlockchange',()=>this.lockChanged());
    this.listen(this.document,'pointerlockerror',()=>this.fail('无法启用原生鼠标输入 (Raw Input). 请重新点击自由漫游按钮重试.'));
    this.listen(this.document,'keydown',event=>this.keyDown(event),true);
    this.listen(this.document,'keyup',event=>{this.keys.delete(event.code);if(event.code==='ShiftLeft'||event.code==='ShiftRight')this.keys.delete('ShiftModifier');if(this.active&&movementKeys.has(event.code))this.consume(event);},true);
    this.listen(this.document,'mousemove',event=>this.mouseMove(event));
    this.listen(this.document,'mousedown',event=>{if(!this.active)return;if(event.button===0||event.button===2){this.consume(event);this.suppressContextMenu=event.button===2;this.exit({cancel:event.button===2});}},true);
    this.listen(element,'contextmenu',event=>{if(this.active||this.suppressContextMenu){this.consume(event);this.suppressContextMenu=false;}});
    this.listen(element,'wheel',event=>{if(!this.active)return;this.consume(event);this.setSpeed(this.speed*Math.exp(-Math.sign(event.deltaY)*0.15));},{passive:false});
    this.listen(this.window,'blur',()=>this.exit());
    this.listen(this.document,'visibilitychange',()=>{if(this.document.hidden)this.exit();});
  }
  listen(target,type,listener,options){target.addEventListener(type,listener,options);this.listeners.push(()=>target.removeEventListener(type,listener,options));}
  consume(event){event.preventDefault();event.stopImmediatePropagation();}
  state(){return {active:this.active,pending:this.pending,speed:this.speed};}
  changed(){this.onChange(this.state());}
  setSpeed(value){if(Number.isFinite(value)&&value>0)this.speed=Math.min(1e9,Math.max(1e-6,value));if(this.listeners)this.changed();return this.speed;}
  enter(){
    if(this.disposed||this.active||this.pending)return false;
    if(typeof this.element.requestPointerLock!=='function'){this.onError(new Error('当前视口无法锁定鼠标, 自由漫游不可用.'));return false;}
    this.pending=true;this.changed();this.element.focus({preventScroll:true});
    try{const request=this.element.requestPointerLock({unadjustedMovement:true});request?.catch?.(error=>{if(this.pending)this.fail(error?.name==='NotSupportedError'?'当前设备或浏览器不支持原生鼠标输入 (Raw Input), 无法启用自由漫游.':`无法启用原生鼠标输入 (Raw Input): ${error?.message || error}. 请重新点击自由漫游按钮重试.`);});}
    catch(error){this.fail(error?.name==='NotSupportedError'?'当前设备或浏览器不支持原生鼠标输入 (Raw Input), 无法启用自由漫游.':`无法启用原生鼠标输入 (Raw Input): ${error.message || error}`);return false;}
    return true;
  }
  fail(message){if(!this.pending&&!this.active)return;this.exit();this.onError(new Error(message));}
  lockChanged(){
    if(this.document.pointerLockElement!==this.element){if(this.active||this.pending)this.exit({cancel:true});return;}
    if(!this.pending||this.disposed){if(!this.active)this.document.exitPointerLock?.();return;}
    this.pending=false;this.active=true;this.keys.clear();
    this.saved={position:this.camera.position.clone(),quaternion:this.camera.quaternion.clone(),target:this.controls.target.clone(),enabled:this.controls.enabled};
    this.targetDistance=Math.max(this.camera.position.distanceTo(this.controls.target),0.001);
    this.rotation.setFromQuaternion(this.camera.quaternion,'YXZ');this.rotation.z=0;
    this.controls.enabled=false;this.changed();
  }
  keyDown(event){
    if(!this.active){
      if(event.code==='Backquote'&&event.shiftKey&&!event.ctrlKey&&!event.altKey&&!event.metaKey&&!event.repeat&&!blockedTarget(event.target)&&(event.target===this.element||this.element.contains?.(event.target))){this.consume(event);this.enter();}
      return;
    }
    // All navigation keystrokes are modal, including Space and legacy animation shortcuts.
    if(blockedTarget(event.target)){this.exit();return;}
    this.consume(event);
    if(event.code==='Escape'){this.exit({cancel:true});return;}
    if(event.code==='Enter'){this.exit();return;}
    if(event.shiftKey===true)this.keys.add('ShiftModifier');else if(event.shiftKey===false)this.keys.delete('ShiftModifier');
    if(movementKeys.has(event.code)&&!event.ctrlKey&&!event.altKey&&!event.metaKey)this.keys.add(event.code);
  }
  mouseMove(event){
    if(!this.active||this.document.pointerLockElement!==this.element)return;
    this.rotation.y-=(Number(event.movementX)||0)*this.sensitivity;
    this.rotation.x=Math.max(-Math.PI/2+0.001,Math.min(Math.PI/2-0.001,this.rotation.x-(Number(event.movementY)||0)*this.sensitivity));
    this.camera.quaternion.setFromEuler(this.rotation);this.syncTarget();
  }
  syncTarget(){this.camera.getWorldDirection(this.direction);this.controls.target.copy(this.camera.position).addScaledVector(this.direction,this.targetDistance);}
  update(deltaSeconds){
    if(!this.active)return false;
    const delta=Math.min(Math.max(Number(deltaSeconds)||0,0),0.1);
    const forward=Number(this.keys.has('KeyW'))-Number(this.keys.has('KeyS'));
    const side=Number(this.keys.has('KeyD'))-Number(this.keys.has('KeyA'));
    const vertical=Number(this.keys.has('KeyE'))-Number(this.keys.has('KeyQ'));
    if(!forward&&!side&&!vertical)return false;
    this.camera.getWorldDirection(this.direction);this.right.set(1,0,0).applyQuaternion(this.camera.quaternion);
    this.motion.set(0,0,0).addScaledVector(this.direction,forward).addScaledVector(this.right,side).addScaledVector(this.up,vertical).normalize();
    const fast=this.keys.has('ShiftLeft')||this.keys.has('ShiftRight')||this.keys.has('ShiftModifier');
    this.camera.position.addScaledVector(this.motion,this.speed*(fast?this.boost:1)*delta);this.syncTarget();return true;
  }
  exit({cancel=false}={}){
    const wasActive=this.active,wasPending=this.pending;
    this.active=false;this.pending=false;this.keys.clear();
    if(wasActive){
      if(cancel){this.camera.position.copy(this.saved.position);this.camera.quaternion.copy(this.saved.quaternion);this.controls.target.copy(this.saved.target);}
      else this.syncTarget();
      // Drain Orbit damping deltas without carrying them into the confirmed pose.
      const position=this.camera.position.clone(),quaternion=this.camera.quaternion.clone(),target=this.controls.target.clone(),damping=this.controls.enableDamping;
      this.controls.enableDamping=false;this.controls.update();
      this.camera.position.copy(position);this.camera.quaternion.copy(quaternion);this.controls.target.copy(target);
      this.controls.enableDamping=damping;this.controls.enabled=this.saved.enabled;this.saved=null;
    }
    if(this.document.pointerLockElement===this.element)this.document.exitPointerLock?.();
    if(wasActive||wasPending)this.changed();
    return wasActive||wasPending;
  }
  dispose(){if(this.disposed)return;this.exit();this.disposed=true;for(const remove of this.listeners)remove();this.listeners=[];}
}
