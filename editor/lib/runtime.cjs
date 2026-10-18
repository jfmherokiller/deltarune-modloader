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
async function snapshot(pid) {
  return readFrame(pid, pipeFor(pid), MAX_MESSAGE_BYTES, value => validate(value, pid));
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
  return readFrame(pid, `\\\\.\\pipe\\RunnerInspector-${pid}`, 65536,
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
async function readFrame(pid, pipe, maxBytes, validateFrame, request) {
  if (!validPid(pid)) throw new TypeError('PID must be an integer between 1 and 4294967295');
  if (process.platform !== 'win32') throw new Error('Runtime pipes require Windows');
  return new Promise((resolve, reject) => {
    const started = performance.now();
    const socket = net.createConnection(pipe);
    if (request !== undefined) socket.once('connect', () => socket.write(request));
    let data = Buffer.alloc(0);
    let settled = false;
    const timer = setTimeout(() => finish(new Error('Runtime snapshot timed out')), TIMEOUT_MS);
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
async function discover() {
  if (process.platform !== 'win32') return [];
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
module.exports = { discover, snapshot, inspect };
