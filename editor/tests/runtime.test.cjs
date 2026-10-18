'use strict';
// SYNTHETIC TEST FIXTURES ONLY: no game is started, contacted, or modified.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const path = require('node:path');
const fs = require('node:fs');
const runtimePath = path.join(__dirname, '../lib/runtime.cjs');
const pipe = `\\\\.\\pipe\\RunnerEditor-${process.pid}`;
const sample = (overrides = {}) => ({ protocol: 1, pid: process.pid, room: 'SYNTHETIC_TEST_ROOM',
  events: 123, sampledAt: Date.now(), ageMs: 0, runnerType: 'unknown',
  capabilities: { inspect: true, liveCode: false, liveAssets: false },
  testFixture: 'synthetic-not-game-telemetry', ...overrides });
async function fixture(handler, body) {
  const sockets = new Set();
  const server = net.createServer(socket => {
    sockets.add(socket);
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
    handler(socket);
  });
  await new Promise((resolve, reject) => server.once('error', reject).listen(pipe, resolve));
  try { await body(); }
  finally {
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => server.close(resolve));
  }
}
test('runtime transport exists and reads newline JSON from a real local pipe (SYNTHETIC)', { skip: process.platform !== 'win32' }, async () => {
  assert.ok(fs.existsSync(runtimePath), 'runtime client implementation is missing');
  const runtime = require(runtimePath);
  let receivedBytes = 0;
  await fixture(socket => { socket.on('data', chunk => { receivedBytes += chunk.length; }); socket.write(JSON.stringify(sample()) + '\n'); }, async () => {
    const value = await runtime.snapshot(process.pid);
    assert.equal(value.room, 'SYNTHETIC_TEST_ROOM');
    assert.deepEqual(value.capabilities, { inspect: true, liveCode: false, liveAssets: false });
    assert.equal(receivedBytes, 0, 'client must never send a command');
  });
});
test('rejects invalid schema, PID mismatch, stale or malformed telemetry (SYNTHETIC)', { skip: process.platform !== 'win32' }, async t => {
  const runtime = require(runtimePath);
  const invalid = [
    ['wrong protocol', sample({ protocol: 2 })],
    ['wrong pid', sample({ pid: process.pid + 1 })],
    ['unsafe capability', sample({ capabilities: { inspect: true, liveCode: true, liveAssets: false } })],
    ['wrong room', sample({ room: {} })],
    ['invalid events', sample({ events: -1 })],
    ['stale monotonic age', sample({ ageMs: 5000 })],
    ['missing age', sample({ ageMs: undefined })],
    ['stale wall timestamp', sample({ sampledAt: Date.now() - 60000 })],
    ['future timestamp', sample({ sampledAt: Date.now() + 60000 })],
    ['null document', null],
    ['array document', []],
  ];
  for (const [name, value] of invalid) await t.test(name, async () => {
    await fixture(socket => socket.end(JSON.stringify(value) + '\n'), async () => {
      await assert.rejects(runtime.snapshot(process.pid), /protocol|snapshot|stale|timestamp|capabilit|age/i);
    });
  });
  await t.test('malformed JSON', async () => {
    await fixture(socket => socket.end('{bad}\n'), async () => { await assert.rejects(runtime.snapshot(process.pid)); });
  });
});
test('bounds incomplete, stalled, oversized and invalid UTF-8 frames (SYNTHETIC)', { skip: process.platform !== 'win32' }, async t => {
  const runtime = require(runtimePath);
  const cases = [
    ['stalled', () => {}],
    ['truncated', socket => socket.end('{"protocol":1}')],
    ['oversized', socket => socket.write('x'.repeat(5000))],
    ['invalid UTF-8', socket => socket.end(Buffer.from(JSON.stringify(sample()).replace('SYNTHETIC_TEST_ROOM', '\xff') + '\n', 'latin1'))],
  ];
  for (const [name, handler] of cases) await t.test(name, async () => {
    await fixture(handler, async () => {
      const result = await Promise.race([
        runtime.snapshot(process.pid).then(() => 'resolved', () => 'rejected'),
        new Promise(resolve => { const timer = setTimeout(() => resolve('hung'), 2200); timer.unref(); }),
      ]);
      assert.equal(result, 'rejected');
    });
  });
});
test('rejects nonnumeric and out-of-range PID before opening any pipe', async () => {
  const runtime = require(runtimePath);
  for (const pid of [0, -1, 1.5, '123', '../remote', NaN, 4294967296]) {
    await assert.rejects(runtime.snapshot(pid), /PID/i);
  }
});
test('discovers exact per-PID pipe names without sending commands (SYNTHETIC)', { skip: process.platform !== 'win32' }, async () => {
  const runtime = require(runtimePath);
  assert.equal(typeof runtime.discover, 'function');
  let connects = 0;
  await fixture(() => { connects++; }, async () => {
    const entries = await runtime.discover();
    assert.ok(entries.some(entry => entry.pid === process.pid && entry.pipe === pipe));
    assert.ok(entries.every(entry => Number.isInteger(entry.pid) && entry.pipe === `\\\\.\\pipe\\RunnerEditor-${entry.pid}`));
    assert.equal(connects, 0);
  });
});
test('handles fragmented UTF-8 without game-specific globals (SYNTHETIC)', { skip: process.platform !== 'win32' }, async () => {
  const runtime = require(runtimePath);
  await fixture(socket => {
    const bytes = Buffer.from(JSON.stringify(sample({ room: 'SYNTHETIC_雪', sampledAt: undefined })) + '\n');
    const split = bytes.indexOf(Buffer.from('雪')) + 1;
    socket.write(bytes.subarray(0, split));
    setTimeout(() => { if (!socket.destroyed) socket.end(bytes.subarray(split)); }, 15);
  }, async () => {
    const value = await runtime.snapshot(process.pid);
    assert.equal(value.room, 'SYNTHETIC_雪');
    assert.equal(value.gold, undefined);
    assert.equal(value.chapter, undefined);
  });
});
test('production native transport interoperates with Node (SYNTHETIC, no game)', { skip: process.platform !== 'win32' || !fs.existsSync(path.join(__dirname, '../runtime/build/native-tests.exe')) }, async () => {
  const { spawn } = require('node:child_process');
  const { once } = require('node:events');
  const runtime = require(runtimePath);
  const child = spawn(path.join(__dirname, '../runtime/build/native-tests.exe'), ['--serve-synthetic'], { stdio: ['pipe', 'pipe', 'pipe'] });
  let output = '';
  const exited = once(child, 'exit');
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Synthetic native fixture did not become ready')), 3000);
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.once('exit', () => { clearTimeout(timer); reject(new Error('Synthetic fixture exited before ready: ' + output)); });
      child.stdout.on('data', chunk => { output += chunk; if (output.includes('SYNTHETIC_READY')) { clearTimeout(timer); resolve(); } });
    });
    assert.ok((await runtime.discover()).some(entry => entry.pid === child.pid));
    for (let i = 0; i < 5; i++) {
      const result = await runtime.snapshot(child.pid);
      assert.match(result.room, /^SYNTHETIC_NATIVE_TEST/);
      assert.equal(result.runnerType, 'unknown');
      assert.equal(result.chapter, undefined);
      await new Promise(resolve => setTimeout(resolve, 25));
    }
  } finally { child.stdin.end('stop\n'); }
  const [code] = await exited;
  assert.equal(code, 0);
});
test('counts transport time against monotonic sample freshness (SYNTHETIC)', { skip: process.platform !== 'win32' }, async () => {
  const runtime = require(runtimePath);
  await fixture(socket => {
    const frame = JSON.stringify(sample({ ageMs: 2990, sampledAt: undefined })) + '\n';
    setTimeout(() => { if (!socket.destroyed) socket.end(frame); }, 60);
  }, async () => { await assert.rejects(runtime.snapshot(process.pid), /stale/i); });
});
module.exports = { fixture, sample };
