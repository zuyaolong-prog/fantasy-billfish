import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../vendor/three/build/three.module.js';
import {DeferredMaterials,collectMaterialBindings,attachMaterialLibrary,createPrimitiveIdentityPlugin} from '../../ui/deferred-materials.mjs';
import {createFullSkinLoaderPlugin} from '../../ui/full-skin.mjs';
import {fullSkinFixture} from './full-skin-fixture.mjs';
import {pinnedLoader} from './import-pinned-loader.mjs';
const {GLTFLoader}=await pinnedLoader(),renderer={capabilities:{maxAttributes:16}};
async function scene(options={}){return new GLTFLoader().register(createPrimitiveIdentityPlugin()).register(createFullSkinLoaderPlugin({renderer})).parseAsync(fullSkinFixture(options).bytes.buffer,'');}
async function library(){const gltf=await new GLTFLoader().parseAsync(JSON.stringify({asset:{version:'2.0'},scenes:[{nodes:[]}],scene:0,materials:[{pbrMetallicRoughness:{baseColorFactor:[.1,.3,.8,1],metallicFactor:.2,roughnessFactor:.6},doubleSided:true}]}),'');return{parser:gltf.parser,materials:await gltf.parser.getDependencies('material')};}
const meshes=gltf=>{const result=[];gltf.scene.traverse(o=>{if(o.isMesh)result.push(o);});return result;};
const bytes=attribute=>Buffer.from(attribute.array.buffer,attribute.array.byteOffset,attribute.array.byteLength).toString('hex');
function repack(fixture,json){const source=new DataView(fixture.bytes.buffer),oldJsonLength=source.getUint32(12,true),bin=fixture.bytes.subarray(20+oldJsonLength+8),text=new TextEncoder().encode(JSON.stringify(json)),jsonLength=(text.length+3)&~3,result=new Uint8Array(12+8+jsonLength+8+bin.length),view=new DataView(result.buffer);view.setUint32(0,0x46546c67,true);view.setUint32(4,2,true);view.setUint32(8,result.length,true);view.setUint32(12,jsonLength,true);view.setUint32(16,0x4e4f534a,true);result.fill(32,20,20+jsonLength);result.set(text,20);const offset=20+jsonLength;view.setUint32(offset,bin.length,true);view.setUint32(offset+4,0x004e4942,true);result.set(bin,offset+8);return result.buffer;}

