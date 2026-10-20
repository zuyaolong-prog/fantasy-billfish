'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const directory=path.resolve(__dirname,'../../ui'),html=fs.readFileSync(path.join(directory,'index.html'),'utf8'),source=fs.readFileSync(path.join(directory,'app.mjs'),'utf8').replace(/^import .*;\r?\n/gm,'');
class Element{
 constructor(tag='DIV'){this.tagName=tag;this.children=[];this.dataset={};this.hidden=false;this.disabled=false;this.value='';this.textContent='';this.scrollTop=0;this.classList={toggle(){},contains(){return false;}};}
 append(...children){this.children.push(...children);}replaceChildren(...children){this.children=children;}add(child){this.children.push(child);}setAttribute(name,value){this[name]=value;}addEventListener(){}
}
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
async function page(){
 const elements=Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m=>{const element=new Element();element.id=m[1];return [m[1],element];})),initial=deferred(),model=deferred(),ready=[],failures=[];let onEvent,viewport;
 elements['step-fps'].value='30';elements['profile-scope'].value='character';
 class Viewport{constructor(){viewport=this;this.model=null;this.controls={reset(){}};this.player={state:()=>({rowId:null,time:0,duration:0,speed:1}),stop(){},setPlaying(){},seek(){}};}async load(){await model.promise;this.model={};return {meshCount:1};}frame(){}setFacing(){}capture(){return 'data:image/png;base64,AA==';}dispose(){}}
 const bridge={initialize:()=>initial.promise,onEvent:fn=>(onEvent=fn,()=>{}),ready:async value=>ready.push(value),failed:async error=>failures.push(error),call:async()=>({})};
 const {visibleRows}=await import('../../ui/list.mjs');
 const context={CharacterViewport:Viewport,CoreViewport:Viewport,visibleRows,compatibilityNames:{full:'完全兼容'},seconds:String,fpsLabel:String,document:{body:new Element('BODY'),getElementById:id=>elements[id],createElement:tag=>new Element(tag.toUpperCase()),createDocumentFragment:()=>new Element(),querySelectorAll:()=>[],addEventListener(){}},window:{billfishAnimationBrowser:bridge,addEventListener(){}},Option:class extends Element{constructor(text,value){super('OPTION');this.textContent=text;this.value=value;}},setInterval:()=>1,clearInterval(){},setTimeout:()=>1,clearTimeout(){},console:{error(){}},CSS:{escape:String},requestAnimationFrame:fn=>fn()};
 vm.runInNewContext(source,context,{filename:'app.mjs'});await flush();
 return {elements,initial,model,ready,failures,get viewport(){return viewport;},async emit(event){onEvent(event);await flush();},async prepared(){initial.resolve({source:'E:/Library/latest.glb',model:{path:'derived.glb'},rows:[],profile:{}});await flush();await flush();}};
}
test('pending initialization shows the actual file and phase without enabling model actions or reporting rendered completion',async()=>{
 const p=await page(),e=p.elements;assert.equal(e['viewport-status'].hidden,false);assert.equal(e['viewport-status']['aria-busy'],'true');for(const id of ['refresh','frame','reset-camera','cover','settings-toggle','report-toggle','play'])assert.equal(e[id].disabled,true,id);
 await p.emit({type:'loading',payload:{stage:'checking-source',source:'E:/Library/latest.glb',name:'Latest Model',libraryContext:{libraryRoot:'E:/Library',libraryName:'素材库',relativeFolder:'环境'}}});
 assert.equal(e['model-name'].textContent,'Latest Model');assert.equal(e['library-directory'].textContent,'目录: 环境');assert.match(e['viewport-status'].textContent,/验证/);assert.equal(p.ready.length,0);
 await p.emit({type:'loading',payload:{stage:'preparing-model',source:'E:/Library/latest.glb',name:'Latest Model'}});assert.match(e['viewport-status'].textContent,/准备/);assert.equal(p.ready.length,0);
 await p.prepared();assert.match(e['viewport-status'].textContent,/加载模型资源/);assert.equal(e['viewport-status'].hidden,false);assert.equal(e.cover.disabled,true);assert.equal(p.ready.length,0);
 p.model.resolve();await flush();await flush();assert.equal(e['viewport-status'].hidden,true);assert.equal(e['viewport-status']['aria-busy'],'false');assert.equal(e.frame.disabled,false);assert.equal(e.cover.disabled,false);assert.equal(p.ready.length,1);assert.equal(p.ready[0].meshCount,1);
 await p.emit({type:'loading',payload:{stage:'checking-source',source:'E:/Library/old.glb',name:'Old'}});assert.equal(e['viewport-status'].hidden,true);assert.notEqual(e['model-name'].textContent,'Old');
});
test('a genuine initialization failure leaves an honest error and never sends rendered readiness',async()=>{
 const p=await page();await p.emit({type:'loading',payload:{stage:'preparing-model',source:'E:/Library/broken.glb',name:'Broken'}});p.initial.reject(Error('Invalid model data'));await flush();await flush();
 assert.equal(p.elements['viewport-status'].hidden,false);assert.equal(p.elements['viewport-status']['aria-busy'],'false');assert.match(p.elements['viewport-status'].textContent,/模型加载失败\nInvalid model data/);assert.equal(p.elements.cover.disabled,true);assert.equal(p.ready.length,0);assert.equal(p.failures.length,1);assert.equal(p.failures[0].message,'Invalid model data');
});
test('a renderer resource-load failure is shown as an error and cannot become rendered readiness',async()=>{
 const p=await page();await p.prepared();p.model.reject(Error('模型有 1 个贴图或资源加载失败'));await flush();await flush();assert.equal(p.elements['viewport-status'].hidden,false);assert.equal(p.elements['viewport-status']['aria-busy'],'false');assert.match(p.elements['viewport-status'].textContent,/模型加载失败\n模型有 1 个贴图或资源加载失败/);assert.equal(p.elements.cover.disabled,true);assert.equal(p.ready.length,0);assert.equal(p.failures.length,1);
});


