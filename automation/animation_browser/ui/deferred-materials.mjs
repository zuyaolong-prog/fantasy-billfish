import * as THREE from '../vendor/three/build/three.module.js';
import {applyFullSkinMaterial} from './full-skin.mjs';

// GLTFParser reduces its global association map after each scene. Preserve the
// exact primitive identity before that reduction; Object3D.clone copies userData
// when a source mesh is instantiated under multiple nodes or scenes.
export function createPrimitiveIdentityPlugin(){
  return parser=>({name:'BILLFISH_primitive_identity',beforeRoot(){
    // loadNode assigns node extras after cloning a mesh. Reject an authored
    // collision rather than allowing those extras to replace runtime identity.
    for(const [index,node]of (parser.json.nodes||[]).entries())if(Object.prototype.hasOwnProperty.call(node.extras||{},'billfishPrimitive'))throw new Error(`源节点 ${index} extras 与内部保留字段 billfishPrimitive 冲突`);
  },async loadMesh(index){
    const result=await parser.loadMesh(index);result.traverse(mesh=>{if(!mesh.geometry||!mesh.material)return;const association=parser.associations.get(mesh),primitive=association?.meshes===index?parser.json.meshes[index]?.primitives?.[association.primitives]:null;
      if(!primitive)throw new Error('无法保存原始 Mesh primitive 的身份');
      mesh.userData.billfishPrimitive={meshes:index,primitives:association.primitives};});return result;
  }});
}

function disposeMaterials(materials){
  const textures=new Set();for(const material of new Set(materials)){if(!material)continue;for(const value of Object.values(material))if(value?.isTexture)textures.add(value);material.dispose();}
  for(const texture of textures){texture.dispose();texture.source?.data?.close?.();}
}

export function collectMaterialBindings(gltf){
  const bindings=[];gltf.scene.traverse(mesh=>{if(!mesh.material||!mesh.geometry)return;const association=mesh.userData.billfishPrimitive,primitive=association?.meshes===undefined?null:gltf.parser.json.meshes?.[association.meshes]?.primitives?.[association.primitives];
    if(!primitive)throw new Error('无法定位原始 Mesh primitive 的材质槽');
    bindings.push({mesh,index:primitive.material,solid:mesh.material,material:null});});return bindings;
}

export function attachMaterialLibrary(bindings,parser,materials,renderer){
  const owned=new Set(materials.filter(Boolean)),fallback=new THREE.MeshStandardMaterial({color:0xffffff,metalness:1,roughness:1});let usedFallback=false;
  try{for(const binding of bindings){const {mesh,index}=binding;let material=index===undefined?fallback:materials[index];if(!material)throw new Error(`原始材质槽 ${index} 不存在`);if(material===fallback)usedFallback=true;
      const previous=mesh.material;try{mesh.material=material;parser.assignFinalMaterial(mesh);owned.add(mesh.material);material=applyFullSkinMaterial(mesh,mesh.material,renderer);owned.add(material);binding.material=material;}finally{mesh.material=previous;}}
    if(!usedFallback)fallback.dispose();else owned.add(fallback);return owned;
  }catch(error){owned.add(fallback);disposeMaterials(owned);throw error;}
}

async function loadMaterialLibrary(url,{onWarning,Loader,materialIndices}={}){
  const manager=new THREE.LoadingManager(),failures=new Set();let parser,materials=[];
  manager.onError=resource=>failures.add(resource);
  const plugin=p=>{parser=p;return{name:'BILLFISH_material_library',async afterRoot(){await Promise.all(materialIndices.map(async index=>{materials[index]=await p.getDependency('material',index);}));}};};
  try{await new Loader(manager).register(plugin).loadAsync(url);if(failures.size)throw new Error(`原始材质有 ${failures.size} 个资源加载失败: ${String([...failures][0]).slice(0,1024)}`);return{parser,materials};}
  catch(error){disposeMaterials(materials);onWarning?.({code:'MATERIAL_RESOURCE_FAILED',message:error.message});throw error;}
}

export class DeferredMaterials {
  constructor({gltf,renderer,url,render,onState=()=>{},onWarning,Loader,loadLibrary=loadMaterialLibrary}){
    Object.assign(this,{renderer,url,render,onState,onWarning,Loader,loadLibrary});this.bindings=collectMaterialBindings(gltf);this.mode='solid';this.requestedMode='solid';this.materialState='idle';this.error=null;this.disposed=false;this.owned=new Set();this.sequence=0;
    // Formats already loaded with materials still offer the same display modes.
    // Their materialState is honestly ready; this path does not save load work.
    if(!url){this.materialState='ready';this.libraryReady=true;const neutral=new THREE.MeshStandardMaterial({color:new THREE.Color(.55,.56,.58),metalness:0,roughness:.8,side:THREE.DoubleSide});let used=false;
      for(const binding of this.bindings){binding.material=binding.solid;this.owned.add(binding.material);binding.mesh.material=neutral;gltf.parser.assignFinalMaterial(binding.mesh);binding.solid=applyFullSkinMaterial(binding.mesh,binding.mesh.material,renderer);binding.mesh.material=binding.solid;if(binding.solid===neutral)used=true;}
      if(!used)neutral.dispose();}
  }
  state(){return{mode:this.mode,requestedMode:this.requestedMode,materialState:this.materialState,error:this.error};}
  changed(){if(!this.disposed)this.onState(this.state());}
  load(){
    if(this.libraryReady)return Promise.resolve();if(this.loading)return this.loading;
    this.materialState='loading';this.error=null;this.changed();
    this.loading=(async()=>{let library;try{library=await this.loadLibrary(this.url,{onWarning:warning=>{if(!this.disposed)this.onWarning?.(warning);},Loader:this.Loader,materialIndices:[...new Set(this.bindings.map(binding=>binding.index).filter(index=>index!==undefined))]});if(this.disposed){disposeMaterials(library.materials);return;}this.owned=attachMaterialLibrary(this.bindings,library.parser,library.materials,this.renderer);this.libraryReady=true;this.materialState='ready';this.changed();}
      catch(error){if(this.disposed)return;this.materialState='error';this.error=error.message;this.changed();throw error;}
      finally{this.loading=null;}})();return this.loading;
  }
  async setMode(mode){
    if(!['solid','material'].includes(mode))throw new Error('未知渲染模式');if(this.disposed)return this.state();if(this.mode===mode&&this.requestedMode===mode&&!this.error)return this.state();const sequence=++this.sequence;this.requestedMode=mode;this.changed();
    if(mode==='material')await this.load();if(this.disposed||sequence!==this.sequence)return this.state();
    const previous=this.mode;
    try{for(const binding of this.bindings)binding.mesh.material=mode==='material'?binding.material:binding.solid;this.render();this.mode=mode;if(this.libraryReady)this.materialState='ready';this.error=null;this.changed();return this.state();}
    catch(error){for(const binding of this.bindings)binding.mesh.material=previous==='material'?binding.material:binding.solid;this.requestedMode=this.mode=previous;this.materialState='error';this.error=error.message;this.render();this.changed();throw error;}
  }
  dispose(){if(this.disposed)return;this.disposed=true;++this.sequence;for(const binding of this.bindings)binding.mesh.material=binding.solid;disposeMaterials(this.owned);this.owned.clear();}
}
