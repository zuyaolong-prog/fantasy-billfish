import { CharacterViewport } from './viewport.mjs';
import { CoreViewport } from './core-viewport.mjs';
import { compatibilityNames, visibleRows, seconds, fpsLabel } from './list.mjs';

const $ = id => document.getElementById(id), bridge = window.billfishAnimationBrowser;
const ui = {rows:[],libraryContext:null,capabilities:{},settings:{search:'',sort:'name',group:true,compatibility:'full'},selected:new Set(),current:null,profile:{},warnings:[],conflicts:[],loading:null,opening:false};
let materialCaptureInProgress=false;
let viewport, unsubscribe, timer, settingsTimer, initialized = false, queuedEvents = [], lastAnchor = null,modelKey=null,reloadSequence=0,scopeChanging=false,timelinePointer=null;
const nameFromPath = path => String(path || '').split(/[\\/]/).pop();
function rowLabel(row){const file=nameFromPath(row.source).replace(/\.(fbx|glb)$/i,'');return /^(Take\s*\d*|Animation\s*\d*|Default\s*Take|mixamo\.com)$/i.test(row.name || '')&&file?file:row.name || `动画 ${row.clipIndex+1}`;}
function message(text,error=false) { $('message').textContent = text; $('message').hidden = !text; $('message').classList.toggle('error',error); }
function failure(error) { message(error?.message || String(error),true); console.error(error); }
async function call(method,args={}) { const result = await bridge.call(method,args); if (result?.ok === false) throw new Error(result.error?.message || result.error || '操作失败'); return result; }
let feedbackSequence=0;
function action(id,handler) { $(id).addEventListener('click',() => {
  const button=$(id);if(button.disabled)return;
  try{
    const result=handler();
    if(!result||typeof result.then!=='function')return;
    const sequence=++feedbackSequence;
    const label=button.getAttribute?.('aria-label')||button.textContent||button.title||id;
    const status=$('operation-feedback');status.hidden=false;status.textContent=`正在执行: ${label}`;status.setAttribute('aria-busy','true');
    Promise.resolve(result).then(()=>{if(sequence===feedbackSequence){status.textContent=`已完成: ${label}`;status.setAttribute('aria-busy','false');}},error=>{if(sequence===feedbackSequence){status.textContent=`操作失败: ${error?.message||error}`;status.setAttribute('aria-busy','false');}failure(error);});
  }catch(error){failure(error);}
}); }
function setDrawer(id,show) { $(id).hidden = !show; $(id === 'settings-panel' ? 'settings-toggle' : 'report-toggle').setAttribute('aria-expanded',String(show)); }
function warning(w) { const text = typeof w === 'string' ? w : w.message || w.code || JSON.stringify(w); if (ui.warnings.some(v=>v.text===text)) return; ui.warnings.push({text,warning:w}); $('warning-count').textContent=ui.warnings.length; const li=document.createElement('li');li.textContent=text;$('warning-list').append(li); }
function selectedRow() { return ui.rows.find(r=>r.id === [...ui.selected].at(-1)) || ui.rows.find(r=>r.id === ui.current); }
function filtered() { return visibleRows(ui.rows,ui.settings); }
function playable(row){return row.compatibility!=='incompatible'&&row.association?.enabled!==false&&(row.compatibility!=='partial'||row.association?.confirmed);}
function syncControls(){
  const available=!ui.opening&&!!viewport?.model&&ui.rows.some(playable),active=!ui.opening&&!!viewport?.player?.action;
  for(const id of ['play','previous','next','loop','speed','step-fps'])$(id).disabled=!available;
  for(const id of ['replay','timeline','seek-seconds','seek-frame','frame-back','frame-forward'])$(id).disabled=!active;
}
function syncNavigation(state=viewport?.navigationState || {}){
  const supported=typeof viewport?.enterWalk==='function',loaded=!ui.opening&&!!viewport?.model;
  $('walk-mode').disabled=!loaded||!supported||state.pending===true;
  $('walk-mode').setAttribute('aria-pressed',String(state.active===true));
  $('walk-mode').textContent=state.pending?'正在进入漫游...':state.active?'确认漫游位置':'自由漫游';
  $('walk-mode').title=supported?'自由漫游 (Shift+`), WASD 移动, Shift 加速':'此格式的当前视口暂不支持自由漫游';
  $('walk-help').hidden=!state.active;
  $('walk-help').textContent=`WASD 移动 | E/Q 升降 | Shift 5x 加速 | 滚轮调速 | 速度 ${Number(state.speed||0).toPrecision(3)} 单位/s | Enter/左键确认 | Esc/右键取消`;
}
function syncRenderMode(state=viewport?.renderModeState || {}){
  const supported=typeof viewport?.setRenderMode==='function',loaded=!ui.opening&&!!viewport?.model;
  $('render-mode-control').hidden=loaded&&!supported;
  for(const mode of ['solid','material']){const button=$(`render-${mode}`);button.disabled=!loaded||!supported||materialCaptureInProgress;button.setAttribute('aria-pressed',String(loaded&&supported&&state.mode===mode));button.setAttribute('aria-busy',String(mode==='material'&&state.requestedMode==='material'&&state.materialState==='loading'));button.classList.toggle('requested',loaded&&state.requestedMode===mode&&state.mode!==mode);}
  const loading=state.materialState==='loading',error=state.materialState==='error';
  $('render-mode-feedback').hidden=!supported||(!loading&&!error);
  $('render-mode-feedback').classList.toggle('error',error);
  $('render-mode-status').setAttribute('aria-busy',String(loading));
  if(error)$('render-mode-status').textContent=`材质加载失败, 当前保留${state.mode==='material'?'材质':'实体'}显示. ${state.error || ''}`;
  else if(loading){const progress=state.progress,counts=Number.isFinite(progress?.total)&&progress.total>0&&Number.isFinite(progress?.loaded)?` (${progress.loaded}/${progress.total})`:'';$('render-mode-status').textContent=`正在加载原质量材质${counts}. 可切回实体继续查看.`;}
  else $('render-mode-status').textContent='';
  $('retry-material').hidden=!error;$('retry-material').disabled=materialCaptureInProgress;
}
async function selectRenderMode(mode){
  const target=viewport;if(typeof target?.setRenderMode!=='function')return;
  try{await target.setRenderMode(mode);}finally{if(viewport===target)syncRenderMode();}
}
function syncCapabilities(){
  syncNavigation();syncRenderMode();
  const loaded=!ui.opening&&!!viewport?.model;for(const id of ['frame','reset-camera','settings-toggle','report-toggle'])$(id).disabled=!loaded;
  const scope=ui.libraryContext;$('scope-parent-control').hidden=viewport?.isCore===true||!['current','parent'].includes(scope?.candidateScope);$('search-parent').disabled=scopeChanging||scope?.canSearchParent!==true||viewport?.isCore===true;$('search-parent').checked=scope?.candidateScope==='parent';
  const c=ui.capabilities;$('coordinate-controls').disabled=!loaded||c.coordinateProfiles===false;$('capability-note').hidden=c.coordinateProfiles!==false;
  const skeleton=viewport?.hasSkeleton===true&&typeof viewport.setSkeletonVisible==='function';$('skeleton-control').hidden=!skeleton;$('show-skeleton').disabled=!skeleton;$('show-skeleton').checked=skeleton&&viewport.skeletonVisible===true;
  $('refresh').disabled=!loaded||viewport?.isCore===true;$('refresh').title=viewport?.isCore?'重新打开模型可更新文件自带动画':'增量刷新库内动画关联';
  $('render-facing').disabled=!loaded||c.displayFacing===false;$('facing-note').hidden=c.coordinateProfiles!==false||c.displayFacing!==true;
  $('cover').disabled=!loaded||c.currentFrameCover===false||materialCaptureInProgress;$('cover').title=c.currentFrameCover===false?'此格式不支持设置素材封面':'';
  for(const id of ['associate','disassociate','show-source'])$(id).hidden=c.libraryAssociation===false;
  for(const id of ['hide-associated','show-all','load-visibility-conflict','resolve-visibility-conflict'])$(id).disabled=c.visibility===false;
  $('report-toggle').textContent=c.libraryAssociation===false?'动画信息':'匹配报告';
  const commands=c.cacheCommands;for(const option of $('cache-command').options||[])option.disabled=Array.isArray(commands)&&!commands.includes(option.value);
  if(Array.isArray(commands)&&!commands.includes($('cache-command').value))$('cache-command').value=commands[0]||'';
  $('run-cache-command').disabled=!loaded||(Array.isArray(commands)&&!commands.length);
}
function applyLibraryContext(state){
  if(state.libraryContext){ui.libraryContext=state.libraryContext;const {libraryRoot,libraryName,relativeFolder,relativeScope,scopeRoot}=state.libraryContext,folder=relativeFolder||(state.stage==='waiting-native'?'待确认':'库根目录');$('library-label').textContent=`Billfish 库: ${libraryName||nameFromPath(libraryRoot)||'当前素材库'}`;$('library-label').title=libraryRoot||'';$('library-directory').textContent=`目录: ${folder}`;$('library-directory').title=folder;$('scope-label').textContent=state.stage==='waiting-native'?'动画目录: 待确认':scopeRoot?`动画目录: ${relativeScope||'库根目录'} (含全部子目录)`:`库内目录: ${folder}`;$('scope-label').title=scopeRoot||libraryRoot||'';}
}

