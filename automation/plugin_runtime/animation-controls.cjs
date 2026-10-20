'use strict';
const fs=require('node:fs'),path=require('node:path');
const {createAnimationController,formatTime}=require('./animation-controller.cjs');

function installAnimationControls(preview,{document=globalThis.document,window=globalThis.window,onCapture}={}){
  const controller=createAnimationController(preview);
  if(!controller)return {controller:null,dispose(){}};
  const style=document.createElement('style');style.textContent=fs.readFileSync(path.join(__dirname,'animation-controls.css'),'utf8');document.head.appendChild(style);
  document.body.classList.add('bf-animation-viewer');
  for(const selector of ['.progress','.animation-list','.module-tips'])document.querySelector(selector)?.remove();
  const panel=document.createElement('section');panel.className='bf-animation';panel.setAttribute('aria-label','动画控制');
  panel.innerHTML='<div class="bf-animation-row"><label class="bf-animation-clip">动画 <select data-control="clip" aria-label="选择动画"></select></label><button type="button" data-control="play" title="空格: 播放或暂停">暂停</button><button type="button" data-control="replay" title="R: 从头重播">重播</button><label>速度 <select data-control="speed" aria-label="播放速度"></select></label><label><select data-control="loop" aria-label="播放方式"><option value="loop">循环播放</option><option value="once">播放一次</option></select></label><button type="button" class="bf-animation-cover" data-control="capture">当前帧设为封面</button></div><div class="bf-animation-row bf-animation-timeline"><span data-output="current">00:00.000</span><input type="range" data-control="timeline" aria-label="播放进度" min="0" step="any"><span data-output="duration">00:00.000</span></div><div class="bf-animation-row bf-animation-detail"><button type="button" data-control="previous" title="左方向键: 前一帧">前一帧</button><button type="button" data-control="next" title="右方向键: 后一帧">后一帧</button><label>秒 <input type="number" data-control="seconds" aria-label="时间, 秒" min="0" step="0.001"></label><label>帧 <input type="number" data-control="frame" aria-label="当前帧" min="0" step="1"><span data-output="frames"></span></label><label>逐帧帧率 <select data-control="fps" aria-label="逐帧帧率"></select></label><span class="bf-animation-status" role="status" aria-live="polite"></span></div>';
  document.body.appendChild(panel);
  const $=name=>panel.querySelector(`[data-control="${name}"]`),out=name=>panel.querySelector(`[data-output="${name}"]`),status=panel.querySelector('.bf-animation-status');
  const duplicateNames=new Map();for(const clip of controller.clips)if(clip.name)duplicateNames.set(clip.name,(duplicateNames.get(clip.name)||0)+1);
  for(const clip of controller.clips){const option=document.createElement('option');option.value=String(clip.index);option.textContent=clip.name?(duplicateNames.get(clip.name)>1?`${clip.name} (${clip.index+1})`:clip.name):`动画 ${clip.index+1}`;$('clip').appendChild(option);}
  for(const speed of [0.25,0.5,1,1.5,2]){const option=document.createElement('option');option.value=String(speed);option.textContent=speed+'x';$('speed').appendChild(option);}
  for(const fps of [24,25,30,48,50,60,120]){const option=document.createElement('option');option.value=String(fps);option.textContent=fps+' 帧/秒';$('fps').appendChild(option);}
  let dragging=false,resumeDrag=false,disposed=false,raf=0;
  function update(){const state=controller.getState();
    $('clip').value=String(state.clipIndex);$('clip').disabled=state.busy||controller.clips.length<2;
    for(const control of panel.querySelectorAll('button,input,select'))if(control!==$('clip'))control.disabled=state.busy;
    $('capture').disabled=state.busy||typeof onCapture!=='function';$('play').textContent=state.playing?'暂停':'播放';$('capture').textContent=state.busy?'正在设置封面...':'当前帧设为封面';
    $('speed').value=String(state.speed);$('loop').value=state.loop?'loop':'once';$('fps').value=String(state.fps);$('timeline').max=String(state.duration);if(!dragging)$('timeline').value=String(state.time);
    $('seconds').max=String(state.duration);$('frame').max=String(state.lastFrame);if(document.activeElement!==$('seconds'))$('seconds').value=state.time.toFixed(3);if(document.activeElement!==$('frame'))$('frame').value=String(state.frame);
    out('current').textContent=formatTime(state.time);out('duration').textContent=formatTime(state.duration);out('frames').textContent=' / '+state.lastFrame;
  }
  const action=callback=>()=>{try{callback();update();}catch(error){status.textContent='操作未完成, 请稍后重试.';console.error(error);}};
  $('play').addEventListener('click',action(()=>controller.toggle()));$('replay').addEventListener('click',action(()=>controller.replay()));
  $('previous').addEventListener('click',action(()=>controller.step(-1)));$('next').addEventListener('click',action(()=>controller.step(1)));
  $('clip').addEventListener('change',action(()=>controller.selectClip(Number($('clip').value))));$('speed').addEventListener('change',action(()=>controller.setSpeed(Number($('speed').value))));
  $('loop').addEventListener('change',action(()=>controller.setLoop($('loop').value==='loop')));$('fps').addEventListener('change',action(()=>controller.setFps(Number($('fps').value))));
  $('seconds').addEventListener('change',action(()=>controller.seek(Number($('seconds').value))));$('frame').addEventListener('change',action(()=>controller.seekFrame(Number($('frame').value))));
  $('timeline').addEventListener('pointerdown',()=>{if(controller.getState().busy)return;dragging=true;resumeDrag=controller.getState().playing;controller.pause();});
  $('timeline').addEventListener('input',action(()=>controller.seek(Number($('timeline').value))));
  function finishDrag(){if(!dragging)return;dragging=false;const state=controller.getState();if(resumeDrag&&!state.busy&&state.time<state.duration)controller.play();update();}
  $('timeline').addEventListener('pointerup',finishDrag);$('timeline').addEventListener('pointercancel',finishDrag);$('timeline').addEventListener('change',finishDrag);$('timeline').addEventListener('blur',finishDrag);
  async function capture(){if(controller.getState().busy||!onCapture)return;status.textContent='';try{const result=await controller.capture(onCapture);status.textContent=result?.cancelled?'已取消设置封面.':'已设为当前素材封面.';}catch(error){status.textContent=['source_changed','viewer_source_changed','preview_source_changed','viewer_source_mismatch','library_changed','library_mismatch','library_not_open'].includes(error.code)?'素材或素材库已发生变化, 请重新打开后设置封面.':'封面设置失败, 请重试.';console.error(error);}update();}
  $('capture').addEventListener('click',capture);
  function keydown(event){if(event.defaultPrevented||event.ctrlKey||event.metaKey||event.altKey||controller.getState().busy||event.target?.closest?.('input,select,textarea,button,[contenteditable="true"]'))return;
    const key=event.key.toLowerCase();let handler;if(event.code==='Space')handler=()=>controller.toggle();else if(key==='arrowleft')handler=()=>controller.step(event.shiftKey?-10:-1);else if(key==='arrowright')handler=()=>controller.step(event.shiftKey?10:1);else if(key==='home')handler=()=>{controller.pause();controller.seek(0)};else if(key==='end')handler=()=>{controller.pause();controller.seek(controller.clip.duration)};else if(key==='r')handler=()=>controller.replay();if(handler){event.preventDefault();action(handler)();}}
  document.addEventListener('keydown',keydown);const unsubscribe=controller.subscribe(update);
  function tick(){if(disposed)return;update();raf=window.requestAnimationFrame(tick);}update();raf=window.requestAnimationFrame(tick);
  const viewport=preview.renderer?.domElement?.parentElement;
  function fitViewport(){if(!viewport||!preview.camera)return;const width=viewport.clientWidth,height=viewport.clientHeight;if(width<=0||height<=0)return;preview.camera.aspect=width/height;preview.camera.updateProjectionMatrix();preview.renderer.setSize(width,height);preview.composer?.setSize?.(width,height);}
  fitViewport();const resizeObserver=typeof window.ResizeObserver==='function'&&viewport?new window.ResizeObserver(fitViewport):null;resizeObserver?.observe(viewport);
  function dispose(){if(disposed)return;disposed=true;window.cancelAnimationFrame(raf);resizeObserver?.disconnect();unsubscribe();document.removeEventListener('keydown',keydown);controller.dispose();panel.remove();style.remove();document.body.classList.remove('bf-animation-viewer');}
  window.addEventListener('beforeunload',dispose,{once:true});return {controller,panel,capture,dispose};
}
module.exports={installAnimationControls};
