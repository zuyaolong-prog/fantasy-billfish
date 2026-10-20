import * as THREE from '../vendor/three/build/three.module.js';
import {decodeClipBinary} from './clip-binary.mjs';

// glTF 2.0 cubic Hermite interpolation, as used by the pinned GLTFLoader.
class CubicInterpolant extends THREE.Interpolant {
  copySampleValue_(index) {
    const offset = index * this.valueSize * 3 + this.valueSize;
    for (let i = 0; i < this.valueSize; i++) this.resultBuffer[i] = this.sampleValues[offset + i];
    return this.resultBuffer;
  }
  interpolate_(i1, t0, t, t1) {
    const n = this.valueSize, dt = t1 - t0, p = (t - t0) / dt, p2 = p * p, p3 = p2 * p;
    const a = (i1 - 1) * n * 3, b = i1 * n * 3;
    for (let i = 0; i < n; i++) this.resultBuffer[i] = (2 * p3 - 3 * p2 + 1) * this.sampleValues[a + n + i]
      + (p3 - 2 * p2 + p) * dt * this.sampleValues[a + 2 * n + i]
      + (-2 * p3 + 3 * p2) * this.sampleValues[b + n + i]
      + (p3 - p2) * dt * this.sampleValues[b + i];
    if (this.quaternion) new THREE.Quaternion().fromArray(this.resultBuffer).normalize().toArray(this.resultBuffer);
    return this.resultBuffer;
  }
}

export function sourceNodeMap(gltf, nodeMap = {}) {
  const result = new Map();
  const add = (id, object) => {
    if (id === null || id === undefined) return;
    id = String(id);
    if (result.has(id) && result.get(id) !== object) throw new Error(`重复的源节点 ID: ${id}`);
    result.set(id, object);
  };
  gltf.scene.traverse(object => add(object.userData?.billfishSourceId, object));
  // GLB indices are stable identity. This never falls back to display names.
  const inverse = new Map();
  for (const [id, entry] of Object.entries(nodeMap)) {
    const indexes = Array.isArray(entry) ? entry : [typeof entry === 'object' ? entry.nodeIndex ?? entry.index : entry];
    for (const index of indexes) inverse.set(Number(index), id);
  }
  for (const [object, association] of gltf.parser?.associations || []) {
    if (association?.nodes !== undefined && inverse.has(association.nodes)) add(inverse.get(association.nodes), object);
  }
  return result;
}

export function buildClip(data, mapping, nodes) {
  if (!data || data.version !== 1 || !Array.isArray(data.tracks) || !Number.isFinite(data.duration) || data.duration < 0) throw new Error('动画数据无效');
  const tracks = [], bound = new Set();
  for (const source of data.tracks) {
    const targetId = mapping?.[source.nodeId];
    if (targetId === undefined) continue; // Explicitly partial mappings leave target defaults intact.
    const target = nodes.get(String(typeof targetId === 'object' ? targetId.targetNodeId ?? targetId.id : targetId));
    if (!target) throw new Error(`动画目标节点不存在: ${targetId}`);
    const kind = source.property, size = kind === 'rotation' ? 4 : kind === 'weights' ? null : 3;
    if (!['translation', 'rotation', 'scale', 'weights'].includes(kind)) throw new Error(`不支持的动画属性: ${kind}`);
    const times = source.times, values = source.values, interpolation = source.interpolation || 'LINEAR';
    if (!['LINEAR', 'STEP', 'CUBICSPLINE'].includes(interpolation)) throw new Error(`不支持的插值: ${interpolation}`);
    if (!(Array.isArray(times)||times instanceof Float64Array) || !times.length || !(Array.isArray(values)||values instanceof Float64Array) || !times.every(Number.isFinite) || !values.every(Number.isFinite)
      || times.some((v, i) => v < 0 || v > data.duration + 0.001 || (i > 0 && v <= times[i - 1]))) throw new Error('动画关键帧时间或数值无效');
    const stride = values.length / times.length / (interpolation === 'CUBICSPLINE' ? 3 : 1);
    if (!Number.isInteger(stride) || stride < 1 || (size !== null && size !== stride)) throw new Error('动画关键帧长度无效');
    let targets = [target];
    if (kind === 'weights') {
      targets = [];
      target.traverse(object => { if (object.morphTargetInfluences) targets.push(object); });
      if (!targets.length) throw new Error('目标没有可播放的 Morph Target');
    }
    for (const object of targets) {
      if (kind === 'weights' && object.morphTargetInfluences.length !== stride) throw new Error('Morph Target 数量不匹配');
      const property = {translation:'position',rotation:'quaternion',scale:'scale',weights:'morphTargetInfluences'}[kind];
      const path = `${object.uuid}.${property}`;
      if (bound.has(path)) throw new Error(`多个轨道控制同一目标属性: ${kind}`);
      bound.add(path);
      const Track = kind === 'rotation' ? THREE.QuaternionKeyframeTrack : kind === 'weights' ? THREE.NumberKeyframeTrack : THREE.VectorKeyframeTrack;
      const track = new Track(path, times, values, interpolation === 'STEP' ? THREE.InterpolateDiscrete : THREE.InterpolateLinear);
      if (interpolation === 'CUBICSPLINE') {
        track.createInterpolant = function(result) {
          const interpolant = new CubicInterpolant(this.times, this.values, this.getValueSize() / 3, result);
          interpolant.quaternion = kind === 'rotation';
          return interpolant;
        };
        track.createInterpolant.isInterpolantFactoryMethodGLTFCubicSpline = true;
      }
      tracks.push(track);
    }
  }
  if (!tracks.length) throw new Error('没有可绑定的动画轨道');
  return new THREE.AnimationClip(data.name || '动画', data.duration, tracks);
}

