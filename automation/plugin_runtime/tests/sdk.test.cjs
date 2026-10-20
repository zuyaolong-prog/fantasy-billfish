'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {createEagle} = require('../eagle-sdk.cjs');
const {validateSchema,validate} = require('../schema.cjs');

const row = {id:'9223372036854775806',name:'texture.png',note:'before',origin:'https://example.invalid/old',
  path:'Z:/ordinary/texture.png',file_size:123,thumbnail_path:null,tags:[{id:'1',name:'old'}]};
function sdk(call) { return createEagle({call,local:async () => {},plugin:{path:'Z:/plugin',manifest:{}},app:{theme:'DARK',isDark:true},register:() => {},progress:() => {}}).eagle; }

test('native selection records hydrate Items without inventing selection', async () => {
  const calls = [];
  const eagle = sdk(async (operation,args) => { calls.push({operation,args}); return {items:[row],total:1,library_root:'Z:/ordinary'}; });
  const [item] = await eagle.item.getSelected();
  assert.equal(item.id,row.id); assert.equal(item.filePath,row.path); assert.equal(item.name,'texture'); assert.equal(item.size,123);
  assert.deepEqual(calls.map(value => value.operation),['items.selected']);
  await assert.rejects(sdk(async () => { throw Object.assign(new Error('No library'),{code:'library_not_open'}); }).item.getSelected(),{code:'library_not_open'});
});

test('save only dirty supported fields and reject unsupported writes before any call', async () => {
  const calls = [];
  const eagle = sdk(async (operation,args) => { calls.push({operation,args}); return operation === 'items.get' ? structuredClone(row) : {completion:'persisted_metadata_verified'}; });
  const item = await eagle.item.getById(row.id);
  item.annotation = 'new note'; item.url = 'https://example.invalid/new';
  assert.equal(await item.save(),true);
  assert.deepEqual(calls[1],{operation:'items.update',args:{updates:[{item_id:row.id,note:'new note',origin:'https://example.invalid/new'}],dry_run:false}});
  const count = calls.length;
  item.name = 'unsupported'; item.annotation = 'must not write';
  await assert.rejects(item.save(),{code:'unsupported_write'}); assert.equal(calls.length,count);
  await assert.rejects(eagle.item.get({query:'ignored-filter'}),{code:'unsupported_query'});
  assert.throws(() => eagle.item.addBookmark,{code:'unsupported_api'});
});

test('edits made while save awaits remain dirty and are submitted by the next save', async () => {
  const writes = []; let resolveFirst;
  const eagle = sdk(async (operation,args) => {
    if (operation === 'items.get') return structuredClone(row);
    writes.push(structuredClone(args));
    if (writes.length === 1) await new Promise(resolve => { resolveFirst = resolve; });
    return {completion:'persisted_metadata_verified'};
  });
  const item = await eagle.item.getById(row.id);
  item.annotation = 'submitted';
  const first = item.save();
  item.annotation = 'edited-during-save';
  resolveFirst(); await first; await item.save();
  assert.equal(writes.length,2);
  assert.equal(writes[0].updates[0].note,'submitted');
  assert.equal(writes[1].updates[0].note,'edited-during-save');
});

test('in-place tag edits resolve native names and a failed step never claims total success', async () => {
  const calls = [];
  const eagle = sdk(async (operation,args) => {
    calls.push({operation,args});
    if (operation === 'items.get') return structuredClone(row);
    if (operation === 'tags.list') return {items:[{id:'1',name:'old'},{id:'2',name:'new'}]};
    if (operation === 'tags.assign') throw Object.assign(new Error('native failed'),{code:'native_error'});
    return {completion:'persisted_metadata_verified'};
  });
  const item = await eagle.item.getById(row.id); item.tags.push('new'); item.annotation = 'written first';
  await assert.rejects(item.save(),error => error.code === 'partial_update' && error.details.completed_operations.includes('items.update'));
  assert.deepEqual(calls.find(call => call.operation === 'tags.assign').args,{item_ids:[row.id],tag_ids:['2'],mode:'add',dry_run:false});
});

test('schema subset rejects unknown constraints and validates before command execution', () => {
  const schema = {type:'object',properties:{name:{type:'string',minLength:2,maxLength:4},count:{type:'integer',minimum:1,maximum:3},ids:{type:'array',minItems:1,maxItems:2,items:{type:'string'}},mode:{type:'string',enum:['a','b']}},required:['name','count','ids','mode'],additionalProperties:false};
  validateSchema(schema);
  const valid = {name:'good',count:2,ids:['1'],mode:'a'}; validate(valid,schema);
  for (const value of [{},{...valid,name:'x'},{...valid,count:true},{...valid,count:4},{...valid,ids:[]},{...valid,mode:'c'},{...valid,extra:1}]) assert.throws(() => validate(value,schema),{code:'invalid_arguments'});
  assert.throws(() => validateSchema({type:'object',oneOf:[]}),/Unsupported schema keyword/);
});

