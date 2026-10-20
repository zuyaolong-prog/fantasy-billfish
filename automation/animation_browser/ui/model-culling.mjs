function sheared(matrix){
  const m=matrix.elements,a=m[0]*m[0]+m[1]*m[1]+m[2]*m[2],b=m[4]*m[4]+m[5]*m[5]+m[6]*m[6],c=m[8]*m[8]+m[9]*m[9]+m[10]*m[10];
  if(![a,b,c].every(value=>Number.isFinite(value)&&value>0))return true;
  const ab=m[0]*m[4]+m[1]*m[5]+m[2]*m[6],ac=m[0]*m[8]+m[1]*m[9]+m[2]*m[10],bc=m[4]*m[8]+m[5]*m[9]+m[6]*m[10];
  return Math.abs(ab)>1e-10*Math.sqrt(a*b)||Math.abs(ac)>1e-10*Math.sqrt(a*c)||Math.abs(bc)>1e-10*Math.sqrt(b*c);
}

// Three.js updates each object's world transform during rendering. Its normal
// geometry sphere is valid for rigid meshes with orthogonal world axes. Preserve
// the prior unculled path for deformation, animation and shear.
export function setModelCulling(root,{animated=false}={}){
  root.updateWorldMatrix(true,true);
  root.traverse(mesh=>{if(!mesh.isMesh)return;const materials=Array.isArray(mesh.material)?mesh.material:[mesh.material];
    mesh.frustumCulled=!animated&&!mesh.isSkinnedMesh&&!mesh.isInstancedMesh&&!mesh.isBatchedMesh&&!mesh.morphTargetInfluences?.length&&!Object.keys(mesh.geometry?.morphAttributes||{}).length&&!materials.some(material=>material?.isShaderMaterial||material?.displacementMap)&&!sheared(mesh.matrixWorld);
  });
}
