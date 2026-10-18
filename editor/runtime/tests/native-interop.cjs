'use strict';
// SYNTHETIC interop against production C++ transport and the editor's Node client.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const path = require('node:path');
const runtime = require('../../lib/runtime.cjs');

test('native inspector fixture interoperates with strict Node client', { timeout: 15000 }, async () => {
  const child = spawn(path.join(__dirname, '../build/inspector-transport-tests.exe'), ['--serve-synthetic'], { stdio: ['pipe', 'pipe', 'pipe'] });
  const exited = new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('exit', (code, signal) => resolve({code, signal}));
  });
  try {
    await new Promise((resolve, reject) => {
      let output = '';
      const timer = setTimeout(() => reject(new Error('Native fixture did not become ready')), 3000);
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.stdout.on('data', data => {
        output += data;
        if (output.includes('SYNTHETIC_INSPECTOR_READY')) { clearTimeout(timer); resolve(); }
      });
    });
    for (let n = 0; n < 10; ++n) {
      const globals = await runtime.inspect(child.pid, {kind: 'globals', offset: 0});
      assert.equal(globals.room, 'SYNTHETIC_INSPECTOR_TEST');
      assert.equal(globals.items[0].value, '9223372036854775807');
      assert.equal(globals.items[0].source, 'dynamic');
      const instances = await runtime.inspect(child.pid, {kind: 'instances', offset: 0});
      assert.deepEqual(instances.items, [{id: 100001, object: 'obj_SYNTHETIC'}]);
      const instance = await runtime.inspect(child.pid, {kind: 'instance', instanceId: 100001, offset: 0});
      assert.equal(instance.items[0].source, 'builtin');
      const end = await runtime.inspect(child.pid, {kind: 'globals', offset: 64});
      assert.deepEqual(end.items, []);
      assert.equal(end.hasMore, false);
    }
  } finally {
    child.stdin.end('stop\n');
  }
  assert.deepEqual(await exited, {code: 0, signal: null});
});
