'use strict';
const fs=require('node:fs'),fsp=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {Readable}=require('node:stream');
const {within}=require('./preview-runtime.cjs');
const MIME={'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css','.json':'application/json','.gltf':'model/gltf+json','.glb':'model/gltf-binary','.bin':'application/octet-stream','.data':'application/octet-stream','.wasm':'application/wasm','.hdr':'application/octet-stream','.png':'image/png','.jpg':'image/jpeg','.jpeg':'image/jpeg','.webp':'image/webp','.ktx2':'image/ktx2'};
const SOURCE_MIME={...Object.fromEntries('glb gltf fbx vtk obj stl dae ply spline splinecode usdz usdc usd usda 3dm 3mf amf lwo 3ds pcd vox xyz bvh ifc kmz bin mtl tga dds ktx basis exr hdr'.split(' ').map(ext=>['.'+ext,'application/octet-stream'])),'.gltf':'model/gltf+json','.json':'application/json','.png':'image/png','.jpg':'image/jpeg','.jpeg':'image/jpeg','.webp':'image/webp','.gif':'image/gif','.bmp':'image/bmp','.ktx2':'image/ktx2'};
const WORKER_CSP="default-src 'none'; script-src 'self' 'unsafe-eval' 'wasm-unsafe-eval'; connect-src 'self' data: blob:; worker-src 'self'; object-src 'none'";
function createAnimationAssets({protocol,directory,cacheRoot}){
 const tokens=new Map(),workers=new Map(),nonce=crypto.randomBytes(20).toString('base64');
 const sameStat=(a,b)=>a&&b&&['size','mtimeMs','ctimeMs','dev','ino'].every(key=>a[key]===b[key]);
 const csp=`default-src 'none'; script-src 'self' 'nonce-${nonce}'; style-src 'self' 'unsafe-inline'; connect-src 'self' data: blob:; img-src 'self' data: blob:; worker-src 'self' blob:; font-src 'self'; object-src 'none'; base-uri 'none'`;
 protocol.handle('billfish-animation',async request=>{
  try{
   const url=new URL(request.url);if(url.hostname!=='app'||request.method!=='GET')return new Response(null,{status:403});
   const parts=decodeURIComponent(url.pathname).split('/').filter(Boolean);let root,file,sourceAsset=false;
   if(parts[0]==='core-workers'){
    const worker=parts.length===3&&parts[2]==='worker.js'&&workers.get(parts[1]);if(!worker)return new Response(null,{status:404});const code=await worker.source;if(typeof code!=='string')return new Response(null,{status:404});return new Response(code,{headers:{'Content-Type':'text/javascript','Content-Security-Policy':WORKER_CSP,'X-Content-Type-Options':'nosniff','Cache-Control':'no-store','Cross-Origin-Resource-Policy':'same-origin'}});
   }
   if(['assets','sources'].includes(parts[0])){const token=tokens.get(parts[1]);if(!token||token.kind!==parts[0])return new Response(null,{status:404});
    if(parts[0]==='assets'&&parts[2]==='images'){
     if(parts.length!==4||!/^\d+$/.test(parts[3])||!token.images)return new Response(null,{status:403});
     const image=token.images[Number(parts[3])];if(!image||!sameStat(token.sourceFingerprint,await fsp.stat(token.source)))return new Response(null,{status:409});
     let mime=image.mimeType,body,streamFile,streamOptions;
     if(image.offset!==undefined){streamFile=token.source;streamOptions={start:image.offset,end:image.offset+image.length-1,signal:token.controller.signal};}
     else if(image.uri?.startsWith('data:')){const match=/^data:([^;,]+)(;base64)?,([\s\S]*)$/.exec(image.uri);if(!match)return new Response(null,{status:404});mime=match[1];body=match[2]?Buffer.from(match[3],'base64'):Buffer.from(decodeURIComponent(match[3]));}
     else{
      if(typeof image.uri!=='string'||/^[a-z]+:/i.test(image.uri)||path.isAbsolute(image.uri))return new Response(null,{status:403});
      const imagePath=path.resolve(path.dirname(token.source),decodeURIComponent(image.uri)),dependency=token.dependencies.find(item=>item.path&&path.resolve(item.path).toLowerCase()===imagePath.toLowerCase());
      if(!within(token.libraryRoot,imagePath)||!dependency||!sameStat(dependency,await fsp.stat(imagePath)))return new Response(null,{status:409});
      mime||=SOURCE_MIME[path.extname(imagePath).toLowerCase()];streamFile=imagePath;streamOptions={signal:token.controller.signal};
     }
     if(!/^image\/[a-z0-9.+-]+$/i.test(mime||''))return new Response(null,{status:415});
     if(streamFile)body=Readable.toWeb(fs.createReadStream(streamFile,streamOptions));
     return new Response(body,{headers:{'Content-Type':mime,'Content-Security-Policy':csp,'X-Content-Type-Options':'nosniff','Cache-Control':'no-store'}});
    }
    root=token.root;sourceAsset=parts[0]==='sources';file=path.resolve(root,...parts.slice(2));}
   else {if(!['ui','vendor'].includes(parts[0]))return new Response(null,{status:403});root=path.resolve(directory,parts[0]);file=path.resolve(root,...parts.slice(1));}
   const mime=(sourceAsset?SOURCE_MIME:MIME)[path.extname(file).toLowerCase()];if(!within(root,file)||!mime)return new Response(null,{status:403});
   const stat=await fsp.stat(file);if(!stat.isFile())return new Response(null,{status:404});
   const dedicatedWorker=within(path.join(directory,'vendor','core','lib'),file)&&file.endsWith('.worker.js');const headers={'Content-Type':mime,'Content-Security-Policy':dedicatedWorker?WORKER_CSP:csp,'X-Content-Type-Options':'nosniff','Cache-Control':'no-store'};
   if(path.extname(file)==='.html')return new Response((await fsp.readFile(file,'utf8')).replaceAll('<script',`<script nonce="${nonce}"`),{headers});
   return new Response(Readable.toWeb(fs.createReadStream(file)),{headers});
  }catch{return new Response(null,{status:404});}
 });
 return {entry:'billfish-animation://app/ui/index.html',register(model,libraryRoot){
  if(!model?.path||!within(cacheRoot,model.path))throw new Error('Model output must belong to the derived cache');
  const token=crypto.randomUUID(),entry={root:path.dirname(model.path),kind:'assets',controller:new AbortController()};
  if(model.progressiveMaterials){
   if(!model.source||!libraryRoot||!within(libraryRoot,model.source)||!model.sourceFingerprint||!model.materialsPath||!within(entry.root,model.materialsPath)||!Array.isArray(model.imageResources))throw new Error('Progressive images must belong to the current bound model');
   entry.source=model.source;entry.sourceFingerprint=model.sourceFingerprint;entry.libraryRoot=libraryRoot;entry.dependencies=model.dependencies||[];entry.images=model.imageResources;
   for(let index=0;index<entry.images.length;index++){const image=entry.images[index];if(image.index!==index||image.offset!==undefined&&(!Number.isSafeInteger(image.offset)||image.offset<0||!Number.isSafeInteger(image.length)||image.length<=0||image.offset+image.length>model.sourceFingerprint.size))throw new Error('Invalid progressive image extent');}
  }
  tokens.set(token,entry);return {token,url:`billfish-animation://app/assets/${token}/${encodeURIComponent(path.basename(model.path))}`,...(model.materialsPath?{materialsUrl:`billfish-animation://app/assets/${token}/${encodeURIComponent(path.basename(model.materialsPath))}`}:{})};
 },registerSource(source,libraryRoot){if(!within(libraryRoot,source))throw new Error('The model source must belong to the bound library');const token=crypto.randomUUID();tokens.set(token,{root:path.resolve(libraryRoot),kind:'sources'});return {token,url:`billfish-animation://app/sources/${token}/${path.relative(libraryRoot,source).split(path.sep).map(encodeURIComponent).join('/')}`};},reserveWorker(){const token=crypto.randomUUID();let resolve;const source=new Promise(done=>{resolve=done;});const timer=setTimeout(()=>resolve(null),15000);workers.set(token,{source,resolve,timer});return {token,url:`billfish-animation://app/core-workers/${token}/worker.js`};},completeWorker(token,code){const worker=workers.get(token);if(!worker||typeof code!=='string'||Buffer.byteLength(code)>16*1024*1024)throw new Error('Invalid owned Core worker source');clearTimeout(worker.timer);worker.resolve(code);},revoke(token){tokens.get(token)?.controller?.abort();tokens.delete(token);const worker=workers.get(token);if(worker){clearTimeout(worker.timer);worker.resolve(null);workers.delete(token);}},shutdown(){for(const token of tokens.values())token.controller?.abort();tokens.clear();for(const worker of workers.values()){clearTimeout(worker.timer);worker.resolve(null);}workers.clear();protocol.unhandle('billfish-animation');}};
}
module.exports={createAnimationAssets};
