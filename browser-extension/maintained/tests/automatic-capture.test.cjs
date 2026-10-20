'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const root=path.resolve(__dirname,'..');
const api=require('../maintained/video-common.js');
const read=file=>fs.readFileSync(path.join(root,file),'utf8');
const candidate=(name='A',mediaSignature='')=>({src:`https://example.com/${name}.mp4`,pageUrl:'https://example.com/player',title:name,kind:'video-element',mediaSignature});
const allow={allowed:true,code:'allowed',message:''};
function pageController({initial=[candidate()],policy=()=>Promise.resolve(allow),submit=async()=>({billfishVideoCaptureResult:true,ok:true,job_id:'job-1'})}={}) {
  let current=initial;const renders=[],notices=[],requests=[];
  const controller=api.createPageCapture({discover:()=>current,readPolicy:policy,submit:async item=>{requests.push(item);return submit(item);},render:state=>renders.push(state),notify:message=>notices.push(message)});
  return {controller,renders,notices,requests,set:items=>{current=items;}};
}
test('automatic discovery uses the current platform page without inspecting recommendation links',()=>{
  const document={querySelectorAll:()=>assert.fail('platform detection must not enumerate recommendation links/media')};
  for(const page of ['https://www.bilibili.com/video/BV15djk6XEwm/?p=2','https://www.youtube.com/watch?v=example','https://www.youtube.com/shorts/example']) {
    const found=api.discoverPage(document,page,'Current title');
    assert.equal(found.length,1);assert.equal(found[0].src,page);assert.equal(found[0].pageUrl,page);
  }
});
test('ordinary pages discover actual video/source media, deduplicate, and invalidate changed blob sources',()=>{
  const videos=[{currentSrc:'https://cdn.example/a.mp4',querySelector:()=>null},{src:'https://cdn.example/a.mp4',querySelector:()=>null},{querySelector:()=>({src:'/b.webm'})},{currentSrc:'blob:https://example.com/old',querySelector:()=>null},{querySelector:()=>null},{src:'javascript:alert(1)',querySelector:()=>null}];
  const document={querySelectorAll:selector=>{assert.equal(selector,'video');return videos;}};
  const before=api.discoverPage(document,'https://example.com/page','Page');
  assert.equal(before.length,3);
  assert.equal(before[1].src,'https://example.com/b.webm');
  assert.equal(before[2].src,'https://example.com/page');
  const key=api.candidateKey(before[2]);
  videos[3].currentSrc='blob:https://example.com/new';
  assert.notEqual(api.candidateKey(api.discoverPage(document,'https://example.com/page','Page')[2]),key);
  videos[0].src='https://cdn.example/replacement.mp4';
  assert.equal(api.discoverPage(document,'https://example.com/page','Page')[0].src,videos[0].src,'new declared source wins while currentSrc still refers to the previous video');
});
test('discovery only displays an entry; explicit click submits once and has no automatic job',async()=>{
  const h=pageController();
  await h.controller.refresh();await h.controller.refresh();
  assert.equal(h.requests.length,0);
  assert.equal(h.renders.at(-1).candidates.length,1);
  await h.controller.capture(api.candidateKey(candidate()));
  assert.equal(h.requests.length,1);assert.equal(h.requests[0].src,candidate().src);
  assert.equal(h.notices.length,0);
});
test('SPA navigation or changed media during permission lookup never submits the old candidate',async()=>{
  for(const next of [candidate('B'),candidate('A','new-blob')]) {
    let answer;let reads=0;
    const h=pageController({policy:()=>++reads===2?new Promise(resolve=>{answer=resolve;}):Promise.resolve(allow)});
    await h.controller.refresh();
    const pending=h.controller.capture(api.candidateKey(candidate()));
    h.set([next]);
    answer(allow);await pending;
    assert.equal(h.requests.length,0);
    assert.equal(api.candidateKey(h.renders.at(-1).candidates[0]),api.candidateKey(next));
  }
});
test('disabled or unknown policy hides discovery and blocks a click after a policy change',async()=>{
  for(const policy of [{allowed:false,code:'user_site_disabled',message:'用户禁用'},{allowed:false,code:'vendor_site_disabled',message:'网站禁用'},null,{}, {allowed:true}, {allowed:true,code:'unknown'}]) {
    let permission=allow;
    const h=pageController({policy:async()=>permission});
    await h.controller.refresh();permission=policy;
    await h.controller.capture(api.candidateKey(candidate()));
    assert.equal(h.requests.length,0);
    assert.equal(h.renders.at(-1).candidates.length,0);
    assert.equal(h.notices.length,1);
  }
});
test('actual top-page content startup renders a Chinese button but only its click sends capture',async()=>{
  const requests=[],roots=new Map(),timers=[],intervals=[];
  const listeners=new Map();
  const select={options:[],value:'',replaceChildren(){this.options=[];this.value='';},appendChild(option){this.options.push(option);if(!this.value)this.value=option.value;}};
  const button={addEventListener:(name,fn)=>listeners.set(name,fn)};
  const create=tag=>({tag,style:{},remove(){roots.delete(this.id)},attachShadow(){return{set innerHTML(value){this.html=value;},querySelector:selector=>selector==='select'?select:button}}});
  const document={title:'企业家应该如何吃面？',documentElement:{},addEventListener:()=>{},querySelectorAll:()=>assert.fail('platform page must not enumerate anchors'),getElementById:id=>roots.get(id)||null,createElement:create,body:{appendChild:node=>roots.set(node.id,node)}};
  const window={addEventListener:()=>{}};window.top=window.self=window;
  const location={href:'https://www.bilibili.com/video/BV15djk6XEwm/'};
  const sandbox={console,document,window,location,globalThis:null,BillfishCaptureVideo:api,setTimeout:fn=>{timers.push(fn);},setInterval:fn=>{intervals.push(fn);},MutationObserver:class{observe(){}},chrome:{runtime:{onMessage:{addListener:()=>{}},sendMessage:(message,reply)=>{requests.push(message);reply(message.billfishCaptureAction==='getVideoPolicy'?{ok:true,policy:allow}:{billfishVideoCaptureResult:true,ok:true,job_id:'clicked-job',page_notice_sent:true});}}}};
  sandbox.globalThis=sandbox;
  vm.runInNewContext(read('maintained/video-content.js'),sandbox);
  timers.shift()();await new Promise(resolve=>setImmediate(resolve));
  assert.ok(roots.has('billfish-video-capture-entry'));
  assert.equal(button.textContent,'采集视频');
  assert.equal(requests.filter(message=>message.billfishCaptureAction==='captureDetectedVideo').length,0);
  await listeners.get('click')();
  assert.equal(requests.filter(message=>message.billfishCaptureAction==='captureDetectedVideo').length,1);
  assert.equal(requests.at(-1).candidate.src,location.href);
  location.href='https://www.bilibili.com/video/BVnewvideo/';
  // Even before the SPA refresh timer runs, clicking the old option cannot send the old URL.
  await listeners.get('click')();
  assert.equal(requests.filter(message=>message.billfishCaptureAction==='captureDetectedVideo').length,1);
  assert.match(select.value,/BVnewvideo/);
  assert.equal(intervals.length,1);
});
test('failed discovery policy never displays the capture entry and transport failure never retries submission',async()=>{
  const denied=pageController({policy:async()=>{throw Error('background unavailable');}});
  await denied.controller.refresh();assert.equal(denied.renders.at(-1).candidates.length,0);assert.equal(denied.requests.length,0);
  const h=pageController({submit:async()=>{throw Error('response lost');}});
  await h.controller.refresh();await h.controller.capture(api.candidateKey(candidate()));
  assert.equal(h.requests.length,1);assert.equal(h.notices[0].outcome_unknown,true);
});
function background({policy=allow,currentUrl='https://www.bilibili.com/video/BV15djk6XEwm/',nativeResponse={ok:true,accepted:true,job_id:'native-job'}}={}) {
  let listener;const calls=[],notices=[];
  const sandbox={URL,console,Promise,Number,Object,String,Error,chrome:{runtime:{onMessage:{addListener:fn=>{listener=fn;}},sendNativeMessage:(host,message,callback)=>{calls.push({host,message});callback(nativeResponse);}},tabs:{get:async()=>({url:currentUrl}),sendMessage:async(tab,message)=>{notices.push({tab,message});}}}};
  sandbox.globalThis=sandbox;
  const context=vm.createContext(sandbox);
  sandbox.importScripts=(...files)=>files.forEach(file=>vm.runInContext(read(file),context));
  vm.runInContext(read('maintained/video-background.js'),context);
  if(policy!==null)sandbox.BillfishCaptureNative.setPolicyReader(async()=>policy);
  const sender={frameId:0,tab:{id:7,url:'https://www.bilibili.com/video/BV15djk6XEwm/'}};
  const message=data=>new Promise(resolve=>{assert.equal(listener(data,sender,resolve),true);});
  return {calls,notices,message,sender};
}
test('actual background automatic-policy request has no native side effect and explicit capture uses the same host',async()=>{
  const h=background();
  const discovery=await h.message({billfishCaptureAction:'getVideoPolicy'});
  assert.equal(discovery.policy.allowed,true);assert.equal(h.calls.length,0);
  const result=await h.message({billfishCaptureAction:'captureDetectedVideo',candidate:{src:h.sender.tab.url,pageUrl:h.sender.tab.url,title:'企业家应该如何吃面？',kind:'bilibili'}});
  assert.equal(h.calls.length,1);assert.equal(h.calls[0].host,'com.billfish.capture');
  assert.equal(h.calls[0].message.action,'captureVideo');assert.equal(h.calls[0].message.url,h.sender.tab.url);
  assert.equal(result.job_id,'native-job');assert.equal(result.page_notice_sent,true);
});
test('actual background rejects missing policy, excluded site, iframe, and stale tab without native submission',async()=>{
  for(const options of [{policy:null},{policy:{allowed:false,code:'user_site_disabled',message:'用户禁用'}},{currentUrl:'https://www.bilibili.com/video/BVother/'}]) {
    const h=background(options);
    const result=await h.message({billfishCaptureAction:'captureDetectedVideo',candidate:{src:h.sender.tab.url,pageUrl:h.sender.tab.url}});
    assert.equal(result.ok,false);assert.equal(h.calls.length,0);
  }
  const h=background();h.sender.frameId=2;
  assert.equal((await h.message({billfishCaptureAction:'getVideoPolicy'})).ok,false);
  assert.equal(h.calls.length,0);
});
test('generated policy adapter preserves separate user and vendor exclusions and rejects unreadable settings',async()=>{
  const source=read('background.js');const anchor='globalThis.BillfishCaptureNative.setPolicyReader(';
  const start=source.indexOf(anchor)+anchor.length;
  const expression=source.slice(start,source.indexOf('),wn.init(),',start));
  function provider(user,vendor,error) {
    return vm.runInNewContext('('+expression+')',{zr:{get:async()=>{if(error)throw Error('settings unavailable');return user;}},Vr:{DragHostList:'host-list'},wn:{getSitePolicies:{disableDarg:vendor}}});
  }
  assert.equal((await provider('www.bilibili.com',[])('www.bilibili.com')).code,'user_site_disabled');
  assert.equal((await provider('',['bilibili.com'])('www.bilibili.com')).code,'vendor_site_disabled');
  assert.equal((await provider('',[])('www.bilibili.com')).allowed,true);
  assert.equal((await provider('',null)('www.bilibili.com')).allowed,false);
  await assert.rejects(provider('',[],true)('www.bilibili.com'));
  const h=background({policy:null});
  assert.equal((await h.message({billfishCaptureAction:'getVideoPolicy'})).policy.allowed,false);
});
