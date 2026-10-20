'use strict';
// Explicit catalog membership for synthetic backend fixtures. Only fixture setup
// may enumerate a known fixture tree; the provider never discovers disk files.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const canonical=value=>path.resolve(value),key=value=>canonical(value).toLowerCase();
function createFixtureCatalog(){
 const libraries=new Map();
 function members(root){const k=key(root);if(!libraries.has(k))libraries.set(k,new Map());return libraries.get(k);}
 function register(root,source,id){
  root=canonical(root);source=canonical(source);id=String(id??BigInt('0x'+crypto.createHash('sha256').update(key(source)).digest('hex').slice(0,15)));
  const entries=members(root);for(const [existingId,item] of entries)if(key(item.path)===key(source)&&existingId!==id)throw new Error('Fixture path already has a different catalog ID');
  entries.set(id,{id,name:path.basename(source),folderId:'0',path:source,relativePath:path.relative(root,source).replaceAll('\\','/'),isLink:false,pathError:null});return id;
 }
 async function registerFixture(root,baseSource,baseId='42',fixtureRoot=root){
  register(root,baseSource,baseId);
  async function visit(directory){for(const entry of (await fs.readdir(directory,{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name))){const source=path.join(directory,entry.name);if(entry.isDirectory())await visit(source);else if(entry.isFile()&&['.fbx','.glb'].includes(path.extname(entry.name).toLowerCase())&&key(source)!==key(baseSource))register(root,source);}}
  await visit(fixtureRoot);
 }
 async function snapshot({libraryRoot:root,scopeRoot,signal}={}){
  if(signal?.aborted)throw Object.assign(new Error('Aborted'),{code:'ABORT_ERR'});root=canonical(root);if(typeof scopeRoot!=='string'||!path.isAbsolute(scopeRoot))throw new Error('Fixture model scope is required');scopeRoot=canonical(scopeRoot);const relative=path.relative(root,scopeRoot);if(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative))throw new Error('Fixture scope is outside the library');const items=[];
  for(const item of members(root).values()){const relative=path.relative(scopeRoot,item.path);if(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative))continue;let exists=false;try{exists=(await fs.stat(item.path)).isFile();}catch(error){if(!['ENOENT','ENOTDIR'].includes(error.code))throw error;}items.push({...item,exists});}
  items.sort((a,b)=>BigInt(a.id)<BigInt(b.id)?-1:BigInt(a.id)>BigInt(b.id)?1:0);
  const value={schemaVersion:2,libraryRoot:root,scopeRoot,scopeMode:'model-directory-recursive',items};return {...value,complete:true,revision:crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex')};
 }
 return {register,registerFixture,getLibraryCandidates:snapshot,remove(root,id){members(root).delete(String(id));},rename(root,id,source){if(!members(root).has(String(id)))throw new Error('Cannot rename an unregistered fixture');return register(root,source,id);}};
}
module.exports={createFixtureCatalog};
