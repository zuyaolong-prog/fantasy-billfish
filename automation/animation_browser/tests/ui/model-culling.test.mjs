import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../vendor/three/build/three.module.js';
import {setModelCulling} from '../../ui/model-culling.mjs';
function mesh(){return new THREE.Mesh(new THREE.BoxGeometry(),new THREE.MeshStandardMaterial());}
test('rigid geometry enables normal Three frustum tests without changing geometry or render semantics',()=>{
 const root=new THREE.Group(),visible=mesh(),outside=mesh();root.add(visible,outside);visible.position.z=-5;outside.position.set(100,0,-5);const geometry=visible.geometry,material=visible.material;
 setModelCulling(root);assert.equal(visible.frustumCulled,true);assert.equal(outside.frustumCulled,true);const camera=new THREE.PerspectiveCamera(38,1,.1,100);camera.updateMatrixWorld();const frustum=new THREE.Frustum().setFromProjectionMatrix(new THREE.Matrix4().multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse));assert.equal(frustum.intersectsObject(visible),true);assert.equal(frustum.intersectsObject(outside),false);assert.equal(visible.geometry,geometry);assert.equal(visible.material,material);assert.equal(material.side,THREE.FrontSide);assert.equal(material.transparent,false);
});
test('skin, morph and instanced geometry preserve their existing uncullled safety path',()=>{
 const root=new THREE.Group(),skin=new THREE.SkinnedMesh(new THREE.BoxGeometry(),new THREE.MeshStandardMaterial()),morph=mesh(),instances=new THREE.InstancedMesh(new THREE.BoxGeometry(),new THREE.MeshStandardMaterial(),2);morph.geometry.morphAttributes.position=[morph.geometry.attributes.position.clone()];morph.updateMorphTargets();root.add(skin,morph,instances);setModelCulling(root);assert.equal(skin.frustumCulled,false);assert.equal(morph.frustumCulled,false);assert.equal(instances.frustumCulled,false);
});
test('nonuniform parent scale plus rotated child retains shear safety while orthogonal transforms can cull',()=>{
 const root=new THREE.Group(),parent=new THREE.Group(),shear=mesh(),rigid=mesh();root.add(parent,rigid);parent.scale.set(10,.5,2);shear.rotation.z=.6;parent.add(shear);rigid.rotation.set(.2,.3,.4);rigid.scale.set(2,.5,3);setModelCulling(root);assert.equal(shear.frustumCulled,false);assert.equal(rigid.frustumCulled,true);
});
test('an external clip disables static culling and restoring its default pose permits culling again',()=>{
 const root=new THREE.Group(),object=mesh();root.add(object);const source=object.geometry.attributes.position.array.slice();setModelCulling(root);assert.equal(object.frustumCulled,true);setModelCulling(root,{animated:true});assert.equal(object.frustumCulled,false);object.position.set(9,2,3);setModelCulling(root,{animated:true});assert.equal(object.frustumCulled,false);object.position.set(0,0,0);setModelCulling(root);assert.equal(object.frustumCulled,true);assert.deepEqual(object.geometry.attributes.position.array,source);
});
test('a displacement material switch is excluded before drawing with the new material',()=>{
 const root=new THREE.Group(),object=mesh();root.add(object);const solid=object.material;setModelCulling(root);assert.equal(object.frustumCulled,true);object.material=new THREE.MeshStandardMaterial({displacementMap:new THREE.Texture(),displacementScale:100});setModelCulling(root);assert.equal(object.frustumCulled,false);object.material=solid;setModelCulling(root);assert.equal(object.frustumCulled,true);
});
