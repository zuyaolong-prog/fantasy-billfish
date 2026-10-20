'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const {AnimationController,createAnimationController,formatTime,LOOP_ONCE}=require('../animation-controller.cjs');
function fixture(){
 const listeners=new Set(),actions=[];
 const mixer={time:0,addEventListener:(_name,listener)=>listeners.add(listener),removeEventListener:(_name,listener)=>listeners.delete(listener),update(delta){this.time+=delta;for(const action of actions){if(!action.enabled||action.paused||!action.running)continue;action.time+=delta*action.timeScale;if(action.time>=action.duration&&delta>0){if(action.loop===LOOP_ONCE){action.time=action.duration;action.paused=true;for(const listener of listeners)listener({action});}else action.time%=action.duration;}}}};
 for(let index=0;index<3;index++)actions.push({time:0,timeScale:1,duration:index+1,paused:false,enabled:true,running:index===0,stop(){this.running=false;this.reset();return this;},reset(){this.time=0;this.paused=false;this.enabled=true;return this;},play(){this.running=true;return this;},stopFading(){},stopWarping(){},setEffectiveWeight(value){this.weight=value;},setEffectiveTimeScale(value){this.timeScale=value;},setLoop(value){this.loop=value;}});
 const preview={animationMixer:mixer,animationActions:actions,animations:[{name:'Move',duration:1},{name:'Turn',duration:2},{name:'',duration:3}],scene:{},camera:{},renderer:{render(){preview.draws++;}},controls:{enabled:true},draws:0,async screenshot(){return new Blob(['fixture']);}};
 return {preview,mixer,actions,controller:new AnimationController(preview)};
}
test('clip switching stops old contributions and preserves intended playback state',()=>{
 const {controller,actions,mixer}=fixture();mixer.update(.25);controller.pause();controller.selectClip(1);assert.equal(controller.getState().playing,false);assert.equal(controller.getState().time,0);assert.equal(actions[0].running,false);assert.equal(actions[1].running,true);assert.equal(actions[1].weight,1);
 controller.play();controller.selectClip(2);assert.equal(controller.getState().playing,true);assert.equal(actions[1].running,false);assert.equal(actions[2].running,true);
});
test('paused seek includes exact end and explicit-frame steps are independent of speed',()=>{
 const {controller,mixer}=fixture();controller.pause();controller.seek(1);assert.equal(controller.getState().time,1);assert.equal(controller.getState().playing,false);mixer.update(.5);assert.equal(controller.getState().time,1);
 controller.setFps(24);controller.setSpeed(2);controller.seek(.5);controller.step(1);assert.ok(Math.abs(controller.getState().time-(.5+1/24))<1e-9);assert.equal(controller.getState().playing,false);controller.seekFrame(0);assert.equal(controller.getState().time,0);
 controller.play();controller.seek(.4);mixer.update(.1);assert.ok(Math.abs(controller.getState().time-.6)<1e-9);
});
test('loop mode wraps while once mode clamps, pauses and can replay',()=>{
 const {controller,mixer}=fixture();controller.setLoop(true);mixer.update(1.2);assert.ok(Math.abs(controller.getState().time-.2)<1e-9);controller.setLoop(false);mixer.update(2);assert.equal(controller.getState().time,1);assert.equal(controller.getState().playing,false);
 controller.play();assert.equal(controller.getState().time,0);assert.equal(controller.getState().playing,true);controller.replay();assert.equal(controller.getState().time,0);
});
test('capture freezes exact pose and restores clip/time/play/speed/loop on success, cancellation and failure',async()=>{
 const {controller,preview,mixer}=fixture();controller.selectClip(1);controller.seek(.75);controller.setSpeed(.5);controller.setLoop(false);controller.setFps(60);
 const expected=controller.getState();const project=state=>{const {busy,...rest}=state;return rest;};
 for(const outcome of ['success','cancel','failure']){
  const operation=controller.capture(async({blob,pose})=>{assert.ok(blob instanceof Blob);assert.equal(pose.clip_index,1);assert.equal(pose.time,.75);assert.equal(controller.getState().playing,false);assert.equal(preview.controls.enabled,false);assert.throws(()=>controller.seek(0),{code:'capture_in_progress'});mixer.update(4);assert.equal(controller.getState().time,.75);if(outcome==='failure')throw new Error('native failed');return outcome==='cancel'?{cancelled:true}:{completion:'native_custom_cover_verified'};});
  if(outcome==='failure')await assert.rejects(operation,/native failed/);else await operation;
  assert.deepEqual(project(controller.getState()),project(expected));assert.equal(preview.controls.enabled,true);assert.equal(controller.getState().busy,false);
 }
 assert.ok(mixer.time>=12,'the controller does not rewind the independent mixer clock');
});
test('static/unsupported models remain without controls and public input boundaries are explicit',()=>{
 assert.equal(createAnimationController({animations:[],animationActions:[],animationMixer:{}}),null);assert.equal(createAnimationController({animations:[{duration:-1}],animationActions:[{}],animationMixer:{update(){}}}),null);
 const {controller}=fixture();assert.throws(()=>controller.seek(NaN),TypeError);assert.throws(()=>controller.setFps(0),RangeError);assert.throws(()=>controller.setSpeed(-1),RangeError);assert.throws(()=>controller.selectClip(99),RangeError);
 assert.equal(formatTime(3661.123),'1:01:01.123');assert.equal(formatTime(.025),'00:00.025');
 controller.preview.animations[0].duration=1.2000000476837158;controller.clips[0].duration=1.2000000476837158;controller.setFps(30);assert.equal(controller.getState().lastFrame,36);controller.seekFrame(36);assert.equal(controller.getState().time,controller.clip.duration);
});
test('a redraw or screenshot failure restores playback flags and camera interaction',async()=>{
 const {controller,preview}=fixture();controller.seek(.4);const before=controller.getState();let fail=true;preview.renderer.render=()=>{if(fail){fail=false;throw new Error('GPU draw failed');}};
 await assert.rejects(controller.capture(()=>{throw new Error('Must not publish')}),/GPU draw failed/);assert.equal(controller.getState().busy,false);assert.equal(controller.getState().playing,before.playing);assert.equal(controller.getState().time,before.time);assert.equal(preview.controls.enabled,true);
 preview.screenshot=async()=>{throw new Error('PNG failed')};await assert.rejects(controller.capture(()=>{}),/PNG failed/);assert.equal(controller.getState().playing,before.playing);assert.equal(controller.getState().busy,false);
});
