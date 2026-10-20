import * as THREE from '../vendor/three/build/three.module.js';

const properties=['x','y','z','w'];
function component(attribute,vertex,lane){return attribute.getComponent(vertex,lane);}
function weightsCopy(attribute){const values=new Float32Array(attribute.count*4);for(let v=0;v<attribute.count;v++)for(let lane=0;lane<4;lane++)values[v*4+lane]=component(attribute,v,lane);return values;}
function primitiveSets(attributes){
  const sets=new Set();for(const name of Object.keys(attributes||{})){const match=/^(?:JOINTS|WEIGHTS)_(\d+)$/.exec(name);if(match)sets.add(Number(match[1]));}
  if(!sets.size)return 0;const count=Math.max(...sets)+1;
  for(let i=0;i<count;i++)if(attributes[`JOINTS_${i}`]===undefined||attributes[`WEIGHTS_${i}`]===undefined)throw new Error('蒙皮 JOINTS/WEIGHTS 组必须连续且成对');
  return count;
}
function geometrySets(geometry){let count=1;while(geometry.getAttribute(`joints_${count}`)||geometry.getAttribute(`weights_${count}`)){if(!geometry.getAttribute(`joints_${count}`)||!geometry.getAttribute(`weights_${count}`))throw new Error('蒙皮属性组不完整');count++;}return count;}
function groupAttributes(geometry,sets){return Array.from({length:sets},(_,index)=>({joints:geometry.getAttribute(index?`joints_${index}`:'skinIndex'),weights:geometry.getAttribute(index?`weights_${index}`:'skinWeight')}));}

function restoreAndNormalize(geometry,snapshots,sets){
  const count=geometry.getAttribute('position').count;if(snapshots.length!==sets||snapshots.some(snapshot=>snapshot.length!==count*4))throw new Error('原始蒙皮权重数量不匹配');
  const groups=groupAttributes(geometry,sets);
  for(const group of groups)if(!group.joints||!group.weights||group.joints.count!==count||group.weights.count!==count||group.joints.itemSize!==4||group.weights.itemSize!==4)throw new Error('蒙皮属性长度不匹配');
  const copies=snapshots.map(snapshot=>snapshot.slice());
  for(let v=0;v<count;v++){
    let total=0;for(const values of copies)for(let lane=0;lane<4;lane++){const weight=values[v*4+lane];if(!Number.isFinite(weight)||weight<0)throw new Error('蒙皮权重必须为有限非负数');total+=weight;}
    if(!(total>0))throw new Error('骨架网格体存在无权重顶点');
    for(const values of copies)for(let lane=0;lane<4;lane++)values[v*4+lane]/=total;
  }
  copies.forEach((values,index)=>geometry.setAttribute(index?`weights_${index}`:'skinWeight',new THREE.Float32BufferAttribute(values,4)));
}

export function fullSkinAttributeBudget(mesh,material,sets){
  const a=mesh.geometry.attributes;let count=1+sets*2;
  if(a.normal)count++;if(a.tangent)count++;
  for(const name of ['uv','uv1','uv2','uv3'])if(a[name])count++;
  if(a.color&&material.vertexColors)count++;
  if(mesh.isInstancedMesh)count+=4+(mesh.instanceColor?1:0);
  return count;
}

export function applyFullSkinMaterial(mesh,material,renderer){
  const sets=geometrySets(mesh.geometry);
  if(!mesh.isSkinnedMesh||sets<=1)return material;
  const budget=fullSkinAttributeBudget(mesh,material,sets),maximum=renderer?.capabilities?.maxAttributes;
  if(Number.isFinite(maximum)&&budget>maximum)throw new Error(`完整蒙皮的保守顶点属性预算为 ${budget}, 超过当前 GPU 上限 ${maximum}; 未丢弃源权重`);
  const clone=material.clone(),oldHook=material.onBeforeCompile,oldKey=material.customProgramCacheKey.bind(material);
  clone.onBeforeCompile=function(shader,activeRenderer){
    oldHook.call(this,shader,activeRenderer);
    const chunks=['skinning_pars_vertex','skinbase_vertex','skinnormal_vertex','skinning_vertex'];
    for(const chunk of chunks)if(!shader.vertexShader.includes(`#include <${chunk}>`))throw new Error(`材质缺少完整蒙皮所需的 shader chunk: ${chunk}`);
    const declarations=Array.from({length:sets-1},(_,i)=>`attribute vec4 joints_${i+1};\nattribute vec4 weights_${i+1};`).join('\n');
    const terms=[];for(let set=0;set<sets;set++)for(const lane of properties){const joints=set?`joints_${set}`:'skinIndex',weights=set?`weights_${set}`:'skinWeight';terms.push(`if (${weights}.${lane} > 0.0) billfishSkinMatrix += getBoneMatrix(${joints}.${lane}) * ${weights}.${lane};`);}
    shader.vertexShader=shader.vertexShader
      .replace('#include <skinning_pars_vertex>',`#include <skinning_pars_vertex>\n#ifdef USE_SKINNING\n${declarations}\n#endif`)
      .replace('#include <skinbase_vertex>',`#ifdef USE_SKINNING\nmat4 billfishSkinMatrix = mat4(0.0);\n${terms.join('\n')}\n#endif`)
      .replace('#include <skinnormal_vertex>','#ifdef USE_SKINNING\nmat4 skinMatrix = bindMatrixInverse * billfishSkinMatrix * bindMatrix;\nobjectNormal = (skinMatrix * vec4(objectNormal, 0.0)).xyz;\n#ifdef USE_TANGENT\nobjectTangent = (skinMatrix * vec4(objectTangent, 0.0)).xyz;\n#endif\n#endif')
      .replace('#include <skinning_vertex>','#ifdef USE_SKINNING\ntransformed = (bindMatrixInverse * billfishSkinMatrix * bindMatrix * vec4(transformed, 1.0)).xyz;\n#endif');
  };
  clone.customProgramCacheKey=()=>`${oldKey()}|billfish-full-skin-1|${sets}`;
  return clone;
}

