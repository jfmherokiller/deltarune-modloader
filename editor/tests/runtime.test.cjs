'use strict';
// SYNTHETIC TEST FIXTURES ONLY: no game is started, contacted, or modified.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { serve, WINDOWS, PIPES } = require('./transport-fixture.cjs');
const path = require('node:path');
const fs = require('node:fs');
const runtimePath = path.join(__dirname, '../lib/runtime.cjs');
const sample = (overrides = {}) => ({ protocol: 1, pid: process.pid, room: 'SYNTHETIC_TEST_ROOM',
  events: 123, sampledAt: Date.now(), ageMs: 0, runnerType: 'unknown',
  capabilities: { inspect: true, liveCode: false, liveAssets: false },
  testFixture: 'synthetic-not-game-telemetry', ...overrides });
const fixture = (handler, body) => serve('snapshot', process.pid, handler, body);
test('runtime transport exists and reads newline JSON from a real local pipe (SYNTHETIC)', async () => {
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
test('rejects invalid schema, PID mismatch, stale or malformed telemetry (SYNTHETIC)', async t => {
  const runtime = require(runtimePath);
  const invalid = [
    ['wrong protocol', sample({ protocol: 2 })],
    ['wrong pid', sample({ pid: process.pid + 1 })],
    ['unsafe capability', sample({ capabilities: { inspect: true, liveCode: true, liveAssets: false } })],
    ['wrong room', sample({ room: {} })],
    ['invalid events', sample({ events: -1 })],
    ['unknown runner type', sample({ runnerType: 'interpreter' })],
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
test('bounds incomplete, stalled, oversized and invalid UTF-8 frames (SYNTHETIC)', async t => {
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
test('discovers exact per-PID pipe names without sending commands (SYNTHETIC)', { skip: !WINDOWS }, async () => {
  const runtime = require(runtimePath);
  assert.equal(typeof runtime.discover, 'function');
  let connects = 0;
  await fixture(() => { connects++; }, async () => {
    const entries = await runtime.discover();
    assert.ok(entries.some(entry => entry.pid === process.pid && entry.pipe === PIPES.snapshot(process.pid)));
    assert.ok(entries.every(entry => Number.isInteger(entry.pid) && entry.pipe === `\\\\.\\pipe\\RunnerEditor-${entry.pid}`));
    assert.equal(connects, 0);
  });
});
test('handles fragmented UTF-8 without game-specific globals (SYNTHETIC)', async () => {
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
const nativeTests = path.join(__dirname, '../runtime/build/native-tests.exe');
// Linux: run the production C++ transport under wine; it then serves through TcpRelay.
const wine = !WINDOWS && process.env.WINE !== '' && require('node:child_process').spawnSync(process.env.WINE || 'wine', ['--version']).status === 0;
test('production native transport interoperates with Node (SYNTHETIC, no game)', { skip: !fs.existsSync(nativeTests) || (!WINDOWS && !wine) }, async () => {
  const { spawn } = require('node:child_process');
  const { once } = require('node:events');
  const runtime = require(runtimePath);
  const child = WINDOWS ? spawn(nativeTests, ['--serve-synthetic'], { stdio: ['pipe', 'pipe', 'pipe'] })
    : spawn(process.env.WINE || 'wine', [nativeTests, '--serve-synthetic'], { stdio: ['pipe', 'pipe', 'pipe'], env: { ...process.env, WINEDEBUG: '-all' } });
  let output = '';
  let pid = child.pid;
  const exited = once(child, 'exit');
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Synthetic native fixture did not become ready')), WINDOWS ? 3000 : 20000);
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.once('exit', () => { clearTimeout(timer); reject(new Error('Synthetic fixture exited before ready: ' + output)); });
      child.stdout.on('data', chunk => {
        output += chunk;
        const ready = /SYNTHETIC_READY pid=(\d+) relay=(\d)/.exec(output);
        if (ready) { clearTimeout(timer); pid = Number(ready[1]); resolve(); }
      });
    });
    if (!WINDOWS) assert.match(output, /relay=1/, 'TcpRelay did not start under wine');
    assert.ok((await runtime.discover()).some(entry => entry.pid === pid));
    for (let i = 0; i < 5; i++) {
      const result = await runtime.snapshot(pid);
      assert.match(result.room, /^SYNTHETIC_NATIVE_TEST/);
      assert.equal(result.runnerType, 'unknown');
      assert.equal(result.chapter, undefined);
      await new Promise(resolve => setTimeout(resolve, 25));
    }
  } finally { child.stdin.end('stop\n'); }
  const [code] = await exited;
  assert.equal(code, 0);
});
test('counts transport time against monotonic sample freshness (SYNTHETIC)', async () => {
  const runtime = require(runtimePath);
  await fixture(socket => {
    const frame = JSON.stringify(sample({ ageMs: 2990, sampledAt: undefined })) + '\n';
    setTimeout(() => { if (!socket.destroyed) socket.end(frame); }, 60);
  }, async () => { await assert.rejects(runtime.snapshot(process.pid), /stale/i); });
});
test('Wine relay registry: discovery lists live entries, prunes dead ones, rejects malformed ones (SYNTHETIC)', { skip: WINDOWS }, async () => {
  const runtime = require(runtimePath);
  const { registry } = require('./transport-fixture.cjs');
  const net = require('node:net');
  // A port nobody listens on: bind then close to obtain one.
  const dead = net.createServer();
  await new Promise(resolve => dead.listen({ host: '127.0.0.1', port: 0 }, resolve));
  const deadPort = dead.address().port;
  await new Promise(resolve => dead.close(resolve));
  const stale = path.join(registry, 'RunnerEditor-4000000001.json');
  fs.writeFileSync(stale, JSON.stringify({ protocol: 1, pid: 4000000001, port: deadPort, token: 'a'.repeat(64) }));
  const malformed = path.join(registry, 'RunnerEditor-4000000002.json');
  fs.writeFileSync(malformed, JSON.stringify({ protocol: 1, pid: 4000000002, port: deadPort, token: 'short' }));
  try {
    await fixture(socket => socket.end(JSON.stringify(sample()) + '\n'), async ({ port }) => {
      const entries = await runtime.discover();
      assert.deepEqual(entries.find(entry => entry.pid === process.pid), { pid: process.pid, endpoint: `tcp://127.0.0.1:${port}` });
      assert.ok(!entries.some(entry => entry.pid >= 4000000001));
    });
    assert.equal(fs.existsSync(stale), false, 'refused entry should be pruned');
    assert.equal(fs.existsSync(malformed), true, 'malformed entries are skipped, never trusted or deleted');
    await assert.rejects(runtime.snapshot(4000000002), /registry/i);
    await assert.rejects(runtime.snapshot(4000000003), /No runtime registered/);
  } finally { fs.rmSync(malformed, { force: true }); fs.rmSync(stale, { force: true }); }
});
test('Wine relay registry: entries naming an exited Unix process are pruned even if the port still accepts (SYNTHETIC)', { skip: process.platform !== 'linux' }, async () => {
  const runtime = require(runtimePath);
  const { registry } = require('./transport-fixture.cjs');
  const own = fs.readFileSync(`/proc/${process.pid}/stat`, 'utf8');
  const start = own.slice(own.lastIndexOf(')') + 2).split(' ')[19];
  await fixture(socket => socket.end(JSON.stringify(sample()) + '\n'), async ({ file }) => {
    const entry = JSON.parse(fs.readFileSync(file, 'utf8'));
    // Live: this test process, with its real start time.
    fs.writeFileSync(file, JSON.stringify({ ...entry, unixPid: process.pid, unixStart: start }));
    assert.ok((await runtime.discover()).some(e => e.pid === process.pid));
    assert.equal((await runtime.snapshot(process.pid)).room, 'SYNTHETIC_TEST_ROOM');
    // Same pid, different start time = pid reuse after the game exited; the port still accepts.
    const stale = path.join(registry, 'RunnerEditor-4000000004.json');
    fs.writeFileSync(stale, JSON.stringify({ ...entry, pid: 4000000004, unixPid: process.pid, unixStart: '1' }));
    assert.ok(!(await runtime.discover()).some(e => e.pid === 4000000004));
    assert.equal(fs.existsSync(stale), false);
    fs.writeFileSync(stale, JSON.stringify({ ...entry, pid: 4000000004, unixPid: process.pid, unixStart: '1' }));
    await assert.rejects(runtime.snapshot(4000000004), /exited/);
    fs.rmSync(stale, { force: true });
  });
});
test('Wine relay: wrong-token gets no data (SYNTHETIC)', { skip: WINDOWS }, async () => {
  const runtime = require(runtimePath);
  let reached = 0;
  await fixture(socket => { reached++; socket.end(JSON.stringify(sample()) + '\n'); }, async ({ file }) => {
    const entry = JSON.parse(fs.readFileSync(file, 'utf8'));
    fs.writeFileSync(file, JSON.stringify({ ...entry, token: 'f'.repeat(64) }));
    await assert.rejects(runtime.snapshot(process.pid));
  });
  assert.equal(reached, 0);
});
module.exports = { fixture, sample };