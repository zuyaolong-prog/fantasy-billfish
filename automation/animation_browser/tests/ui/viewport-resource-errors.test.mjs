import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import * as THREE from '../../vendor/three/build/three.module.js';
import {ClipPlayer,sourceNodeMap} from '../../ui/playback.mjs';
import {SkeletonOverlay} from '../../ui/skeleton-overlay.mjs';
import {createFullSkinLoaderPlugin} from '../../ui/full-skin.mjs';
import {DeferredMaterials,createPrimitiveIdentityPlugin} from '../../ui/deferred-materials.mjs';
import {setModelCulling} from '../../ui/model-culling.mjs';
const source=(await fs.readFile(new URL('../../ui/viewport.mjs',import.meta.url),'utf8')).replace(/^import .*;\r?\n/gm,'').replace('export class CharacterViewport','class CharacterViewport');
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function graph(){const released={geometry:0,material:0,texture:0,bitmap:0},geometry=new THREE.BoxGeometry(),texture=new THREE.Texture({close(){released.bitmap++;}}),material=new THREE.MeshStandardMaterial({map:texture}),scene=new THREE.Group(),other=new THREE.Group();scene.add(new THREE.Mesh(geometry,material));other.add(new THREE.Mesh(geometry,material));scene.children[0].userData.billfishPrimitive={meshes:0,primitives:0};const parser={json:{meshes:[{primitives:[{material:0}]}]},associations:new Map([[scene.children[0],{meshes:0,primitives:0}]]),assignFinalMaterial(){}};for(const [name,resource]of Object.entries({geometry,material,texture}))resource.addEventListener('dispose',()=>released[name]++);return{gltf:{scene,scenes:[scene,other],parser},texture,released};}
function harness(fixtures){
 class Loader{
  constructor(manager){this.manager=manager;this.fixture=fixtures.shift();this.plugins=[];}
  register(plugin){this.plugins.push(plugin);return this;}
  async loadAsync(){const fixture=this.fixture,parser={json:fixture.json||{},associations:new Map()};for(const create of this.plugins)await create(parser).beforeRoot?.();await fixture.wait;for(const uri of fixture.urls||[]){this.manager.resolveURL(uri);this.manager.itemStart(uri);if(fixture.fail)this.manager.itemError(uri);this.manager.itemEnd(uri);}return fixture.gltf;}
 }
 const context={THREE,GLTFLoader:Loader,ClipPlayer,sourceNodeMap,SkeletonOverlay,createFullSkinLoaderPlugin,DeferredMaterials,createPrimitiveIdentityPlugin,setModelCulling,URL,window:{},performance};
 const Viewport=vm.runInNewContext(source+';CharacterViewport',context,{filename:'viewport.mjs'});
 return()=>{const viewport=Object.create(Viewport.prototype),warnings=[],scene=new THREE.Scene(),displayRoot=new THREE.Group();scene.add(displayRoot);Object.assign(viewport,{disposed:false,model:null,scene,displayRoot,onWarning:warning=>warnings.push(warning),renderer:{capabilities:{maxAttributes:16},setAnimationLoop(){},dispose(){},forceContextLoss(){},domElement:{removeEventListener(){},remove(){}}},controls:{dispose(){}},resizeObserver:{disconnect(){}},frame(){this.frames=(this.frames||0)+1;},render(){this.renders=(this.renders||0)+1;}});return{viewport,warnings};};
}
test('real LoadingManager resource errors reject a partial scene and release its resources and only loader-owned blobs',async()=>{
 const partial=graph(),owned=URL.createObjectURL(new Blob(['embedded'])),borrowed=URL.createObjectURL(new Blob(['caller'])),create=harness([{...partial,urls:[owned,borrowed],fail:true,json:{images:[{bufferView:0},{uri:borrowed}]}}]),{viewport,warnings}=create();
 try{await assert.rejects(viewport.load({path:'model.glb'}),/2 个贴图或资源加载失败/);assert.equal(viewport.model,null);assert.equal(viewport.frames,undefined);assert.equal(viewport.renders,undefined);assert.equal(warnings.length,2);assert.ok(warnings.every(w=>w.code==='RESOURCE_MISSING'));assert.deepEqual(partial.released,{geometry:1,material:1,texture:1,bitmap:1});await assert.rejects(fetch(owned));assert.equal(await (await fetch(borrowed)).text(),'caller');}finally{viewport.dispose();URL.revokeObjectURL(owned);URL.revokeObjectURL(borrowed);}
});
test('successful embedded material loading retains the complete texture and does not report a resource failure',async()=>{
 const complete=graph(),owned=URL.createObjectURL(new Blob(['embedded'])),create=harness([{...complete,urls:[owned],json:{images:[{bufferView:0}]}}]),{viewport,warnings}=create();
 try{const loaded=await viewport.load({path:'model.glb'},{renderMode:'material'});assert.equal(loaded.meshCount,1);assert.equal(warnings.length,0);assert.equal(viewport.model.children[0].material.map,complete.texture);assert.ok(viewport.frames>0);assert.ok(viewport.renders>0);assert.deepEqual(complete.released,{geometry:0,material:0,texture:0,bitmap:0});}finally{viewport.dispose();URL.revokeObjectURL(owned);}
});
test('a disposed old load suppresses late resource diagnostics and cannot contaminate a new viewport load',async()=>{
 let finish;const wait=new Promise(resolve=>finish=resolve),old=graph(),fresh=graph(),owned=URL.createObjectURL(new Blob(['old embedded'])),callerModel=URL.createObjectURL(new Blob(['caller model'])),create=harness([{...old,urls:[owned],fail:true,wait,json:{images:[{bufferView:0}]}},{...fresh}]),first=create(),second=create();
 try{const pending=first.viewport.load({url:callerModel});await flush();first.viewport.dispose();await second.viewport.load({path:'new.glb'});finish();await pending;assert.equal(first.warnings.length,0);assert.equal(second.warnings.length,0);assert.equal(second.viewport.model,fresh.gltf.scene);assert.deepEqual(old.released,{geometry:1,material:1,texture:1,bitmap:1});await assert.rejects(fetch(owned));assert.equal(await (await fetch(callerModel)).text(),'caller model');}finally{finish();first.viewport.dispose();second.viewport.dispose();URL.revokeObjectURL(owned);URL.revokeObjectURL(callerModel);}
});
