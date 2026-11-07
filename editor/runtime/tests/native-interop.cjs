'use strict';
// SYNTHETIC interop against production C++ transport and the editor's Node client.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const path = require('node:path');
// Linux: the fixture runs under wine and is reached through TcpRelay; the registry
// goes to a private temp directory and the Windows PID comes from the ready line.
if (process.platform !== 'win32' && !process.env.RUNNER_STUDIO_RUNTIME_DIR) {
  const fs = require('node:fs');
  const dir = fs.mkdtempSync(path.join(process.env.TMPDIR || require('node:os').tmpdir(), 'runner-registry-'));
  process.env.RUNNER_STUDIO_RUNTIME_DIR = dir;
  process.on('exit', () => fs.rmSync(dir, { recursive: true, force: true }));
}
const runtime = require('../../lib/runtime.cjs');

test('native inspector fixture interoperates with strict Node client', { timeout: 15000 }, async () => {
  const exe = path.join(__dirname, '../build/inspector-transport-tests.exe');
  const child = process.platform === 'win32' ? spawn(exe, ['--serve-synthetic'], { stdio: ['pipe', 'pipe', 'pipe'] })
    : spawn(process.env.WINE || 'wine', [exe, '--serve-synthetic'], { stdio: ['pipe', 'pipe', 'pipe'], env: { ...process.env, WINEDEBUG: '-all' } });
  let pid = child.pid;
  const exited = new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('exit', (code, signal) => resolve({code, signal}));
  });
  try {
    await new Promise((resolve, reject) => {
      let output = '';
      const timer = setTimeout(() => reject(new Error('Native fixture did not become ready')), process.platform === 'win32' ? 3000 : 20000);
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.stdout.on('data', data => {
        output += data;
        const ready = /SYNTHETIC_INSPECTOR_READY pid=(\d+) relay=(\d)/.exec(output);
        if (ready) {
          clearTimeout(timer); pid = Number(ready[1]);
          if (process.platform !== 'win32' && ready[2] !== '1') reject(new Error('TcpRelay did not start under wine'));
          else resolve();
        }
      });
    });
    for (let n = 0; n < 10; ++n) {
      const globals = await runtime.inspect(pid, {kind: 'globals', offset: 0});
      assert.equal(globals.room, 'SYNTHETIC_INSPECTOR_TEST');
      assert.equal(globals.items[0].value, '9223372036854775807');
      assert.equal(globals.items[0].source, 'dynamic');
      const instances = await runtime.inspect(pid, {kind: 'instances', offset: 0});
      assert.deepEqual(instances.items, [{id: 100001, object: 'obj_SYNTHETIC'}]);
      const instance = await runtime.inspect(pid, {kind: 'instance', instanceId: 100001, offset: 0});
      assert.equal(instance.items[0].source, 'builtin');
      const end = await runtime.inspect(pid, {kind: 'globals', offset: 64});
      assert.deepEqual(end.items, []);
      assert.equal(end.hasMore, false);
    }
  } finally {
    child.stdin.end('stop\n');
  }
  assert.deepEqual(await exited, {code: 0, signal: null});
});
