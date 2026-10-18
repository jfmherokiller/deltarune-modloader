'use strict';
// Electron-independent access to an existing loose-mod directory.
// Limits: UTF-8 text 4 MiB; preview media 32 MiB; literal search 200 lines.
// `chapter` is optional legacy scope metadata, never a game/root restriction.
// Security boundary: reject unsafe paths and all observed symlinks/junctions.
// Node's portable path-based open/rename APIs cannot eliminate TOCTOU against
// a hostile process concurrently replacing directories. Use OS isolation for
// that threat model; revision locking here coordinates this process only.
const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash, randomUUID } = require('node:crypto');
const { constants } = require('node:fs');
const TEXT_LIMIT = 4 * 1024 * 1024;
const MEDIA_LIMIT = 32 * 1024 * 1024;
const saveLocks = new Map();
async function exclusive(key, action) {
  const previous = saveLocks.get(key) || Promise.resolve();
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  saveLocks.set(key, gate);
  await previous;
  try { return await action(); }
  finally {
    release();
    if (saveLocks.get(key) === gate) saveLocks.delete(key);
  }
}
const revision = bytes => createHash('sha256').update(bytes).digest('hex');
function decodeText(bytes) {
  if (bytes.includes(0)) fail('BINARY_FILE', 'Binary content is not editable');
  try { return new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(bytes); }
  catch { fail('BINARY_FILE', 'Only valid UTF-8 text is editable'); }
}
async function readBytes(absolute, limit) {
  const before = await noLinks(absolute);
  if (!before.isFile()) fail('UNSUPPORTED_FILE', 'Only regular files are supported');
  if (before.size > limit) fail('TOO_LARGE', `Asset exceeds ${limit} bytes`);
  const handle = await fs.open(absolute, constants.O_RDONLY | (constants.O_NOFOLLOW || 0));
  try {
    const opened = await handle.stat();
    const current = await noLinks(absolute);
    if (!opened.isFile() || opened.dev !== current.dev || opened.ino !== current.ino || opened.ino !== before.ino) {
      fail('UNSAFE_PATH', 'Asset changed while opening');
    }
    if (opened.size > limit) fail('TOO_LARGE', `Asset exceeds ${limit} bytes`);
    const chunks = [];
    let total = 0;
    while (true) {
      const buffer = Buffer.allocUnsafe(Math.min(65536, limit + 1 - total));
      const { bytesRead } = await handle.read(buffer, 0, buffer.length, null);
      if (!bytesRead) break;
      total += bytesRead;
      if (total > limit) fail('TOO_LARGE', `Asset exceeds ${limit} bytes`);
      chunks.push(buffer.subarray(0, bytesRead));
    }
    await noLinks(absolute);
    return Buffer.concat(chunks, total);
  } finally { await handle.close(); }
}
function fail(code, message) { throw Object.assign(new Error(message), { code }); }
function safeRelative(relative) {
  if (typeof relative !== 'string' || !relative || relative.length > 4096) fail('INVALID_PATH', 'Expected a relative asset path');
  const normalized = relative.replaceAll('\\', '/');
  for (const component of normalized.split('/')) {
    if (!component || component.startsWith('.') || /[ .]$/.test(component) || /[<>:"|?*\x00-\x1f]/.test(component) ||
      /^(?:con|prn|aux|nul|com[1-9¹²³]|lpt[1-9¹²³])(?:\.|$)/i.test(component)) {
      fail('INVALID_PATH', 'Unsafe asset path');
    }
  }
  return normalized;
}
// Check every ancestor, not just the leaf: junctions are symlinks on Windows.
async function noLinks(absolute) {
  const parsed = path.parse(absolute);
  let current = parsed.root;
  let stat = await fs.lstat(current);
  for (const component of absolute.slice(parsed.root.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, component);
    stat = await fs.lstat(current);
    if (stat.isSymbolicLink()) fail('UNSAFE_PATH', 'Symlinks and junctions are not allowed');
  }
  return stat;
}

function metadata(relative, size) {
  const name = path.posix.basename(relative);
  const object = /^gml_Object_(.+)_([^_]+_\d+)(?:\.(?:append|prepend))?\.(?:gml|patch)$/i.exec(name);
  let kind;
  if (object) kind = 'object';
  else if (/\.gml$/i.test(name)) kind = 'script';
  else if (/\.(?:patch|diff)$/i.test(name)) kind = 'patch';
  else if (/\.origin\.txt$/i.test(name)) kind = 'origin';
  else if (/\.png$/i.test(name)) kind = 'sprite';
  else if (/\.(?:ogg|wav)$/i.test(name)) kind = 'sound';
  else return null;
  return { path: relative, name, kind,
    chapter: relative.split('/').find(p => /^(?:all_chapters|chapter\d+_windows)$/.test(p)) || 'launcher',
    object: object ? object[1] : null, event: object ? object[2] : null, size };
}

class Workspace {
  constructor(root) { this.root = path.resolve(root); }

  async read(relative) {
    relative = safeRelative(relative);
    const item = metadata(relative);
    if (!item) fail('UNSUPPORTED_FILE', 'Unsupported loose-mod asset');
    const absolute = path.join(this.root, relative);
    const bytes = await readBytes(absolute, item.kind === 'sprite' || item.kind === 'sound' ? MEDIA_LIMIT : TEXT_LIMIT);
    const result = { path: relative, kind: item.kind, revision: revision(bytes) };
    if (item.kind === 'sprite' || item.kind === 'sound') {
      const mime = item.kind === 'sprite' ? 'image/png' : /\.ogg$/i.test(relative) ? 'audio/ogg' : 'audio/wav';
      result.dataUrl = `data:${mime};base64,${bytes.toString('base64')}`;
    } else result.text = decodeText(bytes);
    return result;
  }

  async save(relative, text, expectedRevision) {
    relative = safeRelative(relative);
    const absolute = path.join(this.root, relative);
    const key = process.platform === 'win32' ? absolute.toLowerCase() : absolute;
    return exclusive(key, () => this.#save(relative, text, expectedRevision));
  }

  async #save(relative, text, expectedRevision) {
    const item = metadata(relative);
    if (!item) fail('UNSUPPORTED_FILE', 'Unsupported loose-mod asset');
    if (item.kind === 'sprite' || item.kind === 'sound') fail('BINARY_FILE', 'Media assets cannot be edited as text');
    if (typeof text !== 'string') fail('INVALID_TEXT', 'Text must be a string');
    if (Buffer.byteLength(text, 'utf8') > TEXT_LIMIT) fail('TOO_LARGE', `Text exceeds ${TEXT_LIMIT} bytes`);
    if (text.includes('\0') || Buffer.from(text, 'utf8').toString('utf8') !== text) fail('BINARY_FILE', 'Only valid UTF-8 text is editable');
    const previous = await this.read(relative);
    if (previous.revision !== expectedRevision) fail('REVISION_CONFLICT', 'Asset changed on disk; reload before saving');
    if (text === previous.text) return { path: relative, revision: previous.revision };
    // Retain the original BOM, first EOL style and final-newline convention.
    const eol = /\r\n|\r|\n/.exec(previous.text)?.[0] || '\n';
    let next = text.replace(/^\uFEFF/, '').replace(/\r\n|\r|\n/g, '\n').replace(/\n+$/, '');
    next = next.replace(/\n/g, eol);
    if (/[\r\n]$/.test(previous.text)) next += eol;
    if (previous.text.startsWith('\uFEFF')) next = '\uFEFF' + next;
    if (Buffer.byteLength(next, 'utf8') > TEXT_LIMIT) fail('TOO_LARGE', `Text exceeds ${TEXT_LIMIT} bytes after newline conversion`);
    const absolute = path.join(this.root, relative);
    const temporary = path.join(path.dirname(absolute), `.workspace-${randomUUID()}.tmp`);
    let handle;
    try {
      const target = await noLinks(absolute);
      handle = await fs.open(temporary, 'wx', target.mode & 0o777);
      const created = await handle.stat();
      await noLinks(temporary);
      await handle.writeFile(next, 'utf8');
      await handle.sync();
      await handle.close();
      handle = null;
      if ((await this.read(relative)).revision !== expectedRevision) fail('REVISION_CONFLICT', 'Asset changed during save; reload before saving');
      const currentTemporary = await noLinks(temporary);
      if (!currentTemporary.isFile() || currentTemporary.dev !== created.dev || currentTemporary.ino !== created.ino) {
        fail('UNSAFE_PATH', 'Temporary file changed during save');
      }
      await fs.rename(temporary, absolute);
    } finally {
      if (handle) await handle.close();
      await fs.rm(temporary, { force: true });
    }
    return { path: relative, revision: revision(Buffer.from(next)) };
  }

  async search(query) {
    if (typeof query !== 'string' || query.length > 4096) fail('INVALID_QUERY', 'Search requires a literal string up to 4096 characters');
    if (!query) return [];
    const results = [];
    for (const file of (await this.index()).files) {
      if (file.kind === 'sprite' || file.kind === 'sound' || file.size > TEXT_LIMIT) continue;
      let text;
      try { text = (await this.read(file.path)).text; }
      catch (error) {
        if (['BINARY_FILE', 'TOO_LARGE', 'ENOENT', 'UNSAFE_PATH'].includes(error.code)) continue;
        throw error;
      }
      const lines = text.split(/\r\n|\r|\n/);
      for (let index = 0; index < lines.length; index++) {
        if (lines[index].includes(query)) {
          results.push({ path: file.path, line: index + 1, text: lines[index] });
          if (results.length === 200) return results;
        }
      }
    }
    return results;
  }

  async index() {
    const files = [];
    const walk = async relative => {
      await noLinks(path.join(this.root, relative));
      const entries = await fs.readdir(path.join(this.root, relative), { withFileTypes: true });
      for (const entry of entries) {
        if (entry.isSymbolicLink() || entry.name.startsWith('.')) continue;
        const child = relative ? `${relative}/${entry.name}` : entry.name;
        try {
          safeRelative(child);
          if (entry.isDirectory()) await walk(child);
          else if (entry.isFile()) {
            const item = metadata(child);
            if (!item) continue;
            const stat = await noLinks(path.join(this.root, child));
            if (stat.isFile()) files.push({ ...item, size: stat.size });
          }
        } catch (error) {
          if (!['INVALID_PATH', 'UNSAFE_PATH', 'ENOENT'].includes(error.code)) throw error;
        }
      }
    };
    await walk('');
    files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
    return { name: path.basename(this.root), root: this.root, files };
  }
}
module.exports = { Workspace };
