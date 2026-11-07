'use strict';
// SYNTHETIC transport fixtures shared by the runtime/inspector/UI tests; no game is contacted.
// Windows: real local named pipes with the production names.
// Linux/macOS: the Wine relay contract instead — a registry file in a temporary
// RUNNER_STUDIO_RUNTIME_DIR plus a loopback TCP listener that checks the
// "RUNNER-RELAY/1 <token> <channel>" header and hands the remaining bytes to the
// channel's server over a private Unix socket, so servers see exactly what a pipe
// server would. Closing a channel's server makes the relay refuse that channel.
const net = require('node:net');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { randomBytes } = require('node:crypto');

const WINDOWS = process.platform === 'win32';
let registry;
if (!WINDOWS) {
  registry = fs.mkdtempSync(path.join(process.env.TMPDIR || os.tmpdir(), 'runner-registry-'));
  process.env.RUNNER_STUDIO_RUNTIME_DIR = registry;
  process.on('exit', () => fs.rmSync(registry, { recursive: true, force: true }));
}
const PIPES = {
  snapshot: pid => `\\\\.\\pipe\\RunnerEditor-${pid}`,
  inspect: pid => `\\\\.\\pipe\\RunnerInspector-${pid}`,
};
const listen = (server, target) => new Promise((resolve, reject) => server.once('error', reject).listen(target, resolve));
const close = server => new Promise(resolve => server.listening ? server.close(resolve) : resolve());

const relays = new Map(); // pid -> relay state
async function relayFor(pid) {
  if (relays.has(pid)) return relays.get(pid);
  const state = { token: randomBytes(32).toString('hex'), routes: new Map(), sockets: new Set(), connections: 0,
    file: path.join(registry, `RunnerEditor-${pid}.json`) };
  state.server = net.createServer(client => {
    state.connections++;
    state.sockets.add(client); client.on('close', () => state.sockets.delete(client)); client.on('error', () => {});
    let head = Buffer.alloc(0);
    const onData = chunk => {
      head = Buffer.concat([head, chunk]);
      const newline = head.indexOf(10);
      if (newline < 0) { if (head.length > 256) client.destroy(); return; }
      client.off('data', onData);
      const match = /^RUNNER-RELAY\/1 ([0-9a-f]{64}) (snapshot|inspect)$/.exec(head.subarray(0, newline).toString());
      const route = match && match[1] === state.token && state.routes.get(match[2]);
      if (!route) return client.destroy();
      const upstream = net.createConnection(route);
      state.sockets.add(upstream); upstream.on('close', () => state.sockets.delete(upstream));
      upstream.on('error', () => client.destroy());
      client.once('close', () => upstream.destroy());
      upstream.once('connect', () => {
        const rest = head.subarray(newline + 1);
        if (rest.length) upstream.write(rest);
        client.pipe(upstream); upstream.pipe(client);
      });
    };
    client.on('data', onData);
  });
  relays.set(pid, state);
  await listen(state.server, { host: '127.0.0.1', port: 0 });
  state.port = state.server.address().port;
  fs.writeFileSync(state.file, JSON.stringify({ protocol: 1, pid, port: state.port, token: state.token }));
  return state;
}
async function releaseRoute(pid, channel) {
  const state = relays.get(pid);
  if (!state) return;
  state.routes.delete(channel);
  if (state.routes.size) return;
  relays.delete(pid);
  fs.rmSync(state.file, { force: true });
  for (const socket of state.sockets) socket.destroy();
  await close(state.server);
}

// Listen `server` as the given channel of `pid`. Resolves to relay details on Linux.
async function listenChannel(server, channel, pid) {
  if (WINDOWS) { await listen(server, PIPES[channel](pid)); return {}; }
  const socketPath = path.join(registry, `${channel}-${pid}-${randomBytes(4).toString('hex')}.sock`);
  await listen(server, socketPath);
  const state = await relayFor(pid);
  state.routes.set(channel, socketPath);
  server.once('close', () => { releaseRoute(pid, channel); });
  return { token: state.token, port: state.port, file: state.file, relay: state };
}

async function serve(channel, pid, handler, body) {
  const sockets = new Set();
  const server = net.createServer(socket => {
    sockets.add(socket); socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
    handler(socket);
  });
  const details = await listenChannel(server, channel, pid);
  try { return await body(details); }
  finally {
    for (const socket of sockets) socket.destroy();
    await close(server);
    await releaseRoute(pid, channel);
  }
}
module.exports = { serve, listenChannel, registry, WINDOWS, PIPES };
