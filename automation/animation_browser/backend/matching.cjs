'use strict';
const crypto=require('node:crypto');
const hash=v=>crypto.createHash('sha256').update(JSON.stringify(v)).digest('hex');
const clean=n=>String(n).split(':').at(-1);
function near(a,b,tolerance=1e-5){if(Array.isArray(a)&&Array.isArray(b))return a.length===b.length&&a.every((x,i)=>near(x,b[i],tolerance));if(typeof a==='number'&&typeof b==='number')return Math.abs(a-b)<=tolerance;return JSON.stringify(a)===JSON.stringify(b);}
function fingerprints(metadata){const nodes=metadata.nodes||[],byId=new Map(nodes.map(n=>[n.id,n])); return {topology:hash(nodes.filter(n=>n.joint).map(n=>[n.name,byId.get(n.parentId)?.name||null]).sort()),basis:hash(nodes.filter(n=>n.joint).map(n=>[n.path,n.basis||{}]).sort())};}
function matchSkeleton(target,source,clip){
 const targets=target.nodes||[],sources=source.nodes||[],tb=new Map(targets.map(n=>[n.id,n])),sb=new Map(sources.map(n=>[n.id,n])),mapping={},ambiguities=[],unmatched=[],hierarchy=[],basisDifferences=[],unknownBasis=[],required=new Set();
 for(const t of clip.tracks||[]){let n=sb.get(t.nodeId);const seen=new Set();while(n&&!seen.has(n.id)){seen.add(n.id);required.add(n.id);n=sb.get(n.parentId);}}
 // A file that declares a skeleton must describe the same rig, including unanimated joints.
 // Target-only bones may legitimately stay at their reference state.
 for(const n of sources.filter(n=>n.joint)){let p=n;const seen=new Set();while(p&&!seen.has(p.id)){seen.add(p.id);required.add(p.id);p=sb.get(p.parentId);}}
 const byPath=new Map(),byName=new Map(),byClean=new Map();
 const add=(map,key,value)=>{if(!map.has(key))map.set(key,[]);map.get(key).push(value);};
 targets.forEach(n=>{add(byPath,n.path,n);add(byName,n.name,n);add(byClean,clean(n.name),n);});
 const sourceClean=new Map();sources.forEach(n=>add(sourceClean,clean(n.name),n));
 const used=new Set();
 for(const id of required){const n=sb.get(id);if(!n){unmatched.push(id);continue;}let candidates=byPath.get(n.path)||[];
  if(candidates.length!==1)candidates=byName.get(n.name)||[];
  if(candidates.length!==1&&sourceClean.get(clean(n.name))?.length===1)candidates=byClean.get(clean(n.name))||[];
  if(candidates.length!==1){(candidates.length?ambiguities:unmatched).push({nodeId:id,name:n.name,path:n.path,candidates:candidates.map(x=>x.id)});continue;}
  if(used.has(candidates[0].id)){ambiguities.push({nodeId:id,name:n.name,candidates:[candidates[0].id],reason:'non-bijective'});continue;}
  mapping[id]=candidates[0].id;used.add(candidates[0].id);
 }
 for(const id of required){const n=sb.get(id),t=tb.get(mapping[id]);if(!n||!t)continue;
  if(n.parentId!==null && n.parentId!==undefined && mapping[n.parentId]!==t.parentId)hierarchy.push({nodeId:id,targetId:t.id,sourceParent:n.parentId,targetParent:t.parentId});
  if((n.parentId===null||n.parentId===undefined)&&t.parentId!==null&&t.parentId!==undefined)hierarchy.push({nodeId:id,reason:'root-parent-mismatch'});
  const a=n.basis||{},b=t.basis||{};let checked=false;
  for(const key of ['preRotation','postRotation','rotationOrder','inheritMode','geometryTransform']) if(a[key]!==undefined&&b[key]!==undefined){checked=true;if(!near(a[key],b[key]))basisDifferences.push({nodeId:id,targetId:t.id,field:key,source:a[key],target:b[key]});}
  if(a.inverseBindMatrix&&b.inverseBindMatrix){checked=true;if(!near(a.inverseBindMatrix,b.inverseBindMatrix,1e-4))basisDifferences.push({nodeId:id,targetId:t.id,field:'inverseBindMatrix'});}
  if(source.format===target.format&&a.bindMatrices?.length&&b.bindMatrices?.length){checked=true;const missing=a.bindMatrices.filter(sourceBind=>!b.bindMatrices.some(targetBind=>near(sourceBind.matrix,targetBind.matrix,1e-4)));if(missing.length)basisDifferences.push({nodeId:id,targetId:t.id,field:'bindMatrices',space:source.format==='glb'?'gltf-inverse-bind':'fbx-bind-to-world',source:missing,target:b.bindMatrices});}
  // Authored first-pose TRS is not a bind basis. Never compare it as a retarget test.
  if(!checked)unknownBasis.push(id);
 }
 const tracks=clip.tracks||[],matchedTracks=tracks.filter(t=>mapping[t.nodeId]).length;
 const missingIds=new Set(unmatched.map(x=>typeof x==='string'?x:x.nodeId)),criticalMissing=[...missingIds].filter(id=>{const n=sb.get(id);return !n||n.parentId===null||n.parentId===undefined||sources.some(child=>child.parentId===id&&mapping[child.id]);});
 const morphIssues=[];for(const track of tracks.filter(t=>t.properties?.includes('weights'))){const t=tb.get(mapping[track.nodeId]);if(!t?.morphTargetCount)morphIssues.push({nodeId:track.nodeId,code:'NO_TARGET_MORPH'});else morphIssues.push({nodeId:track.nodeId,code:'MORPH_TARGET_IDENTITY_UNVERIFIED',targetCount:t.morphTargetCount});}
 let compatibility=criticalMissing.length||ambiguities.length||hierarchy.length||basisDifferences.length||morphIssues.some(x=>x.code==='NO_TARGET_MORPH')?'incompatible':unmatched.length||unknownBasis.length||morphIssues.length?'partial':'full';
 if(!tracks.length||!matchedTracks)compatibility='incompatible';
 const report={sourceFormat:source.format,targetFormat:target.format,totalTracks:tracks.length,matchedTracks,requiredNodes:required.size,unmatched,criticalMissing,ambiguities,hierarchy,basisDifferences,unknownBasis,morphIssues,hasMorph:!!clip.hasMorph,retargeting:false,compatibility};
 return {compatibility,mapping,report};
}
module.exports={matchSkeleton,fingerprints,hash};