function setRows(rows) {
  ui.rows = rows || [];
  const ids = new Set(ui.rows.map(r=>r.id)); for(const id of ui.selected) if(!ids.has(id)) ui.selected.delete(id);
  if(ui.current&&!ui.rows.some(r=>r.id===ui.current&&r.association?.enabled!==false&&r.compatibility!=='incompatible'))stopPlayback();
  const folder = $('folder-filter').value; $('folder-filter').replaceChildren(new Option('所有目录',''));
  for (const value of [...new Set(ui.rows.map(r=>r.folder || ''))].sort()) if(value) $('folder-filter').add(new Option(value,value));
  $('folder-filter').value = folder; renderList();
}
function stopPlayback(){if(viewport?.player){viewport.player.sequence++;viewport.player.stop();viewport.player.current=null;updateTransport(viewport.player.state());}ui.current=null;$('playing-name').textContent='参考姿态';$('source-fps').textContent='';$('timeline').disabled=true;}
async function restartCurrent(){const row=ui.rows.find(r=>r.id===ui.current);if(row&&viewport?.model&&viewport?.player)await playRow(row);}
function syncClipFeedback(){
  const row=ui.rows.find(row=>row.id===ui.loading),status=$('clip-feedback');
  status.hidden=!ui.loading;status.setAttribute('aria-busy',String(!!ui.loading));
  status.textContent=ui.loading?`正在切换动画: ${row?rowLabel(row):ui.loading}. 当前画面在新动画就绪后更新.`:'';
}
function renderList() {
  syncClipFeedback();
  const rows=filtered(), list=$('clip-list'), fragment=document.createDocumentFragment(); let folder=null;
  const grouped = ui.settings.group ? [...rows].sort((a,b)=>(a.folder || '').localeCompare(b.folder || '')) : rows;
  for(const row of grouped) {
    if(ui.settings.group && folder !== (row.folder || '')) { folder=row.folder || '';const heading=document.createElement('div');heading.className='folder-heading';heading.textContent=folder || '角色目录';fragment.append(heading); }
    const element=document.createElement('div');element.className='clip-row';element.dataset.rowId=row.id;element.role='option';element.tabIndex=-1;element.id=`clip-${encodeURIComponent(row.id)}`;element.setAttribute('aria-selected',String(ui.selected.has(row.id)));element.classList.toggle('selected',ui.selected.has(row.id));element.classList.toggle('current',ui.current===row.id);element.setAttribute('aria-busy',String(ui.loading===row.id));
    const icon=document.createElement('span');icon.className='clip-icon';icon.textContent=ui.loading===row.id?'◌':ui.current===row.id?(viewport?.player?.playing?'▶':'Ⅱ'):'▤';
    const body=document.createElement('div'),name=document.createElement('span');name.className='clip-name';name.textContent=rowLabel(row);name.title=`${row.name}\n${row.relativePath || row.source} · Clip ${row.clipIndex+1}`;
    const meta=document.createElement('div');meta.className='clip-meta';
    for(const text of [seconds(row.duration),fpsLabel(row.fps),String(row.format || '').toUpperCase()]) { const s=document.createElement('span');s.textContent=text;meta.append(s); }
    const status=document.createElement('span');status.textContent=compatibilityNames[row.compatibility] || '待匹配';status.className=`compat-${row.compatibility}`;meta.append(status);
    body.append(name,meta);
    if(ui.rows.some(other=>other.id!==row.id&&rowLabel(other)===rowLabel(row))){const origin=document.createElement('div');origin.className='clip-folder';origin.textContent=`${nameFromPath(row.source)} · Clip ${row.clipIndex+1}`;body.append(origin);}
    if(!ui.settings.group && row.folder) {const f=document.createElement('div');f.className='clip-folder';f.textContent=row.folder;body.append(f);}
    element.append(icon,body);element.addEventListener('click',event=>{
      if(event.shiftKey && lastAnchor) { const ids=grouped.map(r=>r.id),a=ids.indexOf(lastAnchor),b=ids.indexOf(row.id);if(a>=0) { if(!event.ctrlKey) ui.selected.clear();for(const id of ids.slice(Math.min(a,b),Math.max(a,b)+1)) ui.selected.add(id); } }
      else if(event.ctrlKey || event.metaKey) { if(ui.selected.has(row.id))ui.selected.delete(row.id);else ui.selected.add(row.id);lastAnchor=row.id; }
      else {ui.selected=new Set([row.id]);lastAnchor=row.id;playRow(row).catch(failure);}
      renderList();if(!$('report-panel').hidden) renderReport(row);
    });element.addEventListener('contextmenu',event=>{event.preventDefault();ui.selected=new Set([row.id]);renderList();renderReport(row);setDrawer('report-panel',true);});fragment.append(element);
  }
  const scroll=list.scrollTop;list.replaceChildren(fragment);list.scrollTop=scroll;
  $('row-count').textContent = rows.length === ui.rows.length ? `${rows.length}` : `${rows.length} / ${ui.rows.length}`;
  $('empty-list').hidden = rows.length>0;$('selection-bar').hidden=ui.selected.size===0;$('selection-count').textContent=`已选 ${ui.selected.size} 项`;
  syncControls();
}
let pendingSelection=null;
function playRow(row) {
  if(pendingSelection?.rowId===row.id&&pendingSelection.player===viewport?.player)return pendingSelection.promise;
  const entry={rowId:row.id,player:viewport?.player};
  entry.promise=selectRow(row).finally(()=>{if(pendingSelection===entry)pendingSelection=null;});pendingSelection=entry;return entry.promise;
}
async function selectRow(row) {
  if(!viewport?.player) throw new Error('模型仍在加载, 请稍后');
  if(row.compatibility==='incompatible') {renderReport(row);setDrawer('report-panel',true);message('此动作无法可靠绑定. 请查看匹配报告.',true);return;}
  if(row.association?.enabled===false) {renderReport(row);setDrawer('report-panel',true);message('此动作已取消关联. 可在匹配报告中重新关联.',true);return;}
  if(row.compatibility==='partial' && !row.association?.confirmed) {renderReport(row);setDrawer('report-panel',true);message('此动作仅部分兼容. 检查报告后可手动确认关联.',true);return;}
  message('');ui.loading=row.id;renderList();
  try {if(await viewport.player.select(row.id)) {ui.current=row.id;$('playing-name').textContent=rowLabel(row);$('source-fps').textContent=`源: ${fpsLabel(row.fps)}`;$('timeline').disabled=false;$('timeline').max=row.duration || 0;}}
  finally {if(ui.loading===row.id)ui.loading=null;renderList();}
}
function updateTransport(state) { ui.current=state.rowId;ui.loading=state.loading || null;$('play').textContent=state.playing?'Ⅱ':'▶';$('play').setAttribute('aria-label',state.playing?'暂停':'播放');if(state.error)message(state.error,true);tickTransport(state);if(initialized)renderList(); }
function tickTransport(state=viewport?.player?.state()) { if(!state)return;const focused=document.activeElement?.id;if(timelinePointer===null)$('timeline').value=state.time;$('time-label').textContent=`${state.time.toFixed(2)} / ${state.duration.toFixed(2)} s`;const speed=Number.isFinite(state.speed)&&state.speed>0?state.speed:1;$('actual-time-label').textContent=`实际时间 ${(state.time/speed).toFixed(2)} / ${(state.duration/speed).toFixed(2)} s`;if(focused!=='seek-seconds')$('seek-seconds').value=state.time.toFixed(3);if(focused!=='seek-frame')$('seek-frame').value=Math.round(state.time*Number($('step-fps').value)); }
function step(delta) { const p=viewport?.player;if(p?.action){p.setPlaying(false);p.seek(p.state().time+delta/Number($('step-fps').value));tickTransport();} }
function navigate(delta) { const rows=filtered().filter(r=>r.compatibility!=='incompatible'&&r.association?.enabled!==false&&(r.compatibility!=='partial'||r.association?.confirmed));if(!rows.length)return;const i=rows.findIndex(r=>r.id===ui.current),row=rows[(i+delta+rows.length)%rows.length];ui.selected=new Set([row.id]);renderList();playRow(row).catch(failure);requestAnimationFrame(()=>document.querySelector(`[data-row-id="${CSS.escape(row.id)}"]`)?.scrollIntoView({block:'nearest'})); }
function updateScan(scan={}) { const running=['running','scanning','indexing','queued'].includes(scan.state);$('cancel-scan').hidden=!running;const labels={idle:'索引就绪',complete:'索引完成',completed:'索引完成',cancelled:'已取消',running:'扫描中',scanning:'扫描中',indexing:'索引中',error:'索引失败'};$('scan-label').textContent=`${labels[scan.state] || scan.state || '索引就绪'}${scan.total ? ` · ${scan.completed||0}/${scan.total}` : ''}${scan.errorCount ? ` · ${scan.errorCount} 个错误` : ''}`;if(running&&!scan.total)$('scan-meter').removeAttribute('value');else{$('scan-meter').max=scan.total||1;$('scan-meter').value=scan.total?scan.completed||0:1;} }
function renderReport(row=selectedRow()) {
  const content=$('report-content');content.replaceChildren();if(!row){content.textContent='先选择一个动画.';return;}
  ui.reportRow=row.id;const report=row.report || {},dl=document.createElement('dl');
  const values=[['动作',row.name],['来源文件',row.source],['相对目录',row.relativePath || row.folder],['匹配目标',nameFromPath(ui.source)],['兼容状态',compatibilityNames[row.compatibility]],['动画',`${seconds(row.duration)} · ${fpsLabel(row.fps)}`]];
  const names={totalTracks:'总轨道数',matchedTracks:'匹配轨道数',matchedBoneTracks:'成功匹配骨骼轨道数',unmatchedTracks:'未匹配轨道',missingRequiredBones:'缺失的必要骨骼',missingBones:'缺失骨骼',warnings:'坐标与姿态提示',reasons:'匹配说明',diagnostics:'诊断',basisDifferences:'骨骼朝向差异',status:'匹配结果'};
  for(const [key,value] of Object.entries(report)) if(value!==null && value!==undefined)values.push([names[key]||key,typeof value==='object'?JSON.stringify(value,null,2):String(value)]);
  for(const [label,value] of values){const field=document.createElement('div');field.className='report-field';const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=value || '-';field.append(dt,dd);dl.append(field);}content.append(dl);
  $('associate').disabled=row.compatibility==='incompatible';$('associate').textContent=row.compatibility==='partial'?'确认部分兼容并关联':'关联到此角色';$('disassociate').disabled=row.association?.enabled===false;
  $('show-source').textContent=ui.profile.visibility?.show?.[row.id]?'恢复自动隐藏此动画':'单独显示此动画文件';
  syncCapabilities();
}
function populateProfile(profile={},editingFile=false) {
  if(!editingFile)ui.profile=profile;const detected=profile.detected || ui.metadata?.coordinates || {};const source=detected.sourceAxes || detected.detected?.axes || detected.axes || detected;
  $('detected-axes').textContent=`文件检测: ${JSON.stringify(source)}${detected.sourceUnitMeters?` · ${detected.sourceUnitMeters} m/unit`:''}`;
  $('axes-preset').value=profile.interpretation || 'auto';const axes=profile.sourceAxes || {up:'+Y',front:'+Z'};$('up-axis').value=axes.up || '+Y';$('front-axis').value=axes.front || '+Z';$('handedness').value=axes.handedness || 'right';$('unit-meters').value=profile.sourceUnitMeters || '';
  $('joint-primary').value=profile.jointBasisPolicy?.primary || '+X';$('joint-secondary').value=profile.jointBasisPolicy?.secondary || '+Y';$('joint-overrides').value=Object.keys(profile.jointBasisOverrides || {}).length?JSON.stringify(profile.jointBasisOverrides,null,2):'';$('render-facing').value=String(profile.renderFacing?.yaw || 0);viewport?.setFacing(profile.renderFacing?.yaw || 0);
  $('hide-associated').checked=profile.visibility?.enabled!==false;$('show-all').checked=!!profile.visibility?.showAll;
  if(!editingFile)populateVisibilityConflicts(profile.visibility || {});
  if(!editingFile){ui.conflicts=profile.conflicts || [];$('conflicts').hidden=!ui.conflicts.length;$('conflict-version').replaceChildren();for(const c of ui.conflicts)$('conflict-version').add(new Option(`${c.writerId || '设备'} · ${c.revision}`,c.revision));$('conflict-details').textContent=JSON.stringify(ui.conflicts.map(c=>({revision:c.revision,parents:c.parents,writerId:c.writerId,value:c.value})),null,2);$('resolve-conflict').disabled=!ui.conflictBase||!ui.conflicts.some(c=>c.revision===ui.conflictBase);}
}
function populateVisibilityConflicts(profile){ui.visibilityConflicts=profile.conflicts || [];$('visibility-conflicts').hidden=!ui.visibilityConflicts.length;$('visibility-conflict-version').replaceChildren();for(const c of ui.visibilityConflicts)$('visibility-conflict-version').add(new Option(`${c.writerId || '设备'} · ${c.revision}`,c.revision));$('visibility-conflict-details').textContent=JSON.stringify(ui.visibilityConflicts.map(c=>({revision:c.revision,parents:c.parents,value:c.value})),null,2);$('resolve-visibility-conflict').disabled=!ui.visibilityConflictBase||!ui.visibilityConflicts.some(c=>c.revision===ui.visibilityConflictBase);}
function profilePatch() {
  let overrides={};if($('joint-overrides').value.trim()) {overrides=JSON.parse($('joint-overrides').value);if(!overrides||Array.isArray(overrides)||typeof overrides!=='object')throw new Error('逐关节记录需要 JSON 对象');}
  const interpretation=$('axes-preset').value,up=$('up-axis').value,front=$('front-axis').value;if(interpretation==='custom'&&up.slice(1)===front.slice(1))throw new Error('Up 与 Front 不能使用相同轴');
  const primary=$('joint-primary').value,secondary=$('joint-secondary').value;if(primary.slice(1)===secondary.slice(1))throw new Error('Primary 与 Secondary 不能使用相同轴');
  const units=$('unit-meters').value?Number($('unit-meters').value):null;if(units!==null&&(!Number.isFinite(units)||units<=0))throw new Error('每单位米数必须大于零');
  const vector=axis=>['X','Y','Z'].map(a=>axis[1]===a?(axis[0]==='+'?1:-1):0),a=vector(up),b=vector(front),cross=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]],rightIndex=cross.findIndex(v=>v!==0),rightSign=cross[rightIndex]*($('handedness').value==='left'?-1:1);
  return {interpretation,manualPreset:interpretation==='auto'?'maya':interpretation,sourceAxes:interpretation==='custom'?{right:`${rightSign<0?'-':'+'}${['X','Y','Z'][rightIndex]}`,up,front,handedness:$('handedness').value}:null,sourceUnitMeters:units,jointBasisPolicy:{primary,secondary,sourceKind:'explicit'},jointBasisOverrides:overrides,renderFacing:{yaw:Number($('render-facing').value)}};
}
async function saveProfile(resolve=false) {
  if(ui.capabilities.coordinateProfiles===false)throw new Error('此格式不支持骨架坐标配置');
  if(resolve&&!ui.conflictBase)throw new Error('请先加载一个冲突版本, 检查其完整配置后再合并');
  const patch=resolve?{...ui.profile,...profilePatch()}:profilePatch();delete patch.revision;delete patch.conflicts;delete patch.scope;const fileScope=$('profile-scope').value==='file',row=selectedRow();if(fileScope&&!row)throw new Error('请先选择需要覆盖配置的动画');
  const result=await call(resolve?'profile.resolveConflict':'profile.update',{patch,parents:resolve?ui.conflicts.map(c=>c.revision):ui.profile.revision?[ui.profile.revision]:[],...(fileScope?{rowId:row.id,scope:'file'}:{scope:'character'})});
  if(result?.profile)populateProfile(result.profile);else if(result?.interpretation)populateProfile(result);await restartCurrent();$('profile-saved').textContent='已保存';message('坐标配置已保存. 受影响的动画将重新验证.');
}
function applyState(state) {
  applyLibraryContext(state);
  if(state.source)ui.source=state.source;else if(state.model?.metadata?.source)ui.source=state.model.metadata.source;
  if(state.model?.metadata)ui.metadata=state.model.metadata;
  if(state.rows)setRows(state.rows);if(state.scan)updateScan(state.scan);if(state.profile){populateProfile(state.profile);if($('profile-scope').value==='file'){const row=selectedRow();if(row)call('profile.get',{scope:'file',rowId:row.id}).then(result=>{if(selectedRow()?.id===row.id&&$('profile-scope').value==='file')populateProfile(result.profile || result,true);}).catch(failure);}}
  renderList();
  syncCapabilities();
  for(const w of [...(state.diagnostics || []),...(state.model?.warnings || []),...(state.model?.metadata?.warnings || [])])warning(w);
  const key=state.model?.path || state.model?.url;if(viewport?.model&&key&&modelKey!==key)reloadModel(state.model).catch(failure);
}
function showModelLoading(stage,context={}) {
  if(context.source){ui.source=context.source;const name=context.name||nameFromPath(context.source);$('model-name').textContent=name;document.title=`${name} · Billfish`;}
  else if(stage==='waiting-native'){$('model-name').textContent='模型预览';document.title='模型预览 · Billfish';}
  applyLibraryContext({...context,stage});
  if(stage==='ready'&&viewport?.model){ui.opening=false;$('viewport-status').hidden=true;$('viewport-status').setAttribute('aria-busy','false');syncControls();syncCapabilities();return;}
  ui.opening=true;syncControls();syncCapabilities();
  const labels={'waiting-native':'正在打开模型...','opening-viewer':'正在打开模型预览...','checking-source':'正在验证模型文件...','preparing-model':'正在准备模型数据...','loading-resources':'正在加载模型资源...','applying-coordinates':'正在应用坐标配置...'};
  $('viewport-status').textContent=labels[stage]||'正在准备模型...';$('viewport-status').hidden=false;$('viewport-status').setAttribute('aria-busy','true');
}
function showModelFailure(error){$('viewport-status').textContent=`模型加载失败\n${error?.message||error}`;$('viewport-status').hidden=false;$('viewport-status').setAttribute('aria-busy','false');}
function eventReceived(event) {const earlyType=event.type||event.event;if(earlyType==='error'){const payload=event.payload??event.data??event,error=payload.error||payload;if(ui.opening||!viewport?.model)showModelFailure(error);failure(error);return;}if(earlyType==='loading'){const payload=event.payload??event.data??event;if(!viewport?.model||ui.opening||['waiting-native','ready'].includes(payload.stage))showModelLoading(payload.stage,payload);return;}if(!initialized){queuedEvents.push(event);return;}const type=event.type || event.event,payload=event.payload ?? event.data ?? event;if(type==='state')applyState(payload.state || payload);else if(type==='rows')applyState(Array.isArray(payload)?{rows:payload}:payload);else if(type==='progress')updateScan(payload.scan || payload);else if(type==='warning')warning(payload.warning || payload);else if(type==='profileConflict'){if(payload.scope==='visibility'){populateVisibilityConflicts({conflicts:payload.conflicts || payload.heads || []});warning({message:'检测到素材显示配置冲突, 请在设置中处理.'});}else{populateProfile({...ui.profile,conflicts:payload.conflicts || payload.heads || []});warning({message:'检测到坐标或手动关联配置冲突, 请在设置中处理.'});}}else if(type==='visibility'){if(payload.profile)populateProfile(payload.profile);if(payload.settings){$('hide-associated').checked=payload.settings.enabled!==false;$('show-all').checked=!!payload.settings.showAll;populateVisibilityConflicts(payload.settings);}if(payload.rows)setRows(payload.rows);}}
function makeViewport(model){
  if(model?.renderer==='core'){ui.capabilities={coordinateProfiles:false,libraryAssociation:false,displayFacing:false,visibility:false,currentFrameCover:false,cacheCommands:[]};return new CoreViewport($('viewport'),{bridge:bridge.core,onState:updateTransport,onWarning:warning,onRows:setRows,onCapabilities:capabilities=>{ui.capabilities=capabilities;syncCapabilities();}});}
  ui.capabilities={};return new CharacterViewport($('viewport'),{loadClip:(rowId,options={})=>call('clip.load',{rowId,...options}),onState:updateTransport,onWarning:warning,onNavigationState:syncNavigation,onNavigationError:failure,onRenderModeState:syncRenderMode});
}
async function reloadModel(model){
  const sequence=++reloadSequence;modelKey=model.path || model.url;const previous=ui.current;showModelLoading('applying-coordinates');
  try{await viewport.dispose();viewport=makeViewport(model);await viewport.load(model,{renderMode:'solid',facing:ui.profile.renderFacing?.yaw || 0});if(sequence!==reloadSequence)return;if(viewport.isCore){await viewport.setFacing(ui.profile.renderFacing?.yaw || 0);await viewport.frame();}ui.opening=false;syncControls();syncCapabilities();$('viewport-status').hidden=true;$('viewport-status').setAttribute('aria-busy','false');const row=ui.rows.find(r=>r.id===previous);if(row)await playRow(row);}
  catch(error){if(sequence===reloadSequence)showModelFailure(error);throw error;}
}
function settingsChanged() {
  ui.settings={...ui.settings,search:$('search').value,sort:$('sort').value,group:$('group').checked,compatibility:$('compatibility-filter').value,format:$('format-filter').value,folder:$('folder-filter').value,minDuration:$('min-duration').value,maxDuration:$('max-duration').value};renderList();clearTimeout(settingsTimer);if(!viewport?.isCore)settingsTimer=setTimeout(()=>call('listSettings.update',ui.settings).catch(failure),300);
}
async function initialize() {
  if(!bridge)throw new Error('Billfish 预览通道不可用. 请从 Billfish 打开模型.');
  showModelLoading();syncControls();syncCapabilities();unsubscribe=bridge.onEvent(eventReceived);const initial=await bridge.initialize(),state=initial.state || initial;
  ui.source=state.source || state.model?.metadata?.source || '';ui.settings={...ui.settings,...state.listSettings,compatibility:'full'};$('model-name').textContent=state.name || nameFromPath(ui.source) || '模型预览';document.title=`${$('model-name').textContent} · Billfish`;
  for(const [id,key] of [['search','search'],['sort','sort'],['compatibility-filter','compatibility'],['format-filter','format'],['min-duration','minDuration'],['max-duration','maxDuration']])$(id).value=ui.settings[key] || '';$('group').checked=ui.settings.group!==false;
  viewport=makeViewport(state.model);modelKey=state.model?.path || state.model?.url;
  applyState(state);initialized=true;for(const event of queuedEvents)eventReceived(event);queuedEvents=[];
  if(!state.model?.path&&!state.model?.url)throw new Error('模型预处理结果不可用');
  const thumbnail=state.mode==='thumbnail'||state.kind==='thumbnail',renderMode=thumbnail?'material':'solid';
  showModelLoading('loading-resources');const loaded=await viewport.load(state.model,{renderMode,facing:ui.profile.renderFacing?.yaw || 0});if(typeof viewport.setRenderMode==='function')await viewport.setRenderMode(renderMode);ui.opening=false;syncCapabilities();if(viewport.isCore){await viewport.setFacing(ui.profile.renderFacing?.yaw || 0);await viewport.frame();}$('viewport-status').hidden=true;$('viewport-status').setAttribute('aria-busy','false');
  // Completion is based on an actual loaded Mesh and rendered canvas, never just initialization.
  if(bridge.ready)await bridge.ready({png:await viewport.capture(),meshCount:loaded.meshCount,...(typeof viewport.setRenderMode==='function'?{renderMode:viewport.renderModeState.mode,materialState:viewport.renderModeState.materialState}:{}),...(viewport.isCore?{drawableCount:loaded.drawableCount,renderedFrame:loaded.renderedFrame}:{}),kind:state.mode==='thumbnail'||state.kind==='thumbnail'?'thumbnail':'viewer'});
  timer=setInterval(tickTransport,80);renderList();
  if(state.mode!=='thumbnail'&&state.kind!=='thumbnail'){const first=ui.rows.find(r=>r.compatibility==='full'&&r.association?.enabled!==false);if(first){ui.selected=new Set([first.id]);await playRow(first);}}
}

