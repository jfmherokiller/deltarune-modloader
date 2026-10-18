'use strict';
// SYNTHETIC pipe fixtures; these tests never open a game.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const runtime = require('../lib/runtime.cjs');
const pipe = `\\\\.\\pipe\\RunnerInspector-${process.pid}`;
const page = (overrides = {}) => ({ protocol:2, pid:process.pid, kind:'globals', target:null,
  offset:0, items:[{name:'counter',type:'int64',value:'9223372036854775807',source:'dynamic',readOnly:true}],
  hasMore:false, sampledAt:Date.now(), ageMs:0, room:'SYNTHETIC_INSPECTOR_ROOM', error:null, ...overrides });
async function fixture(handler, body) {
  const sockets = new Set();
  const server = net.createServer(socket => {
    sockets.add(socket); socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
    handler(socket);
  });
  await new Promise((resolve,reject) => server.once('error',reject).listen(pipe,resolve));
  try { await body(); }
  finally { for (const socket of sockets) socket.destroy(); await new Promise(resolve => server.close(resolve)); }
}
test('inspector requests a globals page and preserves int64 display strings (SYNTHETIC)', async () => {
  assert.equal(typeof runtime.inspect, 'function', 'read-only inspector client is missing');
  let request;
  await fixture(socket => socket.once('data', bytes => {
    request = bytes.toString(); socket.end(JSON.stringify(page()) + '\n');
  }), async () => {
    const result = await runtime.inspect(process.pid,{kind:'globals'});
    assert.equal(result.items[0].value,'9223372036854775807');
    assert.equal(result.items[0].readOnly,true);
  });
  assert.equal(request,'globals 0\n');
});
test('only enumerated read requests with bounded numeric selectors reach the pipe', async () => {
  let connections = 0;
  await fixture(socket => { connections++; socket.end(); }, async () => {
    for (const query of [null, {}, [], {kind:'eval'}, {kind:'globals\nwrite'}, {kind:'globals',offset:-1},
      {kind:'globals',offset:0.5}, {kind:'globals',offset:1000001}, {kind:'globals',offset:'0'},
      {kind:'instance'}, {kind:'instance',instanceId:-1}, {kind:'instance',instanceId:2147483648},
      {kind:'instance',instanceId:'100001'}, {kind:'globals',instanceId:100001}, {kind:'globals',value:42}]) {
      await assert.rejects(runtime.inspect(process.pid,query), /query|kind|offset|instance|field/i);
    }
    for (const pid of [0,-1,'123',4294967296]) await assert.rejects(runtime.inspect(pid,{kind:'globals'}), /PID/);
  });
  assert.equal(connections,0);
});
test('instance and enumeration page selectors are encoded without arbitrary commands', async () => {
  for (const [query, expected] of [[{kind:'instances',offset:64},'instances 64\n'],
    [{kind:'instance',instanceId:100001,offset:12},'instance 100001 12\n']]) {
    let request;
    await fixture(socket => socket.once('data', data => {
      request=data.toString(); socket.end(JSON.stringify(page({kind:query.kind,target:query.instanceId ?? null,
        offset:query.offset,items:[]}))+'\n');
    }), async () => { await runtime.inspect(process.pid,query); });
    assert.equal(request,expected);
  }
});
test('inspector rejects mismatched, stale, mutable or malformed pages (SYNTHETIC)', async t => {
  const cases = [
    ['wrong protocol',{protocol:1}], ['wrong PID',{pid:process.pid+1}], ['wrong kind',{kind:'instance'}],
    ['wrong target',{target:100001}], ['wrong offset',{offset:64}], ['missing age',{ageMs:undefined}],
    ['stale page',{ageMs:4000}], ['missing timestamp',{sampledAt:undefined}], ['future time',{sampledAt:Date.now()+60000}],
    ['old time',{sampledAt:Date.now()-60000}], ['invalid room',{room:42}], ['oversized room',{room:'x'.repeat(513)}],
    ['missing error status',{error:undefined}], ['invalid error',{error:{message:'bad'}}],
    ['oversized error',{error:'x'.repeat(513)}], ['non-array items',{items:{}}], ['too many rows',{items:Array(65).fill(page().items[0])}],
    ['no progress',{items:[],hasMore:true}], ['non-boolean more',{hasMore:'true'}],
    ['missing readonly',{items:[{name:'a',type:'real',value:'1',source:'dynamic'}]}],
    ['numeric value',{items:[{...page().items[0],value:42}]}], ['large value',{items:[{...page().items[0],value:'x'.repeat(513)}]}],
    ['invalid source',{items:[{...page().items[0],source:'memory'}]}], ['large type',{items:[{...page().items[0],type:'x'.repeat(65)}]}],
    ['duplicate names',{items:[page().items[0],page().items[0]]}],
    ['mixed error and rows',{error:'Target destroyed'}]
  ];
  for (const [label, overrides] of cases) await t.test(label, async () => {
    await fixture(socket => socket.once('data',()=>socket.end(JSON.stringify(page(overrides))+'\n')),
      async () => { await assert.rejects(runtime.inspect(process.pid,{kind:'globals'}),/inspector|stale|timestamp/i); });
  });
});
test('inspector accepts bounded errors and typed instance lists, never treats errors as empty success', async () => {
  await fixture(socket=>socket.once('data',()=>socket.end(JSON.stringify(page({error:'Target destroyed',items:[]}))+'\n')),
    async()=>assert.equal((await runtime.inspect(process.pid,{kind:'globals'})).error,'Target destroyed'));
  const instances = page({kind:'instances',items:[{id:100001,object:'obj_example'}]});
  await fixture(socket=>socket.once('data',()=>socket.end(JSON.stringify(instances)+'\n')),
    async()=>assert.equal((await runtime.inspect(process.pid,{kind:'instances'})).items[0].id,100001));
  for (const items of [[{id:'100001',object:'obj_example'}],[{id:-1,object:'obj_example'}],
    [{id:100001,object:'a'.repeat(513)}],[{id:100001,object:'a'},{id:100001,object:'b'}]]) {
    await fixture(socket=>socket.once('data',()=>socket.end(JSON.stringify({...instances,items})+'\n')),
      async()=>assert.rejects(runtime.inspect(process.pid,{kind:'instances'}),/inspector/i));
  }
});
test('inspector bounds stalled, oversized, truncated and invalid UTF-8 frames (SYNTHETIC)', async t => {
  for (const [name, handler] of [
    ['stalled', () => {}], ['oversized', socket => socket.write('x'.repeat(65537))],
    ['truncated', socket => socket.end('{"protocol":2}')],
    ['invalid UTF-8', socket => socket.end(Buffer.from('{"name":"\xff"}\n','latin1'))]
  ]) await t.test(name, async () => {
    await fixture(socket=>socket.once('data',()=>handler(socket)),async()=>{
      await assert.rejects(runtime.inspect(process.pid,{kind:'globals'}));
    });
  });
});
test('inspector accepts split UTF-8 and counts transport time against freshness', async () => {
  const value=page({items:[{name:'雪',type:'string',value:'<img src=x onerror=alert(1)>',source:'dynamic',readOnly:true}]});
  await fixture(socket=>socket.once('data',()=>{
    const bytes=Buffer.from(JSON.stringify(value)+'\n'), split=bytes.indexOf(Buffer.from('雪'))+1;
    socket.write(bytes.subarray(0,split)); setTimeout(()=>{if(!socket.destroyed)socket.end(bytes.subarray(split));},15);
  }),async()=>assert.equal((await runtime.inspect(process.pid,{kind:'globals'})).items[0].name,'雪'));
  await fixture(socket=>socket.once('data',()=>{
    const text=JSON.stringify(page({ageMs:2990}))+'\n';
    setTimeout(()=>{if(!socket.destroyed)socket.end(text);},60);
  }),async()=>assert.rejects(runtime.inspect(process.pid,{kind:'globals'}),/stale/));
});