function installCPU(mesh,sets){
  const groups=groupAttributes(mesh.geometry,sets),base=new THREE.Vector3(),point=new THREE.Vector3(),boneMatrix=new THREE.Matrix4();
  for(const group of groups)for(let v=0;v<group.joints.count;v++)for(let lane=0;lane<4;lane++){const joint=component(group.joints,v,lane);if(!Number.isInteger(joint)||joint<0||joint>=mesh.skeleton.bones.length)throw new Error('蒙皮 joint index 超出实际骨架范围');}
  // getVertexPosition retains Mesh's morph evaluation before calling this method.
  mesh.applyBoneTransform=function(index,target){
    base.copy(target).applyMatrix4(this.bindMatrix);target.set(0,0,0);
    for(const group of groups)for(let lane=0;lane<4;lane++){const weight=component(group.weights,index,lane);if(weight===0)continue;const joint=component(group.joints,index,lane);boneMatrix.multiplyMatrices(this.skeleton.bones[joint].matrixWorld,this.skeleton.boneInverses[joint]);target.addScaledVector(point.copy(base).applyMatrix4(boneMatrix),weight);}
    return target.applyMatrix4(this.bindMatrixInverse);
  };
}

export function createFullSkinLoaderPlugin({renderer}={}){
  return parser=>{
    const originals=new Map(),byAttribute=new WeakMap();
    return {name:'BILLFISH_full_skin',async beforeRoot(){
      const accessors=new Set();for(const mesh of parser.json.meshes||[])for(const primitive of mesh.primitives||[]){const sets=primitiveSets(primitive.attributes);if(sets>1)for(let i=0;i<sets;i++)accessors.add(primitive.attributes[`WEIGHTS_${i}`]);}
      await Promise.all([...accessors].map(async index=>{const attribute=await parser.getDependency('accessor',index),snapshot=weightsCopy(attribute);originals.set(index,snapshot);byAttribute.set(attribute,snapshot);}));
    },afterRoot(result){
      const geometries=new WeakSet(),meshes=new Set(),beforeMaterials=new Set(),afterMaterials=new Set();
      for(const scene of result.scenes||[result.scene])scene.traverse(mesh=>{if(mesh.material)for(const material of Array.isArray(mesh.material)?mesh.material:[mesh.material])beforeMaterials.add(material);if(mesh.isSkinnedMesh&&mesh.skeleton&&geometrySets(mesh.geometry)>1)meshes.add(mesh);});
      for(const mesh of meshes){
        const sets=geometrySets(mesh.geometry);
        if(!geometries.has(mesh.geometry)){
          const association=mesh.userData.billfishPrimitive||parser.associations.get(mesh),primitive=association?.meshes!==undefined?parser.json.meshes[association.meshes]?.primitives?.[association.primitives]:null;
          const snapshots=groupAttributes(mesh.geometry,sets).map((group,index)=>primitive?originals.get(primitive.attributes[`WEIGHTS_${index}`]):byAttribute.get(group.weights));
          if(snapshots.some(snapshot=>!snapshot))throw new Error('无法恢复完整蒙皮的原始权重');
          restoreAndNormalize(mesh.geometry,snapshots,sets);geometries.add(mesh.geometry);
        }
        installCPU(mesh,sets);mesh.material=Array.isArray(mesh.material)?mesh.material.map(material=>applyFullSkinMaterial(mesh,material,renderer)):applyFullSkinMaterial(mesh,mesh.material,renderer);
        mesh.userData.billfishFullSkin={sets,influenceSlots:sets*4};
      }
      for(const scene of result.scenes||[result.scene])scene.traverse(mesh=>{if(mesh.material)for(const material of Array.isArray(mesh.material)?mesh.material:[mesh.material])afterMaterials.add(material);});
      for(const material of beforeMaterials)if(!afterMaterials.has(material))material.dispose();
    }};
  };
}
