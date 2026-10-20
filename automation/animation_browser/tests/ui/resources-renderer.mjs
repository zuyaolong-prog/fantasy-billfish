import {CharacterViewport} from '../../ui/viewport.mjs';

const bridge=window.resourceProbe;
let viewport;
async function run(){
 const state=await bridge.initialize(),warnings=[];
 viewport=new CharacterViewport(document.getElementById('viewport'),{loadClip:(rowId,options)=>bridge.loadClip(rowId,options),onWarning:warning=>warnings.push(warning)});
 await viewport.load(state.model);
 // Direct production instances; measurements are test-owned, not production APIs.
 const renderer=viewport.renderer,player=viewport.player,mixer=player.mixer,root=viewport.model,modelUUID=root.uuid,context=renderer.getContext();
 const samples=[];
 const measure=async(completed,phase)=>{
  viewport.render();
  const heap=bridge.heap(),perf=performance.memory;
  const snapshot={phase,completed,elapsedMs:performance.now(),modelLoads:viewport.modelLoads,modelUUID:viewport.model?.uuid || null,sameModel:viewport.model===root,
   gpu:{geometries:renderer.info.memory.geometries,textures:renderer.info.memory.textures,programs:renderer.info.programs?.length ?? null},
   player:player.state(),mixer:{actionsTotal:mixer.stats.actions.total,actionsInUse:mixer.stats.actions.inUse,bindingsTotal:mixer.stats.bindings.total,bindingsInUse:mixer.stats.bindings.inUse},
   heap:heap || (perf?{usedHeapSize:perf.usedJSHeapSize,totalHeapSize:perf.totalJSHeapSize,heapSizeLimit:perf.jsHeapSizeLimit}:null),heapSource:heap?'Electron process.getHeapStatistics':perf?'performance.memory':'unavailable',heapUnits:heap?'KiB':'bytes',forcedGC:false};
  const combined=await bridge.sample(snapshot);samples.push(combined);return combined;
 };
 const baseline=await measure(0,'base-rendered');
 let selected=0;
 for(const row of state.rows){
  if(!(await player.select(row.id)))throw new Error('A sequential selection was unexpectedly superseded');
  player.setPlaying(false);player.seek(Math.min(row.duration*.37,.75));viewport.render();selected++;
  if(viewport.model!==root||viewport.modelLoads!==1)throw new Error('The resident model was replaced');
  if(mixer.stats.actions.inUse!==1||mixer.stats.actions.total!==1)throw new Error('More than one action is retained');
  if(player.cache.entries.size>12||player.cache.bytes>player.cache.maxBytes)throw new Error('The production clip cache exceeded its configured limit');
  if(selected%25===0||selected===state.rows.length){const sample=await measure(selected,'clip-switching');for(const key of ['geometries','textures','programs'])if(sample.gpu[key]!==baseline.gpu[key])throw new Error(`GPU ${key} changed while only switching clips`);}
 }
 const beforeDispose=await measure(selected,'before-dispose');
 let contextLostEvent=false;renderer.domElement.addEventListener('webglcontextlost',()=>{contextLostEvent=true;},{once:true});
 viewport.dispose();
 // Context loss is delivered asynchronously; no GC is requested.
 await new Promise(resolve=>setTimeout(resolve,150));
 const afterDispose={phase:'after-dispose',completed:selected,elapsedMs:performance.now(),modelLoads:viewport.modelLoads,modelUUID:viewport.model?.uuid || null,sameModel:viewport.model===root,
  gpu:{geometries:renderer.info.memory.geometries,textures:renderer.info.memory.textures,programs:renderer.info.programs?.length ?? null},
  player:player.state(),mixer:{actionsTotal:mixer.stats.actions.total,actionsInUse:mixer.stats.actions.inUse,bindingsTotal:mixer.stats.bindings.total,bindingsInUse:mixer.stats.bindings.inUse},
  heap:bridge.heap(),heapSource:'Electron process.getHeapStatistics',heapUnits:'KiB',forcedGC:false,contextLost:context.isContextLost(),contextLostEvent,canvasAttached:renderer.domElement.isConnected};
 samples.push(await bridge.sample(afterDispose));
 if(afterDispose.gpu.geometries!==0||afterDispose.gpu.textures!==0||afterDispose.gpu.programs!==0||afterDispose.mixer.actionsTotal!==0||afterDispose.mixer.bindingsTotal!==0||afterDispose.player.cacheBytes!==0||afterDispose.player.cacheEntries!==0||!afterDispose.contextLost||afterDispose.canvasAttached)throw new Error('Dispose left owned GPU/action/cache/context resources alive');
 await bridge.finish({ok:true,selected,modelUUID,samples,warnings,baseline,beforeDispose,afterDispose,notes:['No forced GC was requested. RSS/heap include natural GC and allocator retention.','GPU counts cover Three-owned geometry, textures and programs, not driver-reserved VRAM.','The supplied pressure clips are synthetic distinct GLB fixture files; this is not a claim of 320 independent commercial motions.']});
}
run().catch(async error=>{try{viewport?.dispose();await bridge.fail({message:error.message,stack:error.stack});}catch{}});
