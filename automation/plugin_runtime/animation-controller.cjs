'use strict';
const LOOP_ONCE=2200,LOOP_REPEAT=2201;
const finite=(value,name)=>{if(typeof value!=='number'||!Number.isFinite(value))throw new TypeError(`${name} must be finite`);return value;};
const clamp=(value,min,max)=>Math.min(max,Math.max(min,value));
function frameCount(duration,fps){const count=duration*fps,nearest=Math.round(count);return Math.abs(count-nearest)<.001?nearest:Math.ceil(count);}

class AnimationController {
  constructor(preview,{fps=30}={}){
    this.preview=preview;this.mixer=preview.animationMixer;this.actions=preview.animationActions;
    this.clips=(preview.animations||[]).map((clip,index)=>({index,name:String(clip.name||'').trim(),duration:Number(clip.duration)})).filter(clip=>clip.duration>0&&Number.isFinite(clip.duration)&&this.actions?.[clip.index]);
    if(!this.clips.length||typeof this.mixer?.update!=='function')throw Object.assign(new Error('This model has no controllable animation'),{code:'animation_unavailable'});
    this.listeners=new Set();this.disposed=false;this.busy=false;this.speed=1;this.loop=true;this.fps=this._validFps(fps);
    this.clipIndex=this.clips[0].index;this.playing=!this.actions[this.clipIndex].paused;
    const time=Number(this.actions[this.clipIndex].time)||0;
    this._finished=event=>{if(event.action===this.action&&!this.loop){this.playing=false;this.action.paused=true;this._emit();}};
    this.mixer.addEventListener?.('finished',this._finished);
    this._select(this.clipIndex,time,this.playing);
  }
  get clip(){return this.clips.find(clip=>clip.index===this.clipIndex);}
  get action(){return this.actions[this.clipIndex];}
  _validFps(value){finite(value,'fps');if(value<1||value>240)throw new RangeError('fps must be between 1 and 240');return value;}
  _mutable(){if(this.disposed)throw new Error('Animation controls were disposed');if(this.busy)throw Object.assign(new Error('A frame capture is in progress'),{code:'capture_in_progress'});}
  _configure(action){action.stopFading?.();action.stopWarping?.();action.enabled=true;action.setEffectiveWeight?.(1);action.setEffectiveTimeScale?.(this.speed);if(!action.setEffectiveTimeScale)action.timeScale=this.speed;action.setLoop(this.loop?LOOP_REPEAT:LOOP_ONCE,this.loop?Infinity:1);action.clampWhenFinished=true;}
  _select(index,time,playing,draw=true){
    if(!this.clips.some(clip=>clip.index===index))throw new RangeError('Unknown animation clip');
    for(const action of this.actions)action.stop();
    this.clipIndex=index;const action=this.action;action.reset();this._configure(action);action.play();action.time=clamp(time,0,this.clip.duration);action.paused=!playing;this.playing=playing;if(draw)this._redraw();
  }
  _redraw(){this.mixer.update(0);this.preview.scene?.updateMatrixWorld?.(true);if(typeof this.preview.composer?.render==='function')this.preview.composer.render();else this.preview.renderer?.render(this.preview.scene,this.preview.camera);}
  _emit(){const state=this.getState();for(const listener of this.listeners)listener(state);}
  subscribe(listener){this.listeners.add(listener);return()=>this.listeners.delete(listener);}
  getState(){
    const duration=this.clip.duration,time=clamp(Number(this.action.time)||0,0,duration),lastFrame=frameCount(duration,this.fps);
    return {clipIndex:this.clipIndex,clipName:this.clip.name,duration,time,playing:this.playing&&!this.action.paused,speed:this.speed,loop:this.loop,fps:this.fps,frame:time>=duration?lastFrame:Math.min(lastFrame,Math.round(time*this.fps)),lastFrame,busy:this.busy};
  }
  selectClip(index){this._mutable();const playing=this.getState().playing;this._select(index,0,playing);this._emit();}
  pause(){this._mutable();this.action.paused=true;this.playing=false;this._emit();}
  play(){this._mutable();if(this.action.time>=this.clip.duration){this.action.time=0;this.action.reset();this._configure(this.action);this.action.play();}this.action.enabled=true;this.action.paused=false;this.playing=true;this._emit();}
  toggle(){if(this.getState().playing)this.pause();else this.play();}
  replay(){this._mutable();this.action.reset();this._configure(this.action);this.action.play();this.action.time=0;this.action.paused=false;this.playing=true;this._redraw();this._emit();}
  seek(seconds){this._mutable();finite(seconds,'time');this.action.time=clamp(seconds,0,this.clip.duration);this._redraw();this._emit();}
  seekFrame(frame){finite(frame,'frame');const last=frameCount(this.clip.duration,this.fps),index=clamp(Math.round(frame),0,last);this.seek(index===last?this.clip.duration:index/this.fps);}
  step(frames){this._mutable();finite(frames,'frames');this.action.paused=true;this.playing=false;this.action.time=clamp(this.getState().time+frames/this.fps,0,this.clip.duration);this._redraw();this._emit();}
  setSpeed(speed){this._mutable();finite(speed,'speed');if(speed<=0||speed>8)throw new RangeError('Animation speed is out of range');this.speed=speed;this.action.setEffectiveTimeScale?.(speed);if(!this.action.setEffectiveTimeScale)this.action.timeScale=speed;this._emit();}
  setLoop(loop){this._mutable();if(typeof loop!=='boolean')throw new TypeError('loop must be a boolean');this.loop=loop;this.action.setLoop(loop?LOOP_REPEAT:LOOP_ONCE,loop?Infinity:1);this.action.clampWhenFinished=true;this._emit();}
  setFps(fps){this._mutable();this.fps=this._validFps(fps);this._emit();}
  async capture(publish){
    this._mutable();if(typeof publish!=='function')throw new TypeError('A frame publisher is required');
    const snapshot=this.getState(),controls=this.preview.controls,hadControls=controls&&'enabled'in controls,controlsEnabled=controls?.enabled;
    this.busy=true;
    try{
      this.action.paused=true;this.playing=false;if(hadControls)controls.enabled=false;this._redraw();this._emit();
      const blob=await this.preview.screenshot();
      if(!blob||typeof blob.arrayBuffer!=='function')throw Object.assign(new Error('The displayed frame could not be captured'),{code:'frame_capture_failed'});
      return await publish({blob,pose:{clip_index:snapshot.clipIndex,clip_name:snapshot.clipName,time:snapshot.time,fps:snapshot.fps}});
    }finally{
      this.speed=snapshot.speed;this.loop=snapshot.loop;this.fps=snapshot.fps;if(hadControls)controls.enabled=controlsEnabled;this.busy=false;this._select(snapshot.clipIndex,snapshot.time,snapshot.playing,false);this._emit();this._redraw();
    }
  }
  dispose(){if(this.disposed)return;this.disposed=true;this.mixer.removeEventListener?.('finished',this._finished);this.listeners.clear();}
}
function createAnimationController(preview,options){
  if(!Array.isArray(preview?.animations)||!Array.isArray(preview?.animationActions)||typeof preview.animationMixer?.update!=='function'||!preview.animations.some((clip,index)=>Number.isFinite(clip.duration)&&clip.duration>0&&preview.animationActions[index]))return null;
  return new AnimationController(preview,options);
}
function formatTime(value){const total=Math.max(0,Math.round(Number(value||0)*1000)),ms=total%1000,seconds=Math.floor(total/1000)%60,minutes=Math.floor(total/60000)%60,hours=Math.floor(total/3600000);return `${hours?String(hours)+':':''}${String(minutes).padStart(2,'0')}:${String(seconds).padStart(2,'0')}.${String(ms).padStart(3,'0')}`;}
module.exports={AnimationController,createAnimationController,formatTime,LOOP_ONCE,LOOP_REPEAT};
