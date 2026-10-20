import * as THREE from '../vendor/three/build/three.module.js';

class JointPoints extends THREE.Points {
  constructor(root,bones){
    const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(new Float32Array(bones.length*3),3));
    super(geometry,new THREE.PointsMaterial({color:0xffce69,size:4,sizeAttenuation:false,depthTest:false,depthWrite:false,toneMapped:false,transparent:true}));
    this.root=root;this.bones=bones;this.matrix=root.matrixWorld;this.matrixAutoUpdate=false;this.frustumCulled=false;this.renderOrder=1001;
    this.inverse=new THREE.Matrix4();this.positionValue=new THREE.Vector3();
  }
  updateMatrixWorld(force){
    this.inverse.copy(this.root.matrixWorld).invert();const positions=this.geometry.getAttribute('position');
    for(let i=0;i<this.bones.length;i++){this.positionValue.setFromMatrixPosition(this.bones[i].matrixWorld).applyMatrix4(this.inverse);positions.setXYZ(i,this.positionValue.x,this.positionValue.y,this.positionValue.z);}
    positions.needsUpdate=true;super.updateMatrixWorld(force);
  }
  dispose(){this.geometry.dispose();this.material.dispose();}
}

// Display helpers are scene siblings. They never alter model hierarchy, skin,
// animation tracks or the model-only bounds used for framing.
export class SkeletonOverlay {
  constructor(scene,model){
    this.scene=scene;this.model=model;this.visible=false;this.disposed=false;const bones=new Set();
    model.traverse(object=>{if(object.isSkinnedMesh)for(const bone of object.skeleton?.bones||[])if(bone?.isBone)bones.add(bone);});
    this.bones=[...bones];this.available=this.bones.length>0;
  }
  setVisible(visible){
    if(this.disposed||!this.available)return false;
    if(visible&&!this.lines){
      this.lines=new THREE.SkeletonHelper(this.model);this.lines.setColors(new THREE.Color(0x46d9ff),new THREE.Color(0xffce69));this.lines.frustumCulled=false;this.lines.renderOrder=1000;
      this.joints=new JointPoints(this.model,this.bones);
    }
    this.visible=!!visible;
    if(this.lines){if(this.visible){this.scene.add(this.lines,this.joints);}else{this.lines.removeFromParent();this.joints.removeFromParent();}}
    return this.visible;
  }
  dispose(){if(this.disposed)return;this.disposed=true;this.visible=false;for(const helper of [this.lines,this.joints])if(helper){helper.removeFromParent();helper.dispose();}this.lines=null;this.joints=null;this.bones=[];this.model=null;}
}