action('refresh',async()=>{if(viewport?.isCore)return;await call('scan.refresh');await restartCurrent();});action('cancel-scan',()=>call('scan.cancel'));
action('settings-toggle',()=>setDrawer('settings-panel',$('settings-panel').hidden));action('report-toggle',()=>{renderReport();setDrawer('report-panel',$('report-panel').hidden);});
for(const button of document.querySelectorAll('[data-close]'))button.addEventListener('click',()=>setDrawer(button.dataset.close,false));
action('filters-toggle',()=>{$('extra-filters').hidden=!$('extra-filters').hidden;$('filters-toggle').setAttribute('aria-expanded',String(!$('extra-filters').hidden));});
for(const id of ['search','sort','group','compatibility-filter','format-filter','folder-filter','min-duration','max-duration'])$(id).addEventListener(id==='search'||id.includes('duration')?'input':'change',settingsChanged);
for(const mode of ['solid','material'])$(`render-${mode}`).addEventListener('click',()=>selectRenderMode(mode).catch(failure));
action('retry-material',()=>selectRenderMode('material'));
$('walk-mode').addEventListener('click',()=>{try{if(viewport?.navigationState?.active)viewport.walk.exit();else viewport?.enterWalk();}catch(error){failure(error);}});
action('frame',()=>viewport?.frame());action('reset-camera',()=>{viewport?.walk?.exit();return viewport?.controls.reset();});action('previous',()=>navigate(-1));action('next',()=>navigate(1));action('play',()=>{if(viewport?.player?.action)viewport.player.setPlaying(!viewport.player.playing);else{const row=filtered().find(playable)||ui.rows.find(playable);if(row)return playRow(row);}});action('replay',()=>{viewport?.player?.seek(0);viewport?.player?.setPlaying(true);});action('frame-back',()=>step(-1));action('frame-forward',()=>step(1));
$('search-parent').addEventListener('change',async()=>{const mode=$('search-parent').checked?'parent':'current';scopeChanging=true;syncCapabilities();try{applyState(await call('candidateScope.set',{mode}));}catch(error){failure(error);}finally{scopeChanging=false;syncCapabilities();}});
$('show-skeleton').addEventListener('change',async()=>{try{if(viewport?.hasSkeleton===true)await viewport.setSkeletonVisible($('show-skeleton').checked);}catch(error){failure(error);}finally{syncCapabilities();}});
$('loop').addEventListener('change',()=>viewport?.player?.setLoop($('loop').checked));$('speed').addEventListener('change',()=>{if(viewport?.player){viewport.player.speed=Number($('speed').value);tickTransport();}});
const timeline=$('timeline');
function pauseForScrub(){viewport?.player?.setPlaying(false);}
timeline.addEventListener('pointerdown',event=>{if(event.button!==0||timeline.disabled)return;timelinePointer=event.pointerId;pauseForScrub();});
function endScrub(event){if(timelinePointer!==event.pointerId)return;timelinePointer=null;pauseForScrub();tickTransport();}
for(const type of ['pointerup','pointercancel','lostpointercapture'])window.addEventListener(type,endScrub);
timeline.addEventListener('keydown',event=>{if(!event.ctrlKey&&!event.altKey&&!event.metaKey&&['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','PageUp','PageDown','Home','End'].includes(event.code))pauseForScrub();});
timeline.addEventListener('input',()=>{const time=Number(timeline.value);pauseForScrub();viewport?.player?.seek(time);tickTransport();});$('seek-seconds').addEventListener('change',()=>viewport?.player?.seek(Number($('seek-seconds').value)));$('seek-frame').addEventListener('change',()=>viewport?.player?.seek(Number($('seek-frame').value)/Number($('step-fps').value)));
action('play-selected',()=>{const row=selectedRow();if(row)return playRow(row);});
for(const id of ['reveal','report-reveal'])action(id,()=>{const row=id==='report-reveal'?ui.rows.find(r=>r.id===ui.reportRow):selectedRow();if(row)return bridge.reveal(row.id);});
action('copy-report',()=>{const row=ui.rows.find(r=>r.id===ui.reportRow);if(row)return bridge.copyReport(row.id);});
action('associate',async()=>{if(!ui.reportRow)return;await call('association.update',{rowId:ui.reportRow,enabled:true,confirmed:true});await restartCurrent();message('已记录此动画与当前角色的关联.');});action('disassociate',async()=>{if(ui.reportRow){await call('association.update',{rowId:ui.reportRow,enabled:false});await restartCurrent();}});action('show-source',async()=>{if(!ui.reportRow)return;await call('visibility.update',{rowId:ui.reportRow,show:!ui.profile.visibility?.show?.[ui.reportRow]});renderReport(ui.rows.find(r=>r.id===ui.reportRow));});
for(const id of ['hide-associated','show-all'])$(id).addEventListener('change',()=>call('visibility.update',{enabled:$('hide-associated').checked,showAll:$('show-all').checked}).catch(failure));
$('profile-form').addEventListener('submit',event=>{event.preventDefault();saveProfile().catch(failure);});$('render-facing').addEventListener('change',()=>Promise.resolve(viewport?.setFacing(Number($('render-facing').value))).catch(failure));
$('profile-scope').addEventListener('change',async()=>{try{const file=$('profile-scope').value==='file',row=selectedRow();if(file&&!row)throw new Error('请先选择动画');const result=await call('profile.get',{scope:file?'file':'character',rowId:row?.id});populateProfile(result.profile || result,file);$('profile-saved').textContent=file?(result.sourceKind==='explicit'?'已存储的文件覆盖':'继承角色配置'):'';}catch(error){failure(error);}});
$('axes-preset').addEventListener('change',()=>{if($('axes-preset').value==='maya'){$('up-axis').value='+Y';$('front-axis').value='+Z';}else if($('axes-preset').value==='max'){$('up-axis').value='+Z';$('front-axis').value='-Y';}});
action('load-conflict',()=>{const revision=$('conflict-version').value,conflict=ui.conflicts.find(c=>c.revision===revision);if(conflict){ui.conflictBase=revision;$('profile-scope').value='character';populateProfile({...conflict.value,revision,conflicts:ui.conflicts});$('resolve-conflict').disabled=false;message('已加载所选版本. 合并将使用此版本的手动关联及当前编辑的坐标配置, 所有分支历史继续保留.');}});action('resolve-conflict',()=>saveProfile(true));
action('load-visibility-conflict',()=>{const c=ui.visibilityConflicts.find(c=>c.revision===$('visibility-conflict-version').value);if(!c)return;ui.visibilityConflictBase=c.revision;ui.visibilityConflictValue=c.value || {};$('hide-associated').checked=c.value?.enabled!==false;$('show-all').checked=!!c.value?.showAll;$('resolve-visibility-conflict').disabled=false;message('已加载所选显示版本. 合并将保留此版本的单项显示记录, 并应用当前全局开关.');});
action('resolve-visibility-conflict',async()=>{if(!ui.visibilityConflictBase)throw new Error('请先加载一个显示版本');await call('profile.resolveConflict',{scope:'visibility',parents:ui.visibilityConflicts.map(c=>c.revision),patch:{...ui.visibilityConflictValue,enabled:$('hide-associated').checked,showAll:$('show-all').checked}});await restartCurrent();message('素材显示配置冲突已合并, 所有分支历史继续保留.');});
action('run-cache-command',async()=>{const command=$('cache-command').value;await call('cache.command',{command,rowId:selectedRow()?.id});await restartCurrent();message(command==='regenerate-thumbnail'?'角色参考姿态缩略图已更新.':'已完成所选索引操作.');});
function coverPose(){if(viewport.isCore&&viewport.lastCapturePose)return viewport.lastCapturePose;const row=ui.rows.find(r=>r.id===ui.current);return row?{kind:'clip',rowId:row.id,clip_index:row.clipIndex,clip_name:row.name,time:viewport.player.state().time,fps:row.fps??null}:{kind:'base',time:0};}
action('cover',async()=>{
  if(ui.capabilities.currentFrameCover===false)throw new Error('此格式不支持设置素材封面');
  const target=viewport,player=target.player,previousMode=target.renderModeState?.requestedMode || target.renderModeState?.mode,wasPlaying=player?.playing;
  materialCaptureInProgress=true;syncCapabilities();player?.setPlaying(false);
  try{
    if(typeof target.setRenderMode==='function')await target.setRenderMode('material');
    if(target!==viewport)throw new Error('模型已更换, 请重新设置封面');
    if(typeof target.setRenderMode==='function'&&(target.renderModeState?.mode!=='material'||target.renderModeState?.materialState!=='ready'))throw new Error('材质模式已更改或尚未就绪, 本次封面未生成. 请重试.');
    const png=await target.capture(),pose=coverPose();await bridge.setCover({png,pose});message('已将当前姿态设为素材封面.');
  }finally{
    try{if(target===viewport&&previousMode&&typeof target.setRenderMode==='function'&&target.renderModeState?.requestedMode==='material')await target.setRenderMode(previousMode);}
    finally{if(target===viewport&&target.player===player&&wasPlaying)player?.setPlaying(true);materialCaptureInProgress=false;syncCapabilities();}
  }
});
document.addEventListener('keydown',event=>{if(event.defaultPrevented||viewport?.navigationState?.active||viewport?.navigationState?.pending)return;if(event.ctrlKey||event.altKey||event.metaKey||event.target.isContentEditable)return;const timelineSpace=event.target===timeline&&event.code==='Space';if(/^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(event.target.tagName)&&!timelineSpace)return;const p=viewport?.player;if(!p)return;let handled=true;switch(event.code){case 'Space':if(!event.repeat&&!$('play').disabled)$('play').click();break;case 'PageUp':navigate(-1);break;case 'PageDown':navigate(1);break;case 'ArrowLeft':step(-1);break;case 'ArrowRight':step(1);break;case 'Home':p.seek(0);break;case 'End':p.setPlaying(false);p.seek(p.state().duration);break;case 'KeyR':p.seek(0);p.setPlaying(true);break;case 'KeyF':Promise.resolve(viewport.frame()).catch(failure);break;case 'Escape':setDrawer('report-panel',false);setDrawer('settings-panel',false);break;default:handled=false;}if(handled)event.preventDefault();});
window.addEventListener('beforeunload',()=>{clearInterval(timer);clearTimeout(settingsTimer);unsubscribe?.();viewport?.dispose();});
initialize().catch(error=>{showModelFailure(error);failure(error);bridge?.failed?.({message:error.message}).catch(()=>{});});