test('replaceFile awaits persisted media and refreshes content while preserving pending metadata', async () => {
  const calls = []; let finish;
  const refreshed = {...row,file_size:456,width:768,height:768,thumbnail_path:'Z:/ordinary/thumb.jpg',
    note:'server note',origin:'https://example.invalid/server',tags:[{id:'9',name:'server'}]};
  const eagle = sdk(async (operation,args) => {
    calls.push({operation,args});
    if (operation === 'items.get') return structuredClone(row);
    if (operation === 'items.replace_file') return new Promise(resolve => { finish = resolve; });
    if (operation === 'tags.list') return {items:[{id:'1',name:'old'},{id:'2',name:'new'}]};
    return {};
  });
  const item = await eagle.item.getById(row.id);
  item.annotation = 'pending'; item.tags.push('new');
  let returned = false;
  const replacing = item.replaceFile('C:/outputs/enlarged.png').then(value => { returned = true; return value; });
  await Promise.resolve(); assert.equal(returned,false);
  item.url = 'https://example.invalid/edited-during-replacement';
  finish({completion:'native_replace_persisted',item_id:row.id,item:refreshed,analysis_ready:true,source_safe_to_delete:true});
  assert.equal(await replacing,true);
  assert.equal(item.size,456); assert.equal(item.width,768); assert.equal(item.height,768);
  assert.equal(item.filePath,row.path); assert.equal(item.thumbnailPath,refreshed.thumbnail_path);
  assert.equal(item.thumbnailURL,'file:///Z:/ordinary/thumb.jpg');
  assert.equal(item.annotation,'pending'); assert.deepEqual(item.tags,['old','new']);
  assert.equal(item._baseline.size,456); assert.equal(item._baseline.width,768);
  assert.equal(item._baseline.annotation,row.note); assert.deepEqual(item._baseline.tags,['old']);
  assert.deepEqual(calls[1],{operation:'items.replace_file',args:{item_id:row.id,source_path:'C:/outputs/enlarged.png',dry_run:false}});
  await item.save();
  assert.deepEqual(calls.find(call => call.operation === 'items.update').args,
    {updates:[{item_id:row.id,note:'pending',origin:'https://example.invalid/edited-during-replacement'}],dry_run:false});
  assert.deepEqual(calls.find(call => call.operation === 'tags.assign').args,
    {item_ids:[row.id],tag_ids:['2'],mode:'add',dry_run:false});
});

test('replaceFile rejects incomplete and mismatched confirmations without reissuing a write', async () => {
  const confirmed = {completion:'native_replace_persisted',item_id:row.id,
    item:{...row,file_size:456,width:768,height:768},analysis_ready:true,source_safe_to_delete:true};
  for (const response of [false,{}, {...confirmed,completion:'accepted'}, {...confirmed,item_id:40},
      {...confirmed,item:{...confirmed.item,id:'1'}}, {...confirmed,analysis_ready:false},
      {...confirmed,source_safe_to_delete:false}, {...confirmed,item:{id:row.id}},
      {...confirmed,item:{...confirmed.item,width:0}}]) {
    let writes = 0;
    const item = await sdk(async operation => {
      if (operation === 'items.get') return structuredClone(row);
      writes++; return response;
    }).item.getById(row.id);
    await assert.rejects(item.replaceFile('C:/outputs/enlarged.png'),
      error => ['replace_not_confirmed','invalid_host_response'].includes(error.code));
    assert.equal(writes,1); assert.equal(item.size,row.file_size);
  }
  const nativeError = Object.assign(new Error('replacement analysis timed out'),
    {code:'replace_analysis_timeout',details:{job_id:'job-1',backup_path:'C:/backup.png'}});
  let writes = 0;
  const item = await sdk(async operation => {
    if (operation === 'items.get') return structuredClone(row);
    writes++; throw nativeError;
  }).item.getById(row.id);
  await assert.rejects(item.replaceFile('C:/outputs/enlarged.png'),error => error === nativeError);
  assert.equal(writes,1);
});

test('replaceFile refuses a changed ID or nonlocal source before a write', async () => {
  let writes = 0;
  const eagle = sdk(async operation => {
    if (operation === 'items.get') return structuredClone(row);
    writes++;
  });
  const item = await eagle.item.getById(row.id);
  for (const source of ['',null,'relative.png','https://example.invalid/image.png','C:/image\u0000.png'])
    await assert.rejects(item.replaceFile(source),{code:'invalid_arguments'});
  item.id = '1';
  await assert.rejects(item.replaceFile('C:/outputs/enlarged.png'),{code:'unsupported_write'});
  assert.equal(writes,0);
});