export class ClipLRU {
  constructor(maxBytes = 128 * 1024 * 1024, maxEntries = 12) { this.maxBytes = maxBytes; this.maxEntries = maxEntries; this.entries = new Map(); this.bytes = 0; }
  get(key) { const entry = this.entries.get(key); if (!entry) return null; this.entries.delete(key); this.entries.set(key, entry); return entry.clip; }
  set(key, clip, rowId) {
    this.delete(key);
    const bytes = clip.tracks.reduce((n, track) => n + track.times.byteLength + track.values.byteLength, 0);
    if (bytes > this.maxBytes) return;
    this.entries.set(key, {clip, bytes, rowId}); this.bytes += bytes;
    while (this.bytes > this.maxBytes || this.entries.size > this.maxEntries) this.delete(this.entries.keys().next().value);
  }
  delete(key) { const entry = this.entries.get(key); if (entry) { this.bytes -= entry.bytes; this.entries.delete(key); } }
  clear() { this.entries.clear(); this.bytes = 0; }
}

export class ClipPlayer {
  constructor(root, nodes, load, onState = () => {}) {
    this.root = root; this.nodes = nodes; this.load = load; this.onState = onState;
    this.mixer = new THREE.AnimationMixer(root); this.cache = new ClipLRU(); this.sequence = 0;
    this.loop = true; this.speed = 1; this.current = null; this.playing = false; this.defaults = [];
    root.traverse(object => this.defaults.push({object,position:object.position.clone(),quaternion:object.quaternion.clone(),scale:object.scale.clone(),weights:object.morphTargetInfluences?.slice()}));
    this.mixer.addEventListener('finished', () => { if (!this.seeking) this.finished = true; });
  }
  restore() { for (const d of this.defaults) { d.object.position.copy(d.position); d.object.quaternion.copy(d.quaternion); d.object.scale.copy(d.scale); if (d.weights) d.object.morphTargetInfluences.splice(0, d.weights.length, ...d.weights); } this.root.updateMatrixWorld(true); }
  stop() { if (this.action) { this.action.stop(); this.mixer.uncacheAction(this.clip, this.root); this.mixer.uncacheClip(this.clip); } this.action = null; this.clip = null; this.playing = false; this.finished = false; this.restore(); }
  async select(rowId) {
    const sequence = ++this.sequence;
    // Retain the displayed pose while the next full clip is validated and built.
    // Stopping here restored the bind pose for the entire asynchronous load.
    this.playing = false; this.finished = false; if (this.action) this.action.paused = true;
    this.onState({...this.state(),loading:rowId});
    try {
      // The backend validates freshness on every load; only decoded Three tracks are cached here.
      const cachedKey=[...this.cache.entries].reverse().find(([,entry])=>entry.rowId===rowId)?.[0];
      let result = await this.load(rowId,cachedKey?{cachedKey}:{});
      if (sequence !== this.sequence) return false;
      let key = result.cacheKey;
      let clip = this.cache.get(key);
      if(result.reused&&(!clip||key!==cachedKey)){
        result=await this.load(rowId,{});if(sequence!==this.sequence)return false;key=result.cacheKey;clip=this.cache.get(key);
      }
      if(!/^[a-f0-9]{64}$/i.test(key||''))throw new Error('动画缓存标识无效');
      if (!clip) { clip = buildClip(decodeClipBinary(result.clipBinary), result.mapping, this.nodes); this.cache.set(key, clip,rowId); }
      this.stop();
      this.clip = clip; this.current = rowId; this.action = this.mixer.clipAction(clip); this.action.clampWhenFinished = true;
      this.action.setLoop(this.loop ? THREE.LoopRepeat : THREE.LoopOnce, this.loop ? Infinity : 1);
      this.action.reset().play(); this.playing = true; this.mixer.update(0); this.onState(this.state()); return true;
    } catch (error) { if (sequence !== this.sequence) return false; this.onState({...this.state(),error:error.message}); throw error; }
  }
  setPlaying(value) { if (!this.action) return; if (value && this.action.time >= this.clip.duration && !this.loop) this.seek(0); this.playing = value; this.action.paused = !value; this.onState(this.state()); }
  setLoop(value) { this.loop = value; if (this.action) this.action.setLoop(value ? THREE.LoopRepeat : THREE.LoopOnce, value ? Infinity : 1); this.onState(this.state()); }
  seek(time) { if (!this.action) return; const paused = this.action.paused; this.action.enabled = true; this.action.paused = false; this.action.time = Math.max(0, Math.min(this.clip.duration, time)); this.seeking = true; try { this.mixer.update(0); } finally { this.seeking = false; } this.action.paused = paused; this.root.updateMatrixWorld(true); this.onState(this.state()); }
  update(dt) { if (this.playing) this.mixer.update(dt * this.speed); if (this.finished && this.action) { this.finished = false; this.playing = false; this.action.reset().play(); this.action.paused = true; this.mixer.update(0); this.root.updateMatrixWorld(true); this.onState(this.state()); } }
  state() { return {rowId:this.current,playing:this.playing,time:this.action?.time || 0,duration:this.clip?.duration || 0,loop:this.loop,speed:this.speed,cacheBytes:this.cache.bytes,cacheEntries:this.cache.entries.size}; }
  dispose() { this.sequence++; this.stop(); this.mixer.uncacheRoot(this.root); this.cache.clear(); this.defaults = []; this.nodes.clear(); }
}
