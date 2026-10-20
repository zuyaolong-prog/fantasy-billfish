import * as THREE from '../vendor/three/build/three.module.js';
import { GLTFLoader } from '../vendor/three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from '../vendor/three/addons/controls/OrbitControls.js';
import { ClipPlayer, sourceNodeMap } from './playback.mjs';
import { SkeletonOverlay } from './skeleton-overlay.mjs';
import { createFullSkinLoaderPlugin } from './full-skin.mjs';
import { WalkControls } from './walk-controls.mjs';
import { DeferredMaterials, createPrimitiveIdentityPlugin } from './deferred-materials.mjs';
import { setModelCulling } from './model-culling.mjs';

function resourceURL(path) {
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(path) || /^(blob:|data:)/i.test(path)) return path;
  const normalized = path.replace(/\\/g, '/');
  return normalized.startsWith('//') ? new URL(`file:${normalized}`).href : `file:///${normalized.split('/').map(encodeURIComponent).join('/')}`.replace(/^file:\/\/\/([A-Za-z])%3A/, 'file:///$1:');
}

export class CharacterViewport {
  constructor(element, {loadClip,onState,onWarning,onNavigationState,onNavigationError,onRenderModeState} = {}) {
    this.element = element; this.loadClip = loadClip; this.onState = onState; this.onWarning = onWarning || (() => {}); this.disposed = false;
    this.onRenderModeState=onRenderModeState || (()=>{});
    this.scene = new THREE.Scene(); this.scene.background = new THREE.Color('#202226');
    this.camera = new THREE.PerspectiveCamera(38, 1, 0.001, 10000);
    this.renderer = new THREE.WebGLRenderer({antialias:true, alpha:false, preserveDrawingBuffer:true});
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace; this.renderer.toneMapping = THREE.ACESFilmicToneMapping; this.renderer.toneMappingExposure = 1.2;
    this.renderer.debug.onShaderError=(gl,program,vertexShader,fragmentShader)=>{throw new Error(`模型着色器编译失败: ${(gl.getProgramInfoLog(program)||gl.getShaderInfoLog(vertexShader)||gl.getShaderInfoLog(fragmentShader)||'Unknown shader error').slice(0,4096)}`);};
    this.renderer.domElement.setAttribute('aria-label', '3D 角色视口'); this.renderer.domElement.tabIndex = 0; element.append(this.renderer.domElement);
    this.controls = new OrbitControls(this.camera, this.renderer.domElement); this.controls.enableDamping = true;
    this.walk = new WalkControls({camera:this.camera,controls:this.controls,element:this.renderer.domElement,onChange:onNavigationState,onError:onNavigationError || (error=>this.onWarning({code:'WALK_CONTROL_FAILED',message:error.message || String(error)}))});
    this.scene.add(new THREE.HemisphereLight(0xe4edff, 0x68605a, 2.2));
    const key = new THREE.DirectionalLight(0xfff5e8, 3.0); key.position.set(3,5,4); this.scene.add(key);
    const fill = new THREE.DirectionalLight(0xc8d8ff, 1.5); fill.position.set(-4,2,-2); this.scene.add(fill);
    this.displayRoot = new THREE.Group(); this.scene.add(this.displayRoot);
    this.grid = new THREE.GridHelper(10,20,0x555a64,0x35383e); this.scene.add(this.grid);
    this.resizeObserver = new ResizeObserver(() => this.resize()); this.resizeObserver.observe(element); this.resize();
    let last = performance.now();
    this.renderer.setAnimationLoop(now => { const dt = Math.min((now - last) / 1000,0.1); last = now; this.player?.update(Math.max(0,dt)); if(this.walk.active)this.walk.update(Math.max(0,dt));else this.controls.update(); this.renderer.render(this.scene,this.camera); });
    this.contextLost = event => { event.preventDefault(); this.onWarning({code:'WEBGL_CONTEXT_LOST',message:'显卡渲染上下文已丢失, 请重新打开预览.'}); };
    this.renderer.domElement.addEventListener('webglcontextlost',this.contextLost);
  }
  async load(model,{renderMode=model.initialDisplayMode || 'solid',facing=0}={}) {
    this.walk?.exit();
    if (this.model) throw new Error('当前视口已有常驻模型');
    const manager = new THREE.LoadingManager(),url=resourceURL(model.url || model.path),failures=new Set(),borrowedBlobs=new Set([url]);
    manager.onError = resource => {failures.add(resource);if(!this.disposed)this.onWarning({code:'RESOURCE_MISSING',message:`未能加载贴图或资源: ${resource}`});};
    // Embedded images are loader-owned blobs. Explicit blob URIs belong to the caller.
    const resourceLifetime=parser=>({name:'BILLFISH_resource_lifetime',beforeRoot(){for(const resource of [...(parser.json.images||[]),...(parser.json.buffers||[])])if(resource.uri?.startsWith('blob:')){borrowedBlobs.add(resource.uri);}}});
    let gltf;
    try{
      gltf=await new GLTFLoader(manager).register(resourceLifetime).register(createPrimitiveIdentityPlugin()).register(createFullSkinLoaderPlugin({renderer:this.renderer})).loadAsync(url);
      if(this.disposed||failures.size){
        const rejected=new THREE.Group();for(const scene of new Set([gltf.scene,...(gltf.scenes||[])]))if(scene)rejected.add(scene);this.disposeScene(rejected);
        if(this.disposed)return;
        throw new Error(`模型有 ${failures.size} 个贴图或资源加载失败, 未完成加载: ${String([...failures][0]).slice(0,1024)}`);
      }
    }finally{for(const resource of failures)if(resource.startsWith('blob:')&&!borrowedBlobs.has(resource))URL.revokeObjectURL(resource);}
    this.model = gltf.scene; this.nodes = sourceNodeMap(gltf,model.nodeMap);
    this.displayRoot.add(this.model); this.model.updateMatrixWorld(true);
    this.meshCount = 0; this.model.traverse(object => { if (object.isMesh) this.meshCount++; });
    if (!this.meshCount) throw new Error('该文件中没有可显示的 Mesh');
    this.cullingAnimated=false;
    this.player = new ClipPlayer(this.model,this.nodes,this.loadClip,state=>{const animated=!!this.player?.action;if(this.model&&animated!==this.cullingAnimated){this.cullingAnimated=animated;setModelCulling(this.model,{animated});}this.onState?.(state);});
    this.materials=new DeferredMaterials({gltf,renderer:this.renderer,Loader:GLTFLoader,url:model.materialsUrl,render:()=>{setModelCulling(this.model,{animated:!!this.player?.action});this.render();},onState:this.onRenderModeState,onWarning:this.onWarning});
    setModelCulling(this.model);
    this.skeletonOverlay = new SkeletonOverlay(this.scene,this.model);this.hasSkeleton=this.skeletonOverlay.available;this.skeletonVisible=false;
    this.modelLoads = (this.modelLoads || 0) + 1;
    this.displayRoot.rotation.y=THREE.MathUtils.degToRad(Number(facing)||0);this.frame();
    await this.materials.setMode(renderMode);
    return {meshCount:this.meshCount, nodeCount:this.nodes.size,hasSkeleton:this.hasSkeleton};
  }
  enterWalk() { return this.walk.enter(); }
  get navigationState() { return this.walk.state(); }
  get renderModeState() { return this.materials?.state() || {mode:'solid',requestedMode:'solid',materialState:'idle',error:null}; }
  setRenderMode(mode) { if(!this.materials)throw new Error('模型尚未加载');return this.materials.setMode(mode); }
  resize() { this.walk?.exit();const w = Math.max(1,this.element.clientWidth), h = Math.max(1,this.element.clientHeight); this.renderer.setSize(w,h); this.camera.aspect = w/h; this.camera.updateProjectionMatrix(); }
  frame() {
    this.walk?.exit();
    if (!this.model) return;
    this.model.updateMatrixWorld(true); this.displayRoot.updateMatrixWorld(true);
    const bounds = new THREE.Box3().setFromObject(this.model,true);
    if (bounds.isEmpty()) throw new Error('模型包围盒为空');
    const center = bounds.getCenter(new THREE.Vector3()), size = bounds.getSize(new THREE.Vector3());
    if (![...center,...size].every(Number.isFinite)) throw new Error('模型包围盒无效');
    const radius = Math.max(size.length()/2,0.01), vfov = THREE.MathUtils.degToRad(this.camera.fov), hfov = 2 * Math.atan(Math.tan(vfov/2) * this.camera.aspect);
    this.walk?.setSpeed(radius*.2);
    const direction = new THREE.Vector3(0.8,0.35,1.2).normalize(),right = new THREE.Vector3(0,1,0).cross(direction).normalize(),up = direction.clone().cross(right);
    let distance=radius*0.1;
    for(const x of [bounds.min.x,bounds.max.x])for(const y of [bounds.min.y,bounds.max.y])for(const z of [bounds.min.z,bounds.max.z]){const corner=new THREE.Vector3(x,y,z).sub(center);distance=Math.max(distance,Math.abs(corner.dot(right))/Math.tan(hfov/2)+corner.dot(direction),Math.abs(corner.dot(up))/Math.tan(vfov/2)+corner.dot(direction));}
    distance*=1.15;
    this.camera.near = Math.max(radius/10000,0.00001); this.camera.far = Math.max(radius*1000,100); this.camera.updateProjectionMatrix();
    this.camera.position.copy(center).add(direction.multiplyScalar(distance));
    this.controls.target.copy(center); this.controls.minDistance = radius * 0.03; this.controls.maxDistance = radius*100; this.controls.update(); this.controls.saveState();
    const span = Math.pow(10,Math.floor(Math.log10(Math.max(size.x,size.z,0.01))));
    this.grid.scale.setScalar(span); this.grid.position.set(center.x,bounds.min.y,center.z);
    this.render();
  }
  setFacing(degrees) { this.displayRoot.rotation.y = THREE.MathUtils.degToRad(Number(degrees)||0); this.displayRoot.updateMatrixWorld(true); this.render(); }
  setSkeletonVisible(visible){this.skeletonVisible=this.skeletonOverlay?.setVisible(visible)||false;this.render();return this.skeletonVisible;}
  render() { this.renderer.render(this.scene,this.camera); }
  capture() { if (!this.model) throw new Error('模型尚未加载'); this.render(); return this.renderer.domElement.toDataURL('image/png'); }
  disposeScene(root) {
    const geometries = new Set(),materials = new Set(),textures = new Set(),skeletons = new Set();
    root.traverse(object => { if (object.geometry) geometries.add(object.geometry); if (object.skeleton) skeletons.add(object.skeleton); for (const m of Array.isArray(object.material) ? object.material : object.material ? [object.material] : []) { materials.add(m); for (const value of Object.values(m)) if (value?.isTexture) textures.add(value); } });
    for (const t of textures) { t.dispose(); t.source?.data?.close?.(); }
    for (const g of geometries) g.dispose(); for (const m of materials) m.dispose(); for (const s of skeletons) s.dispose();
  }
  dispose() { if (this.disposed) return; this.disposed = true; this.renderer.setAnimationLoop(null); this.walk?.dispose();this.player?.dispose();this.materials?.dispose(); this.skeletonOverlay?.dispose();this.hasSkeleton=false;this.skeletonVisible=false;this.resizeObserver.disconnect(); this.controls.dispose(); this.renderer.domElement.removeEventListener('webglcontextlost',this.contextLost); this.disposeScene(this.scene); this.renderer.dispose(); this.renderer.forceContextLoss(); this.renderer.domElement.remove(); this.model = null; this.nodes = null; }
}
