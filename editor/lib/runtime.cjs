'use strict';
const net = require('node:net');
const MAX_AGE_MS = 3000;
function validate(value, pid) {
  const integer = n => Number.isSafeInteger(n) && n >= 0;
  if (!value || Array.isArray(value) || value.protocol !== 1 || value.pid !== pid ||
      !(value.room === null || (typeof value.room === 'string' && Buffer.byteLength(value.room) <= 512)) ||
      !integer(value.events)) {
    throw new Error('Invalid runtime snapshot protocol/schema');
  }
  if (!value.capabilities || value.capabilities.inspect !== true || value.capabilities.liveCode !== false || value.capabilities.liveAssets !== false) {
    throw new Error('Invalid runtime snapshot capabilities');
  }
  if (value.runnerType !== undefined && !['vm', 'yyc', 'unknown'].includes(value.runnerType)) {
    throw new Error('Invalid runtime snapshot runnerType');
  }
  if (!integer(value.ageMs) || value.ageMs > MAX_AGE_MS) throw new Error('Runtime snapshot is stale or missing monotonic age');
  if (value.sampledAt !== undefined && (!integer(value.sampledAt) || value.sampledAt === 0 || Date.now() - value.sampledAt > MAX_AGE_MS || value.sampledAt - Date.now() > 1000)) {
    throw new Error('Runtime snapshot timestamp is missing, stale, or in the future');
  }
  return value;
}
const MAX_MESSAGE_BYTES = 4096;
const TIMEOUT_MS = 1200;
function validPid(pid) { return Number.isInteger(pid) && pid > 0 && pid <= 0xffffffff; }
function pipeFor(pid) { return `\\\\.\\pipe\\RunnerEditor-${pid}`; }
const CHANNEL_PIPES = { snapshot: pipeFor, inspect: pid => `\\\\.\\pipe\\RunnerInspector-${pid}` };
// Linux/macOS: the game runs under Wine/Proton, whose named pipes are invisible to native
// processes. RunnerEditor.dll then also listens on 127.0.0.1 and publishes
// RunnerEditor-<pid>.json {protocol, pid, port, token} in this directory (see runtime/README.md).
function registryDir() {
  const os = require('node:os');
  const path = require('node:path');
  if (process.env.RUNNER_STUDIO_RUNTIME_DIR) return process.env.RUNNER_STUDIO_RUNTIME_DIR;
  // Same order as TcpRelay.cpp. Not XDG_RUNTIME_DIR: Proton's container has a private one.
  if (process.env.XDG_CACHE_HOME) return path.join(process.env.XDG_CACHE_HOME, 'runner-studio');
  return path.join(os.homedir(), '.cache', 'runner-studio');
}
const REGISTRY_NAME = /^RunnerEditor-([1-9][0-9]{0,9})\.json$/;
async function registryEntry(pid) {
  const fs = require('node:fs/promises');
  const path = require('node:path');
  const file = path.join(registryDir(), `RunnerEditor-${pid}.json`);
  let value;
  try {
    const handle = await fs.open(file, 'r');
    try {
      const stat = await handle.stat();
      if (!stat.isFile() || stat.size > 512) throw new Error('Runtime registry entry is invalid');
      value = JSON.parse(await handle.readFile('utf8'));
    } finally { await handle.close(); }
  } catch (error) {
    if (error.code === 'ENOENT') throw Object.assign(new Error(`No runtime registered for PID ${pid}`), { code: 'ENOENT' });
    throw error;
  }
  if (!value || typeof value !== 'object' || Array.isArray(value) || value.protocol !== 1 || value.pid !== pid ||
      !Number.isInteger(value.port) || value.port < 1 || value.port > 65535 ||
      typeof value.token !== 'string' || !/^[0-9a-f]{64}$/.test(value.token) ||
      (value.unixPid !== undefined && (!Number.isInteger(value.unixPid) || value.unixPid < 1)) ||
      (value.unixStart !== undefined && (typeof value.unixStart !== 'string' || !/^[0-9]{1,20}$/.test(value.unixStart))))
    throw new Error('Runtime registry entry is invalid');
  return { file, port: value.port, token: value.token, unixPid: value.unixPid, unixStart: value.unixStart };
}
// True/false when the entry names its Unix process (pid + start time), null when it does not.
async function processAlive(entry) {
  if (entry.unixPid === undefined || process.platform !== 'linux') return null;
  try {
    const stat = await require('node:fs/promises').readFile(`/proc/${entry.unixPid}/stat`, 'utf8');
    const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ');
    return entry.unixStart === undefined || fields[19] === entry.unixStart; // field 22 overall
  } catch { return false; }
}
async function snapshot(pid) {
  return readFrame(pid, 'snapshot', MAX_MESSAGE_BYTES, value => validate(value, pid));
}
async function inspect(pid, query) {
  if (!query || Array.isArray(query) || typeof query !== 'object' ||
      !['globals', 'instances', 'instance'].includes(query.kind)) throw new TypeError('Invalid inspector query kind');
  const allowed = query.kind === 'instance' ? ['kind', 'offset', 'instanceId'] : ['kind', 'offset'];
  if (Object.keys(query).some(key => !allowed.includes(key))) throw new TypeError('Unknown inspector query field');
  const offset = query.offset === undefined ? 0 : query.offset;
  if (!Number.isInteger(offset) || offset < 0 || offset > 1000000) throw new TypeError('Invalid inspector offset');
  if (query.kind === 'instance' && (!Number.isInteger(query.instanceId) || query.instanceId < 0 || query.instanceId > 2147483647))
    throw new TypeError('Invalid inspector instance ID');
  const command = query.kind === 'instance' ? `instance ${query.instanceId}` : query.kind;
  return readFrame(pid, 'inspect', 65536,
    value => validateInspector(value, pid, query.kind, query.instanceId ?? null, offset), `${command} ${offset}\n`);
}
function validateInspector(value, pid, kind, target, offset) {
  const string = (text, max = 512) => typeof text === 'string' && Buffer.byteLength(text) <= max;
  const integer = n => Number.isSafeInteger(n) && n >= 0;
  const object = item => item && typeof item === 'object' && !Array.isArray(item);
  if (!object(value) || value.protocol !== 2 || value.pid !== pid || value.kind !== kind ||
      value.target !== target || value.offset !== offset || !(value.room === null || string(value.room)) ||
      !Array.isArray(value.items) || value.items.length > 64 || typeof value.hasMore !== 'boolean' ||
      (value.hasMore && value.items.length === 0) || !(value.error === null || string(value.error)) ||
      (value.error !== null && (value.items.length !== 0 || value.hasMore)))
    throw new Error('Invalid inspector page protocol/schema');
  if (!integer(value.ageMs) || value.ageMs > MAX_AGE_MS || !integer(value.sampledAt) ||
      value.sampledAt === 0 || Date.now() - value.sampledAt > MAX_AGE_MS || value.sampledAt - Date.now() > 1000)
    throw new Error('Inspector page timestamp is missing or stale');
  const seen = new Set();
  for (const item of value.items) {
    if (!object(item)) throw new Error('Invalid inspector row');
    const valid = kind === 'instances'
      ? integer(item.id) && item.id <= 2147483647 && string(item.object)
      : string(item.name) && string(item.type, 64) && string(item.value) && item.readOnly === true &&
        ['dynamic', 'builtin'].includes(item.source);
    const key = kind === 'instances' ? item.id : item.name;
    if (!valid || seen.has(key)) throw new Error('Invalid or duplicate inspector row');
    seen.add(key);
  }
  return value;
}
async function readFrame(pid, channel, maxBytes, validateFrame, request) {
  if (!validPid(pid)) throw new TypeError('PID must be an integer between 1 and 4294967295');
  const started = performance.now();
  let target, preamble = '';
  if (process.platform === 'win32') target = CHANNEL_PIPES[channel](pid);
  else {
    const entry = await registryEntry(pid);
    if (await processAlive(entry) === false) throw new Error(`Runtime for PID ${pid} has exited`);
    target = { host: '127.0.0.1', port: entry.port };
    preamble = `RUNNER-RELAY/1 ${entry.token} ${channel}\n`;
  }
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(target);
    if (preamble || request !== undefined) socket.once('connect', () => socket.write(preamble + (request ?? '')));
    let data = Buffer.alloc(0);
    let settled = false;
    const timer = setTimeout(() => finish(new Error('Runtime snapshot timed out')),
      Math.max(0, TIMEOUT_MS - (performance.now() - started)));
    function finish(error, value) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      socket.destroy();
      if (error) reject(error); else resolve(value);
    }
    socket.on('error', error => finish(error));
    socket.on('end', () => finish(new Error('Runtime snapshot ended without a newline')));
    socket.on('close', () => finish(new Error('Runtime snapshot connection closed')));
    socket.on('data', chunk => {
      if (settled) return;
      if (data.length + chunk.length > maxBytes) return finish(new Error('Runtime snapshot exceeds message size limit'));
      data = Buffer.concat([data, chunk]);
      const newline = data.indexOf(10);
      if (newline < 0) return;
      try {
        const text = new TextDecoder('utf-8', { fatal: true }).decode(data.subarray(0, newline));
        const value = validateFrame(JSON.parse(text));
        if (value.ageMs + performance.now() - started > MAX_AGE_MS) throw new Error('Runtime snapshot became stale in transit');
        finish(null, value);
      } catch (error) { finish(error); }
    });
  });
}
// Registry entries outlive a crashed game. Drop those whose port no longer accepts.
function listening(port) {
  return new Promise(resolve => {
    const socket = net.createConnection({ host: '127.0.0.1', port });
    const done = alive => { socket.destroy(); resolve(alive); };
    socket.setTimeout(300, () => done(true)); // busy, not dead
    socket.once('connect', () => done(true));
    socket.once('error', error => done(error.code !== 'ECONNREFUSED'));
  });
}
async function discoverRegistry() {
  const fs = require('node:fs/promises');
  const path = require('node:path');
  let names;
  try { names = await fs.readdir(registryDir()); }
  catch (error) { if (error.code === 'ENOENT') return []; throw error; }
  const pids = [...new Set(names.map(name => REGISTRY_NAME.exec(name)).filter(Boolean)
    .map(match => Number(match[1])).filter(validPid))].sort((a, b) => a - b).slice(0, 256);
  const found = [];
  for (const pid of pids) {
    let entry;
    try { entry = await registryEntry(pid); } catch { continue; }
    const alive = await processAlive(entry);
    if (alive === false) { await fs.rm(path.join(registryDir(), `RunnerEditor-${pid}.json`), { force: true }); continue; }
    if (alive || await listening(entry.port)) found.push({ pid, endpoint: `tcp://127.0.0.1:${entry.port}` });
    else await fs.rm(path.join(registryDir(), `RunnerEditor-${pid}.json`), { force: true });
  }
  return found;
}
async function discover() {
  if (process.platform !== 'win32') return discoverRegistry();
  const fs = require('node:fs/promises');
  let timer;
  try {
    const names = await Promise.race([
      fs.readdir('\\\\.\\pipe\\'),
      new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('Runtime discovery timed out')), TIMEOUT_MS); }),
    ]);
    return [...new Set(names.filter(name => /^RunnerEditor-[1-9][0-9]{0,9}$/.test(name))
      .map(name => Number(name.slice('RunnerEditor-'.length))).filter(validPid))]
      .sort((a, b) => a - b).slice(0, 256).map(pid => ({ pid, pipe: pipeFor(pid) }));
  } finally { clearTimeout(timer); }
}
module.exports = { discover, snapshot, inspect, registryDir };