test('native admission shows a generic shell before source preparation without inventing its identity or directory',async()=>{
 const p=await page(),e=p.elements;
 await p.emit({type:'loading',payload:{stage:'waiting-native',itemId:'42',source:'',name:'',libraryContext:{libraryRoot:'E:/Library',libraryName:'素材库',relativeFolder:''}}});
 assert.equal(e['model-name'].textContent,'模型预览');assert.doesNotMatch(e['viewport-status'].textContent,/42/);assert.match(e['library-directory'].textContent,/待确认/);assert.doesNotMatch(e['scope-label'].textContent,/根目录/);assert.equal(e['viewport-status']['aria-busy'],'true');assert.equal(e.cover.disabled,true);assert.equal(p.ready.length,0);
 await p.emit({type:'loading',payload:{stage:'checking-source',source:'E:/Library/model.glb',name:'Model',libraryContext:{libraryRoot:'E:/Library',libraryName:'素材库',relativeFolder:''}}});
 assert.equal(e['model-name'].textContent,'Model');assert.match(e['library-directory'].textContent,/库根目录/);
});

test('a native preparation error remains visible in the pending shell without claiming completion',async()=>{
 const p=await page();await p.emit({type:'loading',payload:{stage:'waiting-native',itemId:'42'}});await p.emit({type:'error',error:{code:'source_missing',message:'Source unavailable'}});
 assert.equal(p.elements['viewport-status'].hidden,false);assert.equal(p.elements['viewport-status']['aria-busy'],'false');assert.match(p.elements['viewport-status'].textContent,/Source unavailable/);assert.equal(p.ready.length,0);assert.equal(p.elements.cover.disabled,true);assert.match(p.elements.message.textContent,/Source unavailable/);
});


test('a completed viewer reports a new native intent as pending until verified reuse really finishes',async()=>{
 const p=await page(),e=p.elements;await p.prepared();p.model.resolve();await flush();await flush();assert.equal(p.ready.length,1);
 await p.emit({type:'loading',payload:{stage:'waiting-native',libraryContext:{libraryRoot:'E:/Library',libraryName:'Library',relativeFolder:''}}});assert.equal(e['viewport-status'].hidden,false);assert.equal(e['viewport-status']['aria-busy'],'true');assert.equal(e.frame.disabled,true);assert.equal(e['model-name'].textContent,'模型预览');
 await p.emit({type:'loading',payload:{stage:'checking-source',source:'E:/Library/latest.glb',name:'Latest'}});assert.equal(e['model-name'].textContent,'Latest');assert.equal(e['viewport-status']['aria-busy'],'true');
 await p.emit({type:'loading',payload:{stage:'ready',source:'E:/Library/latest.glb',name:'Latest'}});assert.equal(e['viewport-status'].hidden,true);assert.equal(e['viewport-status']['aria-busy'],'false');assert.equal(e.frame.disabled,false);assert.equal(p.ready.length,1);
 await p.emit({type:'loading',payload:{stage:'waiting-native'}});await p.emit({type:'error',error:{message:'Preparation failed'}});assert.equal(e['viewport-status'].hidden,false);assert.equal(e['viewport-status']['aria-busy'],'false');assert.match(e['viewport-status'].textContent,/Preparation failed/);assert.equal(e.frame.disabled,true);assert.equal(p.ready.length,1);
});


test('coordinate reload failure replaces the busy overlay with a real error',async()=>{
 const p=await page(),e=p.elements;await p.prepared();p.model.resolve();await flush();await flush();p.viewport.constructor.prototype.load=async()=>{throw Error('Coordinate reload failed');};
 await p.emit({type:'state',payload:{model:{path:'changed.glb'}}});await flush();assert.equal(e['viewport-status'].hidden,false);assert.equal(e['viewport-status']['aria-busy'],'false');assert.match(e['viewport-status'].textContent,/Coordinate reload failed/);assert.equal(e.frame.disabled,true);assert.equal(p.ready.length,1);
});
