'use strict';
const fs=require('node:fs'),fsp=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');

function failure(code,message,details){return Object.assign(new Error(message),{code,details});}
async function fingerprintFile(file,{signal}={}){
  signal?.throwIfAborted();
  const before=await fsp.stat(file,{bigint:true});if(!before.isFile())throw failure('viewer_source_changed','The viewer source is not a regular file');
  const hash=crypto.createHash('sha256');for await(const chunk of fs.createReadStream(file,{signal,highWaterMark:1024*1024}))hash.update(chunk);
  signal?.throwIfAborted();const after=await fsp.stat(file,{bigint:true});
  signal?.throwIfAborted();if(before.size!==after.size||before.mtimeNs!==after.mtimeNs)throw failure('viewer_source_changed','The model changed while its viewer was being opened');
  return Object.freeze({sha256:hash.digest('hex'),bytes:before.size.toString(),mtime_ns:before.mtimeNs.toString()});
}
function validatePose(pose){
  if(!pose||!Number.isInteger(pose.clip_index)||pose.clip_index<0||typeof pose.clip_name!=='string'||pose.clip_name.length>1024||typeof pose.time!=='number'||!Number.isFinite(pose.time)||pose.time<0||typeof pose.fps!=='number'||!Number.isFinite(pose.fps)||pose.fps<1||pose.fps>240)throw failure('invalid_arguments','A valid selected animation pose is required');
  return {clip_index:pose.clip_index,clip_name:pose.clip_name,time:pose.time,fps:pose.fps};
}
function validateBuiltinPose(pose,owner){
  if(pose?.kind==='base'&&pose.time===0)return {kind:'base',time:0};
  const current=owner.currentClip;
  if(pose?.kind!=='clip'||!current||pose.rowId!==current.rowId||pose.clip_index!==current.clipIndex||typeof pose.clip_name!=='string'||pose.clip_name.length>1024||!Number.isFinite(pose.time)||pose.time<0||!(pose.fps===null||(Number.isFinite(pose.fps)&&pose.fps>0)))throw failure('invalid_arguments','Cover must identify the accepted current animation pose');
  if(current.row&&(pose.clip_name!==current.row.name||pose.fps!==(current.row.fps??null)||pose.time>current.row.duration+0.00001))throw failure('invalid_arguments','Cover pose must match the accepted animation metadata');
  return {kind:'clip',rowId:pose.rowId,clip_index:pose.clip_index,clip_name:pose.clip_name,time:pose.time,fps:pose.fps};
}
function createPreviewCoverBridge({send,stagingRoot,nativeImage,inspectPng,timeoutMs=90000}){
  const pending=new Map();let stopped=false;
  function finish(id,data,error){const entry=pending.get(id);if(!entry)return;pending.delete(id);clearTimeout(entry.timer);if(error)entry.reject(error);else entry.resolve(data);}
  return {
    async publish(owner,payload){
      if(stopped)throw failure('plugin_disabled','The plugin session stopped');
      if(!owner||owner.request.kind!=='viewer'||!/^\d+$/.test(owner.request.item.id)||!owner.sourceFingerprint)throw failure('invalid_preview_owner','A native model viewer with its original source identity is required');
      if(!payload||!(Buffer.isBuffer(payload.png)||ArrayBuffer.isView(payload.png)))throw failure('invalid_arguments','The selected frame PNG is missing');
      const png=Buffer.from(payload.png.buffer,payload.png.byteOffset,payload.png.byteLength);
      if(png.length<33||png.length>64*1024*1024)throw failure('invalid_thumbnail_output','Selected frame PNG has an invalid byte length');
      const pose=owner.builtin?validateBuiltinPose(payload.pose,owner):validatePose(payload.pose),id='cover-'+crypto.randomUUID(),dest=path.join(stagingRoot,id+'.png');
      const animation_source=owner.builtin&&pose.kind==='clip'?{path:owner.currentClip.source,fingerprint:owner.currentClip.nativeFingerprint,clip_index:owner.currentClip.clipIndex,rowId:owner.currentClip.rowId}:undefined;
      await fsp.mkdir(stagingRoot,{recursive:true});await fsp.writeFile(dest,png,{flag:'wx'});
      let submitted=false,verified=false;
      try{
        if(stopped)throw failure('plugin_disabled','The plugin session stopped');
        const image=inspectPng(dest),decoded=nativeImage.createFromPath(dest);
        if(decoded.isEmpty()||decoded.getSize().width!==image.width||decoded.getSize().height!==image.height)throw failure('invalid_thumbnail_output','Selected frame PNG cannot be decoded');
        const response=await new Promise((resolve,reject)=>{
          const timer=setTimeout(()=>finish(id,null,failure('preview_cover_timeout','Billfish did not confirm the selected frame cover in time')),timeoutMs);
          pending.set(id,{resolve,reject,timer,itemId:owner.request.item.id,pose,animation_source});
          try{submitted=true;send({type:'preview_cover',request_id:id,item:{id:owner.request.item.id,path:owner.request.source},source:{...owner.sourceFingerprint},dest,pose,...(animation_source?{animation_source}:{})});}
          catch(error){finish(id,null,error);}
        });
        verified=true;return response;
      }catch(error){if(submitted)error.details={...error.details,staging_path:dest};throw error;}
      finally{if(!submitted||verified)try{await fsp.unlink(dest);}catch(error){if(error.code!=='ENOENT')console.error('Could not remove selected-frame staging file:',error.message);}}
    },
    receive(message){
      const entry=pending.get(message.request_id);if(!entry)return;
      if(message.error){finish(message.request_id,null,failure(message.error.code||'preview_cover_failed',message.error.message||'Billfish could not set the selected frame cover',message.error.details));return;}
      const data=message.data;
      if(!data||data.completion!=='native_custom_cover_verified'||data.item_id!==entry.itemId||data.source_unchanged!==true||data.metadata_unchanged!==true||typeof data.cover_path!=='string'||!data.cover_path||!data.image||!(data.image.width>0)||!(data.image.height>0)||data.pose?.clip_index!==entry.pose.clip_index||data.pose?.clip_name!==entry.pose.clip_name||data.pose?.time!==entry.pose.time||data.pose?.fps!==entry.pose.fps){finish(message.request_id,null,failure('cover_not_confirmed','Billfish did not confirm this selected pose as the native cover'));return;}
      const external=entry.animation_source,actual=data.animation_source;
      const externalMatches=external?actual&&['path','clip_index','rowId'].every(k=>actual[k]===external[k])&&['sha256','bytes','mtime_ns'].every(k=>actual.fingerprint?.[k]===external.fingerprint[k]):actual===undefined;
      if(entry.pose.kind&&(data.pose?.kind!==entry.pose.kind||data.pose?.rowId!==entry.pose.rowId||!externalMatches)){finish(message.request_id,null,failure('cover_not_confirmed','Billfish did not confirm the animation source identity'));return;}
      finish(message.request_id,data);
    },
    shutdown(){stopped=true;for(const id of [...pending.keys()])finish(id,null,failure('plugin_disabled','The plugin session stopped'));}
  };
}
module.exports={fingerprintFile,createPreviewCoverBridge,validatePose,validateBuiltinPose};
