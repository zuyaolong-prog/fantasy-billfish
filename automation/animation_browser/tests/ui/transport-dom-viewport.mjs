// Test-owned viewport avoids a GPU context while keeping the real ClipPlayer.
import * as THREE from '../vendor/three/build/three.module.js';
import {ClipPlayer} from './playback.mjs';
import {encodeClipFixture} from '/test/clip-binary-fixture.mjs';
export class CharacterViewport{
 constructor(element,{loadClip,onState}={}){this.element=element;this.root=new THREE.Group();this.bone=new THREE.Bone();this.root.add(this.bone);this.player=new ClipPlayer(this.root,new Map([['bone',this.bone]]),async(...args)=>{const result=await loadClip(...args);return{...result,cacheKey:'1'.repeat(64),clipBinary:encodeClipFixture(result.clip)};},onState);this.controls={reset(){}};this.timer=setInterval(()=>this.player.update(.025),25);}
 async load(){this.model=this.root;return {meshCount:1};}frame(){}setFacing(){}capture(){return 'data:image/png;base64,AA==';}dispose(){clearInterval(this.timer);this.player.dispose();}
}
