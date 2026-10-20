'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {hash}=require('./matching.cjs');
async function readJson(p,fallback=null){try{return JSON.parse(await fs.readFile(p,'utf8'));}catch(e){if(['ENOENT','ENOTDIR'].includes(e.code)||e instanceof SyntaxError)return fallback;throw e;}}
async function atomicJson(p,value){await fs.mkdir(path.dirname(p),{recursive:true});const tmp=p+'.'+crypto.randomUUID()+'.tmp';await fs.writeFile(tmp,JSON.stringify(value));await fs.rename(tmp,p);}
class ProfileStore {
 constructor(root){this.root=root;this.writerId=crypto.randomUUID();}
 directory(key){return path.join(this.root,'revisions',hash(key));}
 async read(key){const dir=this.directory(key);let files=[];try{files=await fs.readdir(dir);}catch(e){if(e.code!=='ENOENT')throw e;}
  const revisions=[];for(const file of files.filter(x=>x.endsWith('.json'))){const r=await readJson(path.join(dir,file));if(r?.schemaVersion===1&&r.key===key&&typeof r.revision==='string'&&Array.isArray(r.parents))revisions.push(r);}
  const parentIds=new Set(revisions.flatMap(r=>r.parents));const heads=revisions.filter(r=>!parentIds.has(r.revision));
  // Conflicts deliberately have no implicit winner. Consumers must not apply one branch.
  return {revision:heads.length===1?heads[0].revision:null,heads,conflicts:heads.length>1?heads:[],value:heads.length===1?heads[0].value:{}};
 }
 async update(key,patch,parents){const before=await this.read(key);if(parents===undefined){if(before.conflicts.length){const e=new Error('Profile has concurrent revisions; resolve both branches explicitly');e.code='PROFILE_CONFLICT';e.conflicts=before.conflicts;throw e;}parents=before.heads.map(r=>r.revision);}
  if(!Array.isArray(parents))throw new Error('parents must be revision IDs');
  const all=new Map();let files=[];try{files=await fs.readdir(this.directory(key));}catch(e){if(e.code!=='ENOENT')throw e;}
  for(const f of files.filter(x=>x.endsWith('.json'))){const r=await readJson(path.join(this.directory(key),f));if(r)all.set(r.revision,r);}
  if(parents.some(p=>!all.has(p)))throw new Error('Unknown parent revision');
  const base=parents.length===1?all.get(parents[0]).value:before.conflicts.length?{}:before.value;
  const revision=crypto.randomUUID(),record={schemaVersion:1,key,revision,parents:[...new Set(parents)],writerId:this.writerId,createdAt:new Date().toISOString(),value:merge(base,patch)};
  await fs.mkdir(this.directory(key),{recursive:true});const destination=path.join(this.directory(key),revision+'.json'),temporary=destination+'.'+crypto.randomUUID()+'.tmp';await fs.writeFile(temporary,JSON.stringify(record),{flag:'wx'});await fs.rename(temporary,destination);
  return this.read(key);
 }
}
function merge(a,b){const out={...a};for(const [k,v] of Object.entries(b||{})){if(v&&typeof v==='object'&&!Array.isArray(v))out[k]=Object.keys(v).length?merge(a?.[k]||{},v):{};else out[k]=v;}return out;}
module.exports={ProfileStore,readJson,atomicJson,merge};
