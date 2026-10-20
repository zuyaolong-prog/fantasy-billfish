'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {createEagle} = require('../eagle-sdk.cjs');

const createdId = '9223372036854775806';
const confirmed = () => ({item_id:createdId,item_ids:[createdId],item:{id:createdId,name:'image.png'},
  completion:'native_import_persisted',analysis_ready:false});
function sdk(call) { return createEagle({call,local:async () => {},plugin:{},app:{},register:() => {},progress:() => {}}).eagle; }

test('Pinterest URL import forwards name and website and waits for persisted native ID',async () => {
  const calls = []; let resolve;
  const eagle = sdk((operation,args) => { calls.push({operation,args}); return new Promise(done => { resolve = done; }); });
  let returned = false;
  const promise = eagle.item.addFromURL('https://example.invalid/image.png?case=%2f',{name:'Pinterest image',website:'https://www.pinterest.com/pin/1/'}).then(value => { returned = true; return value; });
  await Promise.resolve(); assert.equal(returned,false);
  assert.deepEqual(calls,[{operation:'items.import_url',args:{url:'https://example.invalid/image.png?case=%2f',name:'Pinterest image',origin:'https://www.pinterest.com/pin/1/'}}]);
  resolve(confirmed());
  assert.equal(await promise,createdId);
});

test('missing or empty Pinterest names use host basename fallback without inventing a name',async () => {
  const calls = [],eagle = sdk(async (operation,args) => { calls.push({operation,args}); return confirmed(); });
  for (const name of [undefined,'','  ']) assert.equal(await eagle.item.addFromURL('https://example.invalid/original.png',{name,website:''}),createdId);
  for (const call of calls) assert.equal(Object.hasOwn(call.args,'name'),false);
});

test('Eagle display titles become safe Windows stems without guessing a URL extension',async () => {
  const calls = [],eagle = sdk(async (operation,args) => { calls.push({operation,args}); return confirmed(); });
  await eagle.item.addFromURL('https://example.invalid/download?format=exe',{name:'  石材: 岩层 / 参考?.v2  ... '});
  assert.equal(calls[0].args.name,'石材_ 岩层 _ 参考_.v2');
  assert.equal(calls[0].args.url,'https://example.invalid/download?format=exe');
  await eagle.item.addFromURL('https://example.invalid/source.jpg',{name:'Sketch.png'});
  assert.equal(calls[1].args.name,'Sketch.png');
  await eagle.item.addFromURL('https://example.invalid/source.jpg',{name:'中文 name.v2#100% ♥'});
  assert.equal(calls[2].args.name,'中文 name.v2#100% ♥');
});

test('display titles cannot become paths, Windows devices or overlong broken Unicode names',async () => {
  const calls = [],eagle = sdk(async (operation,args) => { calls.push({operation,args}); return confirmed(); });
  const cases = [
    ['CON','_CON'],['nul.jpeg','_nul.jpeg'],['COM1 .draft','_COM1 .draft'],
    ['../../collection','.._.._collection'],['C:\\Windows\\pictures:master','C__Windows_pictures_master'],
    ['a\u0000b','a_b'],['...','_'],['CON' + ' '.repeat(237) + '尾部','_CON'],
    ['x' + '😀'.repeat(120),'x' + '😀'.repeat(119)]
  ];
  for (const [name,expected] of cases) {
    await eagle.item.addFromURL('https://example.invalid/real.jpg',{name});
    const result = calls.at(-1).args.name;
    assert.equal(result,expected);
    assert.ok(result.length <= 240);
    assert.doesNotMatch(result,/[\u0000-\u001f<>:"/\\|?*]/);
    assert.doesNotMatch(result,/[\uD800-\uDBFF]$/);
  }
});

test('path import delegates only the absolute source and optional single native folder',async () => {
  const calls = [],eagle = sdk(async (operation,args) => { calls.push({operation,args}); return confirmed(); });
  assert.equal(await eagle.item.addFromPath('C:\\fixture\\中文 #%.png',{folders:['123'],name:'asset'}),createdId);
  assert.deepEqual(calls,[{operation:'items.import_path',args:{source_path:'C:\\fixture\\中文 #%.png',folder_id:'123',name:'asset'}}]);
  assert.equal(await eagle.item.addFromPath('C:\\fixture\\source.png',{folders:[]}),createdId);
  assert.equal(Object.hasOwn(calls[1].args,'folder_id'),false);
});

test('unsupported options and sources are rejected before any import is submitted',async () => {
  let submitted = 0;
  const eagle = sdk(async () => { submitted++; return confirmed(); });
  for (const options of [{annotation:'unsupported'},{tags:['tag']},{unknown:true},{folders:['1','2']}])
    await assert.rejects(eagle.item.addFromURL('https://example.invalid/image.png',options),{code:'unsupported_import_option'});
  await assert.rejects(eagle.item.addFromURL('data:image/png;base64,AAAA',{}),{code:'unsupported_import_source'});
  await assert.rejects(eagle.item.addFromPath('relative.png',{}),{code:'invalid_arguments'});
  assert.equal(submitted,0);
});

test('accepted, numeric or incomplete import responses never become a successful Item ID',async () => {
  await assert.rejects(sdk(async () => ({...confirmed(),completion:'accepted'})).item.addFromURL('https://example.invalid/a.png'),error => error.code === 'import_not_confirmed' && error.details.item_id === createdId);
  await assert.rejects(sdk(async () => ({...confirmed(),item_id:40})).item.addFromURL('https://example.invalid/a.png'),{code:'invalid_host_response'});
  await assert.rejects(sdk(async () => ({...confirmed(),item:{id:'1'}})).item.addFromURL('https://example.invalid/a.png'),error => error.code === 'invalid_host_response' && error.details.item_id === createdId);
});

test('native failure preserves an already imported real ID without retry or fake rollback',async () => {
  const error = Object.assign(new Error('Native import persisted but source metadata failed'),{code:'metadata_failed',details:{item_id:createdId,completion:'native_import_persisted',rollback_performed:false}});
  let submissions = 0;
  const eagle = sdk(async () => { submissions++; throw error; });
  await assert.rejects(eagle.item.addFromURL('https://example.invalid/a.png'),actual => actual === error && actual.details.item_id === createdId && actual.details.rollback_performed === false);
  assert.equal(submissions,1);
});
