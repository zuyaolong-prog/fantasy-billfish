// The frozen format renderer lives only in the trusted isolated preload world.
// This facade holds serialized state and uses a fixed-source, concrete API.
export class CoreViewport {
  constructor(element,{bridge,onState=()=>{},onWarning=()=>{},onRows=()=>{},onCapabilities=()=>{}}={}){
    if(!bridge||typeof bridge.load!=='function')throw new Error('此格式的预览通道不可用');
    this.element=element;this.bridge=bridge;this.onState=onState;this.onWarning=onWarning;this.onRows=onRows;this.onCapabilities=onCapabilities;this.model=null;this.disposed=false;this.isCore=true;this.sequence=0;
    this.capabilities={coordinateProfiles:false,libraryAssociation:false,displayFacing:false,visibility:false,currentFrameCover:false,skeletonOverlay:false,cacheCommands:[]};this.hasSkeleton=false;this.skeletonVisible=false;
    this.snapshot={rowId:null,time:0,duration:0,playing:false,speed:1,loop:true,hasAction:false};
    const owner=this;
    this.player={
      get current(){return owner.snapshot.rowId;},set current(value){owner.snapshot={...owner.snapshot,rowId:value};},
      get playing(){return owner.snapshot.playing;},get action(){return owner.snapshot.hasAction?Object.freeze({serialized:true}):null;},
      get speed(){return owner.snapshot.speed;},set speed(value){owner.command('setSpeed',value);},
      state:()=>({...owner.snapshot}),
      async select(rowId){const generation=++owner.sequence;const result=await owner.bridge.select(rowId);if(owner.disposed||generation!==owner.sequence)return false;owner.applyState(result);const playing=await owner.bridge.setPlaying(true);if(owner.disposed||generation!==owner.sequence)return false;owner.applyState(playing);return true;},
      setPlaying:value=>owner.command('setPlaying',value),seek:value=>owner.command('seek',value),setLoop:value=>owner.command('setLoop',value),
      stop(){owner.sequence++;owner.command('setPlaying',false);},dispose:()=>owner.dispose(),
    };
    this.controls={reset:()=>this.bridge.resetCamera()};
    this.unsubscribe=this.bridge.onState?.(state=>{if(!this.disposed)this.applyState(state);});
  }
  applyState(state){if(!state||typeof state!=='object')return;const previous=this.snapshot,hadSkeleton=this.hasSkeleton,wasVisible=this.skeletonVisible;this.snapshot={...previous,...state};if(typeof state.hasSkeleton==='boolean')this.hasSkeleton=state.hasSkeleton&&this.capabilities.skeletonOverlay===true;if(typeof state.skeletonVisible==='boolean')this.skeletonVisible=this.hasSkeleton&&state.skeletonVisible;if(!this.hasSkeleton)this.skeletonVisible=false;if(hadSkeleton!==this.hasSkeleton||wasVisible!==this.skeletonVisible)this.onCapabilities(this.capabilities);if(previous.rowId!==this.snapshot.rowId||previous.playing!==this.snapshot.playing||previous.hasAction!==this.snapshot.hasAction||state.error)this.onState({...this.snapshot});}
  command(method,value){if(this.disposed)return Promise.resolve();return Promise.resolve().then(()=>this.bridge[method](value)).then(state=>{if(!this.disposed)this.applyState(state);}).catch(error=>{if(!this.disposed)this.onWarning({message:error.message});});}
  async load(){
    if(this.model)throw new Error('当前视口已有模型');
    const result=await this.bridge.load(this.element.id);
    if(this.disposed)return result;
    if(!(Number.isInteger(result?.drawableCount)&&result.drawableCount>0)&&!(Number.isFinite(result?.renderedFrame)&&result.renderedFrame>0))throw new Error('模型未产生可显示的画面');
    this.meshCount=result.meshCount||0;this.drawableCount=result.drawableCount||0;this.renderedFrame=result.renderedFrame;this.model={renderer:'core'};
    this.capabilities={...this.capabilities,...result.capabilities};this.hasSkeleton=result.hasSkeleton===true&&this.capabilities.skeletonOverlay===true;this.skeletonVisible=this.hasSkeleton&&result.skeletonVisible===true;this.onCapabilities(this.capabilities);this.applyState(result.state);this.onRows(result.rows||[]);
    for(const warning of result.warnings||[])this.onWarning(warning);
    return {meshCount:this.meshCount,drawableCount:this.drawableCount,renderedFrame:this.renderedFrame};
  }
  frame(){return this.bridge.frame();}
  setFacing(degrees){if(this.capabilities.displayFacing)return this.bridge.setFacing(degrees);if(Number(degrees)!==0)this.onWarning({message:'此格式不支持保存视口朝向设置'});}
  async setSkeletonVisible(visible){if(typeof visible!=='boolean')throw new TypeError('骨架显示选项必须为 boolean');if(!this.hasSkeleton||!this.capabilities.skeletonOverlay)return false;const state=await this.bridge.setSkeletonVisible(visible);if(this.disposed)return false;if(typeof state?.hasSkeleton!=='boolean'||typeof state?.skeletonVisible!=='boolean')throw new Error('骨架显示状态不可用');this.applyState(state);return this.skeletonVisible;}
  async capture(){this.lastCapturePose=null;const result=await this.bridge.capture();if(!result?.png||!result?.pose)throw new Error('未能取得当前画面');this.lastCapturePose=result.pose;this.applyState(result.state);return result.png;}
  dispose(){if(this.disposed)return;this.disposed=true;this.sequence++;this.unsubscribe?.();this.model=null;this.hasSkeleton=false;this.skeletonVisible=false;return Promise.resolve(this.bridge.dispose()).catch(error=>{this.onWarning({message:`预览资源清理失败: ${error.message}`});throw error;});}
}
