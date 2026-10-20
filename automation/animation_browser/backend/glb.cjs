'use strict';
const fs = require('node:fs/promises');
const COMPONENT = {5120:[1,'getInt8'],5121:[1,'getUint8'],5122:[2,'getInt16'],5123:[2,'getUint16'],5125:[4,'getUint32'],5126:[4,'getFloat32']};
const WIDTH = {SCALAR:1,VEC2:2,VEC3:3,VEC4:4,MAT2:4,MAT3:9,MAT4:16};
function fail(message) { const e = new Error(message); e.code = 'INVALID_GLB'; throw e; }
async function readGlb(source,{signal}={}) {
  signal?.throwIfAborted();const bytes = await fs.readFile(source,{signal});signal?.throwIfAborted();
  if (bytes.length < 20 || bytes.readUInt32LE(0)!==0x46546c67 || bytes.readUInt32LE(4)!==2 || bytes.readUInt32LE(8)!==bytes.length) fail('Invalid GLB header');
  let json, bin; let offset=12;
  while(offset<bytes.length) {
    if(offset+8>bytes.length) fail('Truncated GLB chunk');
    const size=bytes.readUInt32LE(offset), type=bytes.readUInt32LE(offset+4); offset+=8;
    if(size%4 || offset+size>bytes.length) fail('Invalid GLB chunk extent');
    if(type===0x4e4f534a) {if(json) fail('Duplicate JSON chunk'); json=JSON.parse(bytes.toString('utf8',offset,offset+size).trim());}
    if(type===0x004e4942) {if(bin) fail('Duplicate binary chunk'); bin=bytes.subarray(offset,offset+size);}
    offset+=size;
  }
  if(!json || json.asset?.version!=='2.0') fail('Missing glTF 2.0 document');
  if((json.buffers||[]).some((b,i)=>i!==0 || b.uri)) fail('External GLB buffers are unsupported');
  if(json.buffers?.length && (!bin || json.buffers[0].byteLength>bin.length)) fail('Missing GLB buffer');
  return {json,bin:bin||Buffer.alloc(0)};
}
// A GLB's image payload is independent of its geometry. Keep every non-image
// buffer view, including sparse/morph/skin data, while leaving original images
// in the source file for the owned asset protocol to read on demand.
async function readGeometryGlb(source,{signal}={}) {
 signal?.throwIfAborted();const file=await fs.open(source,'r');
 try {
  const stat=await file.stat(),read=async(offset,length)=>{signal?.throwIfAborted();const bytes=Buffer.alloc(length);let done=0;while(done<length){const result=await file.read(bytes,done,length-done,offset+done);if(!result.bytesRead)fail('Truncated GLB chunk');done+=result.bytesRead;signal?.throwIfAborted();}return bytes;};
  const header=await read(0,12);if(stat.size<20||header.readUInt32LE(0)!==0x46546c67||header.readUInt32LE(4)!==2||header.readUInt32LE(8)!==stat.size)fail('Invalid GLB header');
  let json,binStart=0,binLength=0,offset=12;
  while(offset<stat.size){if(offset+8>stat.size)fail('Truncated GLB chunk');const chunk=await read(offset,8),size=chunk.readUInt32LE(0),type=chunk.readUInt32LE(4);offset+=8;if(size%4||offset+size>stat.size)fail('Invalid GLB chunk extent');if(type===0x4e4f534a){if(json)fail('Duplicate JSON chunk');json=JSON.parse((await read(offset,size)).toString('utf8').trim());}if(type===0x004e4942){if(binStart)fail('Duplicate binary chunk');binStart=offset;binLength=size;}offset+=size;}
  if(!json||json.asset?.version!=='2.0')fail('Missing glTF 2.0 document');if((json.buffers||[]).some((b,i)=>i!==0||b.uri))fail('External GLB buffers are unsupported');if(json.buffers?.length&&(!binStart||json.buffers[0].byteLength>binLength))fail('Missing GLB buffer');
  const imageViews=new Set((json.images||[]).filter(image=>image.bufferView!==undefined).map(image=>image.bufferView)),referenced=new Set();
  const collect=value=>{if(!value||typeof value!=='object')return;for(const [key,item]of Object.entries(value)){if(key==='bufferView'&&Number.isInteger(item))referenced.add(item);else collect(item);}};
  for(const [key,value]of Object.entries(json))if(key!=='images')collect(value);
  const imageResources=(json.images||[]).map((image,index)=>{if(image.bufferView===undefined)return {index,uri:image.uri,mimeType:image.mimeType};const view=json.bufferViews?.[image.bufferView];if(!view||(view.buffer||0)!==0||!Number.isSafeInteger(view.byteLength)||view.byteLength<0||!Number.isSafeInteger(view.byteOffset||0)||(view.byteOffset||0)<0||(view.byteOffset||0)+view.byteLength>binLength)fail('Image exceeds GLB buffer');return {index,offset:binStart+(view.byteOffset||0),length:view.byteLength,mimeType:image.mimeType};});
  const chunks=[];let compactLength=0;
  for(let index=0;index<(json.bufferViews||[]).length;index++){const view=json.bufferViews[index];if((view.buffer||0)!==0||!Number.isSafeInteger(view.byteLength)||view.byteLength<0||!Number.isSafeInteger(view.byteOffset||0)||(view.byteOffset||0)<0||(view.byteOffset||0)+view.byteLength>binLength)fail('Invalid buffer view');if(imageViews.has(index)&&!referenced.has(index)){view.byteOffset=0;view.byteLength=0;continue;}const data=await read(binStart+(view.byteOffset||0),view.byteLength);view.byteOffset=compactLength;chunks.push(data);compactLength+=data.length;const padding=(4-compactLength%4)%4;if(padding){chunks.push(Buffer.alloc(padding));compactLength+=padding;}}
  if(json.buffers?.length)json.buffers[0].byteLength=compactLength;
  const materialDocument={asset:json.asset,materials:json.materials||[],textures:json.textures||[],samplers:json.samplers||[],images:(json.images||[]).map((image,index)=>{const copy={...image,uri:'images/'+index};delete copy.bufferView;return copy;}),...(json.extensionsUsed?{extensionsUsed:json.extensionsUsed}:{}),...(json.extensionsRequired?{extensionsRequired:json.extensionsRequired}:{}),...(json.extensions?{extensions:json.extensions}:{})};
  json.materials=(json.materials||[]).map(material=>({name:material.name,pbrMetallicRoughness:{baseColorFactor:[.55,.56,.58,1],metallicFactor:0,roughnessFactor:.8},doubleSided:true,extras:material.extras}));delete json.images;delete json.textures;delete json.samplers;
  signal?.throwIfAborted();return {json,bin:Buffer.concat(chunks,compactLength),imageResources,materialDocument};
 }finally{await file.close();}
}
function accessor(doc,index) {
  const {json,bin}=doc,a=json.accessors?.[index];
  if(!a || !COMPONENT[a.componentType] || !WIDTH[a.type] || !Number.isSafeInteger(a.count) || a.count<0) fail('Invalid accessor');
  const [size,get]=COMPONENT[a.componentType],width=WIDTH[a.type];if(a.count*width>16*1024*1024)fail('Accessor exceeds the supported decoded resource budget');const out=new Array(a.count*width).fill(0),view=new DataView(bin.buffer,bin.byteOffset,bin.length);
  const read=(bvIndex,byteOffset,count,stride,componentSize,method,target,targetWidth)=>{
    const bv=json.bufferViews?.[bvIndex];
    if(!bv || (bv.buffer||0)!==0) fail('Invalid buffer view');
    const start=(bv.byteOffset||0)+byteOffset, step=stride||componentSize*targetWidth;
    if(step<componentSize*targetWidth || start<(bv.byteOffset||0) || (count && start+(count-1)*step+componentSize*targetWidth>(bv.byteOffset||0)+bv.byteLength) || (bv.byteOffset||0)+bv.byteLength>bin.length) fail('Accessor exceeds buffer view');
    for(let i=0;i<count;i++) for(let j=0;j<targetWidth;j++) target[i*targetWidth+j]=view[method](start+i*step+j*componentSize,true);
  };
  if(a.bufferView!==undefined) read(a.bufferView,a.byteOffset||0,a.count,json.bufferViews?.[a.bufferView]?.byteStride,size,get,out,width);
  if(a.sparse) {
    const s=a.sparse,ic=COMPONENT[s.indices.componentType];
    if(!ic || ![5121,5123,5125].includes(s.indices.componentType) || s.count>a.count) fail('Invalid sparse accessor');
    const ids=[],values=[]; read(s.indices.bufferView,s.indices.byteOffset||0,s.count,0,ic[0],ic[1],ids,1); read(s.values.bufferView,s.values.byteOffset||0,s.count,0,size,get,values,width);
    let previous=-1; ids.forEach((id,i)=>{if(id<=previous || id>=a.count) fail('Invalid sparse index'); previous=id; for(let j=0;j<width;j++) out[id*width+j]=values[i*width+j];});
  }
  if(a.normalized && a.componentType!==5126) {const signed=[5120,5122].includes(a.componentType),max=2**(size*8-(signed?1:0))-1; for(let i=0;i<out.length;i++) out[i]=signed?Math.max(-1,out[i]/max):out[i]/max;}
  if(out.some(v=>!Number.isFinite(v))) fail('Non-finite accessor values');
  return {values:out,width,count:a.count};
}
function structure(doc) {
 const j=doc.json, parents=new Map(), joints=new Set(), binds=new Map();
 (j.nodes||[]).forEach((n,i)=>(n.children||[]).forEach(c=>{if(!j.nodes[c] || parents.has(c) || c===i) fail('Invalid node hierarchy'); parents.set(c,i);}));
 (j.skins||[]).forEach((s,skinId)=>{const inv=s.inverseBindMatrices===undefined?null:accessor(doc,s.inverseBindMatrices); if(inv && (inv.width!==16 || inv.count!==s.joints.length)) fail('Invalid skin inverseBind matrices'); for(let i=0;i<s.joints.length;i++){const id=s.joints[i];if(!j.nodes?.[id])fail('Invalid skin joint');joints.add(id);if(!binds.has(id))binds.set(id,[]);binds.get(id).push({skinId:String(skinId),matrix:inv?inv.values.slice(i*16,i*16+16):[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]});}});
 const pathFor=(i,seen=new Set())=>{if(seen.has(i))fail('Cyclic hierarchy');seen.add(i); const n=j.nodes[i],p=parents.get(i);return (p===undefined?'':pathFor(p,seen)+'/')+(n.name||'node_'+i);};
 return (j.nodes||[]).map((n,i)=>({id:String(i),name:n.name||'',path:pathFor(i),parentId:parents.has(i)?String(parents.get(i)):null,joint:joints.has(i),morphTargetCount:n.mesh===undefined?null:Math.max(0,...(j.meshes?.[n.mesh]?.primitives||[]).map(p=>p.targets?.length||0)),rest:{translation:n.translation||[0,0,0],rotation:n.rotation||[0,0,0,1],scale:n.scale||[1,1,1],...(n.matrix?{matrix:n.matrix}:{})},basis:{kind:'gltf',...(binds.has(i)?{bindMatrices:binds.get(i),...(binds.get(i).every(b=>JSON.stringify(b.matrix)===JSON.stringify(binds.get(i)[0].matrix))?{inverseBindMatrix:binds.get(i)[0].matrix}:{})}:{})}}));
}
function clipFrom(doc,index,metadataOnly=false) {
 const a=doc.json.animations?.[index]; if(!a)fail('Animation index does not exist');
 let start=Infinity,end=-Infinity; const tracks=[],summaries=new Map();
 for(const c of a.channels||[]) {
  const s=a.samplers?.[c.sampler],node=c.target?.node,prop=c.target?.path;
  if(!s || !doc.json.nodes?.[node] || !['translation','rotation','scale','weights'].includes(prop))fail('Invalid animation channel');
  const t=accessor(doc,s.input),v=accessor(doc,s.output),interpolation=s.interpolation||'LINEAR';
  if(t.width!==1 || !t.count || !['LINEAR','STEP','CUBICSPLINE'].includes(interpolation) || t.values.some((x,i)=>x<0 || (i && x<=t.values[i-1])))fail('Invalid animation times/interpolation');
  const multiplier=interpolation==='CUBICSPLINE'?3:1,width=prop==='rotation'?4:prop==='weights'?v.values.length/t.count/multiplier:3;
  if(!Number.isInteger(width) || width<1 || v.values.length!==t.count*width*multiplier)fail('Invalid animation output count');
  start=Math.min(start,t.values[0]);end=Math.max(end,t.values.at(-1));
  const id=String(node); if(!summaries.has(id))summaries.set(id,new Set());summaries.get(id).add(prop);
  if(!metadataOnly)tracks.push({nodeId:id,property:prop,times:t.values,values:v.values,interpolation});
 }
 if(!Number.isFinite(start)){start=0;end=0;}
 const summary={index,name:a.name||'Animation '+(index+1),startTime:start,duration:end-start,fps:null,tracks:[...summaries].map(([nodeId,p])=>({nodeId,properties:[...p]})),hasMorph:(a.channels||[]).some(c=>c.target?.path==='weights')};
 if(metadataOnly)return summary;
 tracks.forEach(t=>{t.times=t.times.map(v=>v-start);});
 return {version:1,name:summary.name,duration:summary.duration,fps:null,tracks};
}
function validateSkinAttributes(doc,primitive,skin,vertexCount){
 const attributes=primitive.attributes||{},indexes=new Set();for(const name of Object.keys(attributes)){const match=/^(?:JOINTS|WEIGHTS)_(\d+)$/.exec(name);if(match)indexes.add(Number(match[1]));}
 if(!indexes.size)fail('Skinned mesh is missing joint/weight attributes');const groups=[];
 for(let index=0;index<=Math.max(...indexes);index++){
  const ji=attributes['JOINTS_'+index],wi=attributes['WEIGHTS_'+index];if(ji===undefined||wi===undefined)fail('Skin JOINTS/WEIGHTS sets must be contiguous pairs');
  const ja=doc.json.accessors[ji],wa=doc.json.accessors[wi];if(![5121,5123].includes(ja?.componentType)||ja.normalized||![5121,5123,5126].includes(wa?.componentType)||(wa.componentType!==5126&&!wa.normalized))fail('Invalid skin accessor component type');
  const joints=accessor(doc,ji),weights=accessor(doc,wi);
  if(joints.width!==4||weights.width!==4||joints.count!==vertexCount||weights.count!==vertexCount||joints.values.some(i=>!Number.isInteger(i)||i<0||i>=skin.joints.length)||weights.values.some(w=>w<0))fail('Invalid skin influences');groups.push(weights.values);
 }
 for(let vertex=0;vertex<vertexCount;vertex++){let sum=0;for(const weights of groups)for(let lane=0;lane<4;lane++)sum+=weights[vertex*4+lane];if(!(sum>0)||!Number.isFinite(sum))fail('Skinned vertex has no finite positive weight sum');}
}
async function inspectGlb(source,{signal,geometryOnly=false}={}) {
 const doc=await (geometryOnly?readGeometryGlb:readGlb)(source,{signal}),j=doc.json,nodes=structure(doc);let vertexCount=0,meshCount=0;
 const checkedMeshes=new Map(),checkedSkins=new Set();
 for(const n of j.nodes||[])if(n.mesh!==undefined){
  const m=j.meshes?.[n.mesh];if(!m)fail('Invalid mesh reference');if(n.skin!==undefined&&!j.skins?.[n.skin])fail('Invalid node skin');
  let counts=checkedMeshes.get(n.mesh);
  if(!counts){counts=[];for(const p of m.primitives||[]){if(p.extensions?.KHR_draco_mesh_compression)fail('Draco-compressed GLB needs an unavailable decoder');if(p.attributes?.POSITION===undefined)fail('Mesh missing POSITION');const positions=accessor(doc,p.attributes.POSITION);if(positions.width!==3)fail('Invalid positions');if(p.indices!==undefined){const indices=accessor(doc,p.indices);if(indices.width!==1||indices.values.some(i=>!Number.isInteger(i)||i<0||i>=positions.count))fail('Invalid mesh indices');}counts.push(positions.count);}checkedMeshes.set(n.mesh,counts);}
  if(n.skin!==undefined){const key=n.mesh+':'+n.skin;if(!checkedSkins.has(key)){(m.primitives||[]).forEach((p,index)=>validateSkinAttributes(doc,p,j.skins[n.skin],counts[index]));checkedSkins.add(key);}}
  vertexCount+=counts.reduce((sum,count)=>sum+count,0);meshCount+=counts.length;
 }
 return {format:'glb',hasMeshes:meshCount>0,hasSkinnedMeshes:(j.nodes||[]).some(n=>n.mesh!==undefined && n.skin!==undefined),meshCount,vertexCount,nodes,clips:(j.animations||[]).map((_,i)=>clipFrom(doc,i,true)).filter(c=>c.duration>0&&c.tracks.length),externalResources:(doc.imageResources||j.images||[]).map(i=>i.uri).filter(u=>u&&!u.startsWith('data:')),coordinates:{detected:{axes:{up:'+Y',front:'+Z',right:'+X'},handedness:'right',unitMeters:1},canonical:{up:'+Y',unitMeters:1},sourceKind:'detected'},warnings:[]};
}
async function loadGlbClip(source,index,{signal}={}){return clipFrom(await readGlb(source,{signal}),index);}
module.exports={readGlb,readGeometryGlb,accessor,inspectGlb,loadGlbClip};
