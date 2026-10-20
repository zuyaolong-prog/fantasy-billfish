'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
const root = path.resolve(__dirname,'..');
const api = require('../maintained/video-common.js');
const read = file => fs.readFileSync(path.join(root,file),'utf8');
function element({tag='IMG',href,src,currentSrc,alt='',title='',video=null,source=null}={}) {
  const node={nodeName:tag,href,src,currentSrc,alt,title,querySelector:()=>source};
  const anchor=href?node:null;
  node.closest=selector=>selector==='a[href]'?anchor:selector==='video'?(tag==='VIDEO'?node:video):null;
  return node;
}
test('recognized videos and direct media do not classify ordinary image/page links',()=>{
  for(const url of ['https://www.youtube.com/watch?v=abc','https://m.youtube.com/shorts/abc','https://youtu.be/abc','https://www.bilibili.com/video/BV1xx411c7mD/?p=2','https://b23.tv/abc','https://cdn.example/a.mp4?token=abc','https://cdn.example/a.m3u8']) assert.ok(api.classifyUrl(url),url);
  for(const url of ['https://youtube.com/','https://www.youtube.com/results?search_query=a','https://youtube.com.evil.invalid/watch?v=x','https://bilibili.com.evil.invalid/video/BV123','https://example.com/a.jpg','https://example.com/article','javascript:alert(1)','blob:https://youtube.com/x','file:///a.mp4']) assert.equal(api.classifyUrl(url),null,url);
});
test('video thumbnail uses its watch link, ordinary image stays in original image path',()=>{
  const thumb=element({href:'https://www.youtube.com/watch?v=abc',src:'https://i.ytimg.com/abc.jpg',alt:'Video title'});
  assert.equal(api.fromElement(thumb,'https://youtube.com/','Home').src,thumb.href);
  assert.equal(api.fromElement(element({src:'https://example.com/img.jpg'}),'https://example.com/','Article'),null);
  assert.equal(api.fromElement(element({href:'https://example.com/article',src:'https://example.com/img.jpg'}),'https://example.com/','Article'),null);
});
test('platform players use watch URL and direct videos preserve media URL',()=>{
  const blob=element({tag:'VIDEO',currentSrc:'blob:https://www.bilibili.com/blob'});
  const page='https://www.bilibili.com/video/BV1xx411c7mD/?p=2';
  assert.equal(api.fromElement(blob,page,'Bili').src,page);
  const direct=element({tag:'VIDEO',currentSrc:'https://cdn.example/stream?id=123'});
  assert.equal(api.fromElement(direct,'https://example.com/player','Direct').src,direct.currentSrc);
  assert.equal(api.fromElement(blob,'https://example.com/player','Blob player').src,'https://example.com/player');
});
test('native message includes explicit URL/context and no browser session material',()=>{
  assert.deepEqual(api.serializeCapture({src:'https://cdn.example/a.mp4?x=1',pageUrl:'https://example.com/player',title:'Example',cookies:'secret',headers:{Cookie:'secret'}}),{action:'captureVideo',url:'https://cdn.example/a.mp4?x=1',page_url:'https://example.com/player',title:'Example',media_url:'https://cdn.example/a.mp4?x=1'});
  assert.throws(()=>api.serializeCapture({src:'file:///C:/a.mp4'}));
});
async function nativeHarness(response,lastError) {
  const calls=[];const notifications=[];
  const sandbox={URL,console,Promise,Number,Object,String,Error,chrome:{runtime:{lastError,sendNativeMessage:(host,message,callback)=>{calls.push({host,message});callback(response);}},tabs:{sendMessage:async(tab,message)=>notifications.push({tab,message})}}};
  sandbox.globalThis=sandbox;
  const context=vm.createContext(sandbox);
  sandbox.importScripts=(...files)=>files.forEach(file=>vm.runInContext(read(file),context));
  vm.runInContext(read('maintained/video-background.js'),context);
  const result=await sandbox.BillfishCaptureNative.capture({src:'https://youtu.be/abc',pageUrl:'https://youtu.be/abc',title:'Example',tabId:7});
  return {calls,notifications,result};
}
test('native bridge accepts jobs and reports host errors without claiming import success',async()=>{
  const good=await nativeHarness({ok:true,accepted:true,job_id:'job-1'});
  assert.equal(good.calls[0].host,'com.billfish.capture');
  assert.equal(good.calls[0].message.url,'https://youtu.be/abc');
  assert.equal(good.notifications[0].tab,7);
  assert.equal(good.result.job_id,'job-1');
  assert.equal(good.result.ok,true);
  const bad=await nativeHarness(null,{message:'Native host not found'});
  assert.equal(bad.result.ok,false);
  assert.match(bad.result.error.message,/Native host not found/);
  assert.equal(bad.result.error.code,'native_host_error');
  const rejected=await nativeHarness({ok:false,error:{code:'bad_url',message:'Unsupported URL'}});
  assert.equal(rejected.result.ok,false);
  assert.match(rejected.result.error.message,/Unsupported URL/);
  assert.equal(rejected.result.error.code,'bad_url');
});
test('uncertain native submission retains job identity and displays it without retrying',async()=>{
  const failed=await nativeHarness({ok:false,job_id:'already-submitted-job',error:{code:'online_timeout',message:'Capture outcome is unknown. Query jobs.get using job_id before retrying.'}});
  assert.equal(failed.calls.length,1);
  assert.equal(failed.result.job_id,'already-submitted-job');
  assert.equal(failed.result.error.code,'online_timeout');
  assert.equal(failed.result.outcome_unknown,true);
  assert.equal(failed.notifications[0].message.job_id,'already-submitted-job');
  let handler;let rendered;let timers=0;
  const sandbox={console,globalThis:null,chrome:{runtime:{onMessage:{addListener:fn=>{handler=fn;}}}},document:{addEventListener:()=>{},getElementById:()=>null,createElement:()=>({style:{}}),body:{appendChild:node=>{rendered=node;}}},setTimeout:()=>{timers++;}};
  sandbox.globalThis=sandbox;
  vm.runInNewContext(read('maintained/video-content.js'),sandbox);
  handler(failed.result);
  assert.match(rendered.textContent,/already-submitted-job/);
  assert.match(rendered.textContent,/请先检查 Billfish/);
  assert.match(rendered.textContent,/避免重复下载/);
  assert.equal(timers,0);
});
test('stable unpacked identity and permissions match native host registration',()=>{
  const manifest=JSON.parse(read('manifest.json'));
  const identity=JSON.parse(read('identity.json'));
  const id=crypto.createHash('sha256').update(Buffer.from(manifest.key,'base64')).digest('hex').slice(0,32).replace(/[0-9a-f]/g,c=>String.fromCharCode(97+parseInt(c,16)));
  assert.equal(id,identity.extension_id);
  assert.equal(identity.allowed_origin,`chrome-extension://${id}/`);
  assert.equal(manifest.update_url,undefined);
  assert.ok(manifest.permissions.includes('nativeMessaging'));
  assert.ok(!manifest.permissions.includes('cookies'));
  assert.deepEqual(manifest.content_scripts[0].js.slice(0,2),['./maintained/video-common.js','./maintained/video-content.js']);
});
test('vendor image import remains byte-for-byte after the one video intercept',()=>{
  const provenance=JSON.parse(read('provenance.json'));
  const source=fs.readFileSync(path.join(provenance.source_path,'background.js'),'utf8');
  const fork=read('background.js');
  const anchor='async directSendImage(e,t){';
  const end='async sendBathImport';
  const original=source.slice(source.indexOf(anchor),source.indexOf(end,source.indexOf(anchor)));
  const patched=fork.slice(fork.indexOf(anchor),fork.indexOf(end,fork.indexOf(anchor)));
  assert.equal(patched.replace('if("video"===e.contentType)return globalThis.BillfishCaptureNative.capture(e);',''),original);
  // All untouched vendor assets remain exactly the artifact bytes that were copied.
  for(const [file,hash] of Object.entries(provenance.source_hashes)) {
    if(['manifest.json','background.js','content.js'].includes(file)) continue;
    assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex'),hash,file);
  }
});
test('generated content and background remain parseable with cold-start video drag support',()=>{
  new vm.Script(read('content.js'));
  new vm.Script(read('background.js'));
  const content=read('content.js');
  const start=content.indexOf('async setMessage(e){');
  const section=content.slice(start,content.indexOf('removeDom(e){',start));
  assert.ok(section.indexOf('BillfishCaptureVideo.finalizeDrag')<section.indexOf('Z.GetBillfishStatus'));
  assert.equal((section.match(/type:"image"/g)||[]).length,3);
});
function actualDragHarness() {
  const text=read('content.js');
  const extract=(start,end)=>text.slice(text.indexOf(start)+start.length,text.indexOf(end,text.indexOf(start)+start.length));
  const handlers={start:extract('this.onDragStart=',',this.DeleteDom='),enter:extract('this.onDragEnter=',',this.onDragStart='),end:extract('this.onDragEnd=','}init(){this.getPictures()}')};
  const messageStart=text.indexOf('async setMessage(e){');
  const messageMethod=text.slice(messageStart,text.indexOf('removeDom(e){',messageStart));
  const policies=[];
  const nodes=new Map();const listeners=[];const calls=[];const notices=[];
  const jq={off(){return this},on(){return this},css(){return this}};
  const sandbox={URL,Promise,console,location:{host:'127.0.0.1:48271',href:'http://127.0.0.1:48271/'},document:{title:'HLS QA',documentElement:{style:{}},addEventListener:(name,fn)=>listeners.push({name,fn}),getElementById:id=>nodes.get(id)||null},Ge:()=>jq,setTimeout:()=>1,clearTimeout:()=>{},E:{disableDrop:2,mobileDrop:0,downDrop:1},F:{DirectionDrag:'drag',DownDrag:'down'},Z:{GetHostIncludes:29,SendToSa:17,SendSingleCollect:6,GetBillfishStatus:12},T:{Right:0,Left:1,Bottom:2,Top:3},BillfishCaptureVideo:api,BillfishCaptureVideoUi:{decorate:frame=>{frame.kind='video';frame.foldersVisible=false;}},BillfishCaptureVideoNotice:message=>notices.push(message),Je:()=>{},et:()=>{},ie:status=>status.bfBindStatus===2&&status.bfLocalOnline};
  sandbox.globalThis=sandbox;
  const context=vm.createContext(sandbox);
  const state={settings:{dropType:0},backCaller:{call:(id,payload)=>{if(id===29)return policies.shift().promise;if(id===12)return Promise.resolve({output:{bfBindStatus:2,bfLocalOnline:true}});calls.push({id,payload});return Promise.resolve({});}},direction:[],dragTransfer:{transfer:event=>event.target},scollShow(){if(nodes.has('newSocllMouse'))return;this.created=true;for(const id of ['origin-shows','originShow','mouse-shows']) nodes.set(id,{id,style:{display:id==='mouse-shows'?'flex':'none'}});nodes.set('newSocllMouse',{id:'newSocllMouse',kind:'image',foldersVisible:true});},setStyles:(style,values)=>Object.assign(style,values),removeDom(node){if(node&&nodes.get(node.id)===node)nodes.delete(node.id)},isOPenbf(){}};
  for(const [name,source] of Object.entries(handlers)) state[{start:'onDragStart',enter:'onDragEnter',end:'onDragEnd'}[name]]=vm.runInContext('(function(){return '+source+'})',context).call(state);
  state.setMessage=vm.runInContext('({'+messageMethod+'}).setMessage',context).bind(state);
  function policy() {let resolve;const promise=new Promise(answer=>{resolve=answer;});const entry={promise,resolve};policies.push(entry);return output=>resolve({output});}
  return {state,nodes,listeners,calls,notices,policy,enter:inside=>state.onDragEnter({target:{className:inside?'origin-mouse-show':'outside-page'}}),end:target=>state.onDragEnd({target,clientX:670,clientY:660}),start:target=>state.onDragStart({target,clientX:430,clientY:660}),flush:()=>new Promise(resolve=>setImmediate(resolve)),submitted:()=>calls.filter(call=>call.id===6)};
}
async function actualDragCycle(policyOutput,inside=true) {
  const h=actualDragHarness();
  const resolvePolicy=h.policy();
  const target=element({tag:'A',href:'http://127.0.0.1:48271/master.m3u8',title:'HLS QA'});
  const start=h.start(target);
  assert.equal(h.state.created,true,'panel created synchronously before host policy reply');
  assert.ok(h.listeners.some(item=>item.name==='dragend'),'dragend registered before host policy reply');
  await h.enter(inside);
  await h.end(target);
  assert.equal(h.nodes.has('newSocllMouse'),false,'completed video frame removed before waiting');
  assert.equal(h.submitted().length,0,'no submission before policy resolves');
  resolvePolicy(policyOutput);
  await start;
  await h.flush();
  return {submitted:h.submitted(),notices:h.notices,nodes:h.nodes};
}
test('actual fast video drag waits for host permission at submit and preserves exclusion',async()=>{
  const allowed=await actualDragCycle(undefined);
  assert.equal(allowed.submitted.length,1);
  assert.equal(allowed.submitted[0].payload.type,'video');
  assert.equal(allowed.submitted[0].payload.src,'http://127.0.0.1:48271/master.m3u8');
  const excluded=await actualDragCycle(true);
  assert.equal(excluded.submitted.length,0);
  assert.match(excluded.notices[0].error.message,/禁用采集/);
  assert.equal(excluded.nodes.has('newSocllMouse'),false);
  const outside=await actualDragCycle(false,false);
  assert.equal(outside.submitted.length,0);
  assert.equal((await api.checkPolicy(Promise.reject(new Error('background unavailable')))).allowed,false);
  assert.equal((await api.checkPolicy(Promise.resolve(null))).allowed,false);
});
test('actual overlapping video drags finalize their own URL once and leave the next frame intact',async()=>{
  const h=actualDragHarness();
  const a=element({tag:'A',href:'http://127.0.0.1:48271/A.mp4',title:'Video A'});
  const b=element({tag:'A',href:'http://127.0.0.1:48271/B.mp4',title:'Video B'});
  const allowA=h.policy();await h.start(a);await h.enter(true);
  const recordA=h.state.captureDrag;
  await h.end(a);
  assert.equal(recordA.finalized,true);
  const allowB=h.policy();await h.start(b);await h.enter(true);
  const frameB=h.nodes.get('newSocllMouse');
  allowA(false);await h.flush();
  assert.equal(h.submitted().length,1);
  assert.equal(h.submitted()[0].payload.src,a.href);
  assert.equal(h.nodes.get('newSocllMouse'),frameB);
  assert.equal(h.state.collectImgSrc,b.href);
  await api.finalizeDrag(recordA,true,{},()=>assert.fail('already finalized cleanup'),()=>assert.fail('already finalized send'),()=>assert.fail('already finalized notice'));
  assert.equal(h.submitted().length,1);
  await h.end(b);allowB(false);await h.flush();
  assert.equal(h.submitted().length,2);
  assert.equal(h.submitted()[1].payload.src,b.href);
});
test('actual pending video followed by image never reuses the video frame or clears the image',async()=>{
  for(const finishImageFirst of [false,true]) {
    const h=actualDragHarness();
    const a=element({tag:'A',href:'http://127.0.0.1:48271/A.mp4',title:'Video A'});
    const b=element({tag:'IMG',src:'http://127.0.0.1:48271/B.jpg',alt:'Image B'});
    const allowA=h.policy();await h.start(a);await h.enter(true);await h.end(a);
    assert.equal(h.nodes.has('newSocllMouse'),false);
    const allowB=h.policy();const imageStart=h.start(b);
    assert.equal(h.state.captureVideoCandidate,null,'image owns the gesture before its policy reply');
    allowB(false);await imageStart;await h.enter(true);
    const imageFrame=h.nodes.get('newSocllMouse');
    assert.equal(imageFrame.kind,'image');assert.equal(imageFrame.foldersVisible,true);
    if(finishImageFirst) {await h.end(b);await h.flush();}
    allowA(false);await h.flush();
    if(!finishImageFirst) {
      assert.equal(h.nodes.get('newSocllMouse'),imageFrame);
      assert.equal(h.state.collectImgSrc,b.src);
      await h.end(b);await h.flush();
    }
    const payloads=h.submitted().map(call=>call.payload);
    assert.equal(payloads.length,2);
    assert.equal(payloads.find(payload=>payload.type==='video').src,a.href);
    assert.equal(payloads.find(payload=>payload.type==='image').src,b.src);
  }
});
test('actual stale image start cannot replace a newer video drag after its policy reply',async()=>{
  const h=actualDragHarness();
  const image=element({tag:'IMG',src:'http://127.0.0.1:48271/old.jpg'});
  const video=element({tag:'A',href:'http://127.0.0.1:48271/new.mp4'});
  const allowImage=h.policy();const oldStart=h.start(image);
  const allowVideo=h.policy();await h.start(video);await h.enter(true);
  const frame=h.nodes.get('newSocllMouse');
  allowImage(false);await oldStart;
  assert.equal(h.nodes.get('newSocllMouse'),frame);
  assert.equal(h.state.collectImgSrc,video.href);
  await h.end(video);allowVideo(false);await h.flush();
  assert.equal(h.submitted().length,1);
  assert.equal(h.submitted()[0].payload.src,video.href);
});
test('video drag frame labels the real destination and hides ineffective folder choices',()=>{
  let runtimeHandler;
  const sandbox={globalThis:null,document:{addEventListener:()=>{}},chrome:{runtime:{onMessage:{addListener:handler=>{runtimeHandler=handler;}}}}};
  sandbox.globalThis=sandbox;
  vm.runInNewContext(read('maintained/video-content.js'),sandbox);
  const folders=[{style:{}},{style:{}}];const panel={style:{}};
  const first=[{closest:()=>true},{closest:()=>false}];const next=[{},{},{},{}];const down=[{},{},{},{}];
  const frame={dataset:{},querySelector:()=>panel,querySelectorAll:selector=>({'.file-list':folders,'.mouse-show-first':first,'.mouse-show-next':next,'.p-up-here':down}[selector])};
  sandbox.BillfishCaptureVideoUi.decorate(frame);
  assert.ok(folders.every(node=>node.style.display==='none'));
  assert.equal(frame.dataset.captureType,'video');
  assert.equal(panel.style.width,'240px');
  assert.match(first[1].textContent,/选择视频分辨率/);
  assert.match(next.map(node=>node.textContent).join(''),/浏览器采集文件夹/);
});