test('late material assignment keeps all five influences and morph/skin objects unchanged',async()=>{
 const gltf=await scene({morph:true,weightScale:2}),mesh=meshes(gltf)[0],geometry=mesh.geometry,skeleton=mesh.skeleton,weights=['skinWeight','weights_1'].map(k=>bytes(geometry.getAttribute(k))),lib=await library();
 geometry.deleteAttribute('tangent');geometry.setAttribute('color',new THREE.Float32BufferAttribute(Array(12).fill(.5),3));
 mesh.morphTargetInfluences[0]=.6;const vertex=mesh.getVertexPosition(0,new THREE.Vector3());
 const bindings=collectMaterialBindings(gltf);attachMaterialLibrary(bindings,lib.parser,lib.materials,renderer);
 assert.equal(mesh.geometry,geometry);assert.equal(mesh.skeleton,skeleton);assert.equal(mesh.morphTargetInfluences[0],.6);assert.deepEqual(['skinWeight','weights_1'].map(k=>bytes(geometry.getAttribute(k))),weights);assert.ok(mesh.getVertexPosition(0,new THREE.Vector3()).distanceTo(vertex)<1e-10);
 const material=bindings[0].material;assert.ok(material.customProgramCacheKey().includes('billfish-full-skin'));assert.equal(material.normalScale.y,-1);assert.equal(material.vertexColors,true);assert.equal(material.side,THREE.DoubleSide);
});
test('shared material rebind patches five-influence primitive without changing four-influence primitive',async()=>{
 const gltf=await scene({sharedFourPrimitive:true}),bindings=collectMaterialBindings(gltf),lib=await library();attachMaterialLibrary(bindings,lib.parser,lib.materials,renderer);
 const full=bindings.find(b=>b.mesh.userData.billfishFullSkin),four=bindings.find(b=>!b.mesh.userData.billfishFullSkin);assert.notEqual(full.material,four.material);assert.ok(full.material.customProgramCacheKey().includes('billfish-full-skin'));assert.ok(!four.material.customProgramCacheKey().includes('billfish-full-skin'));
 assert.throws(()=>attachMaterialLibrary(bindings,lib.parser,lib.materials,{capabilities:{maxAttributes:4}}),/GPU/);
});
test('Solid returns immediately and a late material request cannot override a newer Solid choice',async()=>{
 const gltf=await scene(),lib=await library();let finish,loads=0,renders=0;const pending=new Promise(resolve=>finish=resolve),controller=new DeferredMaterials({gltf,renderer,url:'materials.gltf',render:()=>renders++,loadLibrary:()=>{loads++;return pending;}}),mesh=meshes(gltf)[0],solid=mesh.material,geometry=mesh.geometry;
 assert.equal(loads,0);const request=controller.setMode('material');assert.equal(controller.state().mode,'solid');assert.equal(controller.state().materialState,'loading');await controller.setMode('solid');finish(lib);await request;
 assert.equal(controller.state().mode,'solid');assert.equal(mesh.material,solid);assert.equal(mesh.geometry,geometry);assert.equal(loads,1);await controller.setMode('material');assert.equal(controller.state().mode,'material');assert.notEqual(mesh.material,solid);assert.ok(renders>=2);controller.dispose();assert.equal(mesh.material,solid);
});
test('closing a model releases late material resources and suppresses late errors and warnings',async()=>{
 const gltf=await scene(),lib=await library();let finish,disposed=0,warnings=0,renders=0;lib.materials[0].addEventListener('dispose',()=>disposed++);const controller=new DeferredMaterials({gltf,renderer,url:'materials.gltf',render:()=>renders++,onWarning:()=>warnings++,loadLibrary:()=>new Promise(resolve=>finish=resolve)}),request=controller.setMode('material');controller.dispose();finish(lib);await request;assert.equal(disposed,1);assert.equal(renders,0);assert.equal(warnings,0);
 let reject,emit;const old=new DeferredMaterials({gltf:await scene(),renderer,url:'bad.gltf',render(){},onWarning:()=>warnings++,loadLibrary:(_,options)=>{emit=options.onWarning;return new Promise((_,r)=>reject=r);}}),failed=old.setMode('material');old.dispose();emit({message:'late'});reject(new Error('late resource error'));await failed;assert.equal(warnings,0);
});
test('material failure preserves Solid and can be retried without changing geometry or camera objects',async()=>{
 const gltf=await scene(),lib=await library();let attempts=0;const controller=new DeferredMaterials({gltf,renderer,url:'materials.gltf',render(){},loadLibrary:async()=>{if(++attempts===1)throw new Error('missing original texture');return lib;}}),mesh=meshes(gltf)[0],solid=mesh.material,geometry=mesh.geometry;
 await assert.rejects(controller.setMode('material'),/missing original texture/);assert.equal(controller.state().mode,'solid');assert.equal(controller.state().materialState,'error');assert.equal(mesh.material,solid);await controller.setMode('material');assert.equal(controller.state().mode,'material');assert.equal(mesh.geometry,geometry);assert.equal(attempts,2);
});
test('a shader failure rolls back to the rendered mode and retries the existing full material library',async()=>{
 const gltf=await scene(),lib=await library();let loads=0,fail=true;const mesh=meshes(gltf)[0],solid=mesh.material,controller=new DeferredMaterials({gltf,renderer,url:'materials.gltf',render(){if(mesh.material!==solid&&fail)throw new Error('shader compile failed');},loadLibrary:async()=>{loads++;return lib;}});
 await assert.rejects(controller.setMode('material'),/shader compile failed/);assert.equal(controller.state().mode,'solid');assert.equal(mesh.material,solid);assert.equal(controller.state().materialState,'error');fail=false;await controller.setMode('material');assert.equal(controller.state().mode,'material');assert.equal(loads,1);
});
test('already loaded formats offer neutral Solid and preserve their original full materials',async()=>{
 const gltf=await scene(),mesh=meshes(gltf)[0],original=mesh.material,controller=new DeferredMaterials({gltf,renderer,render(){}});assert.equal(controller.state().materialState,'ready');assert.notEqual(mesh.material,original);const solid=mesh.material;await controller.setMode('material');assert.equal(mesh.material,original);await controller.setMode('solid');assert.equal(mesh.material,solid);controller.dispose();
});
test('unused materials with missing images are not loaded by a scene material request',async()=>{
 const gltf=await scene(),json={asset:{version:'2.0'},scene:0,scenes:[{nodes:[]}],materials:[{pbrMetallicRoughness:{baseColorFactor:[.3,.4,.5,1]}},{pbrMetallicRoughness:{baseColorTexture:{index:0}}}],textures:[{source:0}],images:[{uri:'unused-missing-image.png'}]};
 class LibraryLoader extends GLTFLoader{loadAsync(){return this.parseAsync(JSON.stringify(json),'');}}
 const controller=new DeferredMaterials({gltf,renderer,url:'materials.gltf',Loader:LibraryLoader,render(){}});await controller.setMode('material');assert.equal(controller.state().mode,'material');assert.equal(controller.state().materialState,'ready');controller.dispose();
});
test('two scenes with reused single/multiple primitives retain exact material slots after associations are reduced',async()=>{
 const fixture=fullSkinFixture(),json=structuredClone(fixture.json),p=json.meshes[0].primitives[0],attributes=Object.fromEntries(Object.entries(p.attributes).filter(([key])=>!key.startsWith('JOINTS')&&!key.startsWith('WEIGHTS'))),primitive={...p,attributes};delete json.animations;delete json.skins;json.materials.push({pbrMetallicRoughness:{baseColorFactor:[.8,.2,.1,1]}});const defaultPrimitive={...primitive};delete defaultPrimitive.material;
 json.meshes=[{primitives:[primitive]},{primitives:[{...primitive,material:1},defaultPrimitive]}];json.nodes=[{name:'Same',mesh:0},{name:'Same',mesh:0},{name:'Same',mesh:1},{name:'Same',mesh:1}];json.scene=0;json.scenes=[{nodes:[0,1,2]},{nodes:[3]}];
 const loader=()=>new GLTFLoader(),legacy=await loader().parseAsync(repack(fixture,json),'');let lost=0;legacy.scene.traverse(o=>{if(o.geometry&&!legacy.parser.associations.has(o))lost++;});assert.equal(lost,4);
 const gltf=await loader().register(createPrimitiveIdentityPlugin()).parseAsync(repack(fixture,json),''),bindings=collectMaterialBindings(gltf);assert.deepEqual(bindings.map(b=>b.index),[0,0,1,undefined]);assert.deepEqual(bindings.map(b=>b.mesh.userData.billfishPrimitive),[{meshes:0,primitives:0},{meshes:0,primitives:0},{meshes:1,primitives:0},{meshes:1,primitives:1}]);
 const lib=await library();lib.materials.push(new THREE.MeshStandardMaterial({color:0xff0000}));attachMaterialLibrary(bindings,lib.parser,lib.materials,renderer);assert.equal(bindings[2].material.color.getHex(),0xff0000);assert.equal(bindings[3].material.metalness,1);assert.equal(bindings[3].material.roughness,1);assert.equal(bindings[3].material.side,THREE.FrontSide);
});
test('two scene skin clones preserve fifth weights and exact primitive identity during late material attach',async()=>{
 const fixture=fullSkinFixture({sharedFourPrimitive:true,weightScale:2}),json=structuredClone(fixture.json),offset=json.nodes.length;json.nodes.push(...structuredClone(json.nodes).map(node=>({...node,...(node.children?{children:node.children.map(i=>i+offset)}:{}),...(node.skin===undefined?{}:{skin:1})})));json.skins.push({...json.skins[0],joints:json.skins[0].joints.map(i=>i+offset)});json.scenes.push({nodes:[offset]});
 const gltf=await new GLTFLoader().register(createPrimitiveIdentityPlugin()).register(createFullSkinLoaderPlugin({renderer})).parseAsync(repack(fixture,json),''),bindings=collectMaterialBindings(gltf),lib=await library(),full=bindings.find(b=>b.mesh.userData.billfishFullSkin),weights=bytes(full.mesh.geometry.getAttribute('weights_1'));assert.equal(bindings.length,2);assert.equal(gltf.parser.associations.get(full.mesh),undefined);attachMaterialLibrary(bindings,lib.parser,lib.materials,renderer);assert.equal(bytes(full.mesh.geometry.getAttribute('weights_1')),weights);assert.ok(full.material.customProgramCacheKey().includes('billfish-full-skin'));assert.ok(!bindings.find(b=>!b.mesh.userData.billfishFullSkin).material.customProgramCacheKey().includes('billfish-full-skin'));
});
test('source node extras cannot override the runtime primitive identity with another material slot',async()=>{
 const fixture=fullSkinFixture(),json=structuredClone(fixture.json);json.nodes[1].extras.billfishPrimitive={meshes:1,primitives:0};json.materials.push({pbrMetallicRoughness:{baseColorFactor:[1,0,0,1]}});json.meshes.push({primitives:[{...json.meshes[0].primitives[0],material:1}]});
 await assert.rejects(new GLTFLoader().register(createPrimitiveIdentityPlugin()).register(createFullSkinLoaderPlugin({renderer})).parseAsync(repack(fixture,json),''),/源节点 1 extras.*billfishPrimitive.*冲突/);
});
