'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { createHash } = require('node:crypto');
const { Workspace } = require('../lib/workspace.cjs');

async function fixture(t, files = {}) {
  const root = await fs.mkdtemp(path.join(process.env.TMPDIR || os.tmpdir(), 'mod-workspace-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  for (const [relative, content] of Object.entries(files)) {
    await fs.mkdir(path.dirname(path.join(root, relative)), { recursive: true });
    await fs.writeFile(path.join(root, relative), content);
  }
  return { root, workspace: new Workspace(root) };
}
const hash = value => createHash('sha256').update(value).digest('hex');

test('read returns lossless text or MIME data URLs and byte revisions', async t => {
  const files = {
    'code/start.gml': '\uFEFFhello();\r\n',
    'sprites/test.png': Buffer.from([137, 80, 78, 71]),
    'sounds/test.ogg': Buffer.from('OggS'),
    'sounds/test.wav': Buffer.from('RIFF'),
    'sprites/test.origin.txt': '1,2\n',
    'code/fix.patch': '+hello\n'
  };
  const { workspace } = await fixture(t, files);
  for (const [relative, bytes] of Object.entries(files)) {
    const result = await workspace.read(relative.replaceAll('/', '\\'));
    assert.equal(result.path, relative);
    assert.equal(result.revision, hash(bytes));
    if (Buffer.isBuffer(bytes)) {
      const mime = relative.endsWith('.png') ? 'image/png' : relative.endsWith('.ogg') ? 'audio/ogg' : 'audio/wav';
      assert.equal(result.dataUrl, `data:${mime};base64,${bytes.toString('base64')}`);
      assert.equal(result.text, undefined);
    } else assert.equal(result.text, bytes);
  }
});

test('read rejects unsafe Windows paths, hidden paths and unsupported files', async t => {
  const { workspace } = await fixture(t, { 'ok.gml': 'ok', 'notes.txt': 'secret', '.private/hidden.gml': 'hidden' });
  for (const relative of ['', '../ok.gml', 'a/../ok.gml', '..\\ok.gml', '/ok.gml', '\\ok.gml',
    'C:/ok.gml', 'C:ok.gml', '\\\\server\\share\\ok.gml', 'ok.gml:secret', 'ok.gml\0',
    './ok.gml', 'a//ok.gml', 'a./ok.gml', 'a /ok.gml', 'CON.gml', 'COM1.gml', 'a?/ok.gml', '.private/hidden.gml']) {
    await assert.rejects(workspace.read(relative), { code: 'INVALID_PATH' }, relative);
  }
  await assert.rejects(workspace.read('notes.txt'), { code: 'UNSUPPORTED_FILE' });
  await assert.rejects(workspace.read('missing.gml'), { code: 'ENOENT' });
});

test('workspace rejects linked directories including linked roots and ancestors', async t => {
  const { root: outside } = await fixture(t, { 'secret.gml': 'outside' });
  const { root, workspace } = await fixture(t, { 'ok.gml': 'inside' });
  await fs.symlink(outside, path.join(root, 'linked'), process.platform === 'win32' ? 'junction' : 'dir');
  assert.deepEqual((await workspace.index()).files.map(f => f.path), ['ok.gml']);
  await assert.rejects(workspace.read('linked/secret.gml'), { code: 'UNSAFE_PATH' });
  await assert.rejects(new Workspace(path.join(root, 'linked')).index(), { code: 'UNSAFE_PATH' });
  await fs.mkdir(path.join(outside, 'child'));
  await assert.rejects(new Workspace(path.join(root, 'linked', 'child')).index(), { code: 'UNSAFE_PATH' });
});

test('read bounds allocations and rejects binary content disguised as editable text', async t => {
  const { workspace } = await fixture(t, {
    'large.gml': Buffer.alloc(4 * 1024 * 1024 + 1, 65),
    'large.png': Buffer.alloc(32 * 1024 * 1024 + 1),
    'binary.gml': Buffer.from([65, 0, 66]),
    'invalid.patch': Buffer.from([0xff, 0xfe, 0xfd])
  });
  for (const file of ['large.gml', 'large.png']) await assert.rejects(workspace.read(file), { code: 'TOO_LARGE' });
  for (const file of ['binary.gml', 'invalid.patch']) await assert.rejects(workspace.read(file), { code: 'BINARY_FILE' });
});

test('save atomically replaces text while preserving BOM, EOL and final-newline policy', async t => {
  const { root, workspace } = await fixture(t, { 'code/file.gml': '\uFEFFold();\r\n', 'plain.patch': '+old' });
  const previous = await workspace.read('code/file.gml');
  // A hard link observes the old inode: truncation-in-place would corrupt it.
  await fs.link(path.join(root, 'code/file.gml'), path.join(root, 'old-copy.txt'));
  const result = await workspace.save('code\\file.gml', 'new();\nsecond();', previous.revision);
  const expected = '\uFEFFnew();\r\nsecond();\r\n';
  assert.deepEqual(result, { path: 'code/file.gml', revision: hash(expected) });
  assert.equal(await fs.readFile(path.join(root, 'code/file.gml'), 'utf8'), expected);
  assert.equal(await fs.readFile(path.join(root, 'old-copy.txt'), 'utf8'), previous.text);
  assert.deepEqual(await fs.readdir(path.join(root, 'code')), ['file.gml']);
  const plain = await workspace.read('plain.patch');
  await workspace.save('plain.patch', '+new\r\n', plain.revision);
  assert.equal((await workspace.read('plain.patch')).text, '+new');
});

test('save refuses stale or missing revisions and never creates files', async t => {
  const { root, workspace } = await fixture(t, { 'edit.gml': 'old\n' });
  const original = await workspace.read('edit.gml');
  await fs.writeFile(path.join(root, 'edit.gml'), 'external\n');
  for (const expected of [original.revision, undefined, '', 'bogus']) {
    await assert.rejects(workspace.save('edit.gml', 'overwrite', expected), { code: 'REVISION_CONFLICT' });
  }
  await assert.rejects(workspace.save('missing.gml', 'new', original.revision), { code: 'ENOENT' });
  assert.equal(await fs.readFile(path.join(root, 'edit.gml'), 'utf8'), 'external\n');
  assert.deepEqual(await fs.readdir(root), ['edit.gml']);
});

test('save only accepts bounded UTF-8 text for existing editable assets', async t => {
  const files = { 'ok.gml': 'safe', 'image.png': 'PNG', 'song.ogg': 'OggS', 'binary.gml': Buffer.from([65, 0]) };
  const { root, workspace } = await fixture(t, files);
  for (const file of ['image.png', 'song.ogg', 'binary.gml']) {
    await assert.rejects(workspace.save(file, 'bad', hash(files[file])), { code: 'BINARY_FILE' });
  }
  for (const value of [null, Buffer.from('text'), 123]) await assert.rejects(workspace.save('ok.gml', value, hash('safe')), { code: 'INVALID_TEXT' });
  for (const value of ['a\0b', '\ud800']) await assert.rejects(workspace.save('ok.gml', value, hash('safe')), { code: 'BINARY_FILE' });
  await assert.rejects(workspace.save('ok.gml', 'x'.repeat(4 * 1024 * 1024 + 1), hash('safe')), { code: 'TOO_LARGE' });
  await assert.rejects(workspace.save('../outside.gml', 'bad', hash('safe')), { code: 'INVALID_PATH' });
  const { root: outside } = await fixture(t, { 'other.gml': 'outside' });
  await fs.symlink(outside, path.join(root, 'link'), process.platform === 'win32' ? 'junction' : 'dir');
  await assert.rejects(workspace.save('link/other.gml', 'bad', hash('outside')), { code: 'UNSAFE_PATH' });
  assert.equal(await fs.readFile(path.join(outside, 'other.gml'), 'utf8'), 'outside');
  assert.equal(await fs.readFile(path.join(root, 'ok.gml'), 'utf8'), 'safe');
  for (const [file, content] of Object.entries(files)) assert.deepEqual(await fs.readFile(path.join(root, file)), Buffer.from(content));
});

test('concurrent saves across workspace instances allow only one matching revision', async t => {
  const { root, workspace } = await fixture(t, { 'file.gml': 'old' });
  const original = await workspace.read('file.gml');
  const results = await Promise.allSettled([
    workspace.save('file.gml', 'first', original.revision),
    new Workspace(root).save('file.gml', 'second', original.revision)
  ]);
  assert.equal(results.filter(r => r.status === 'fulfilled').length, 1);
  assert.equal(results.find(r => r.status === 'rejected').reason.code, 'REVISION_CONFLICT');
  const current = await workspace.read('file.gml');
  assert.equal(current.revision, results.find(r => r.status === 'fulfilled').value.revision);
  assert.deepEqual(await fs.readdir(root), ['file.gml']);
});

test('save rechecks disk revision before rename and cleans up aborted temporary files', async t => {
  const { root, workspace } = await fixture(t, { 'file.gml': 'old' });
  const original = await workspace.read('file.gml');
  const open = fs.open;
  // Deterministically inject an external editor write after the initial read.
  t.mock.method(fs, 'open', async function (file, ...args) {
    if (path.basename(String(file)).startsWith('.workspace-')) await fs.writeFile(path.join(root, 'file.gml'), 'external');
    return open.call(this, file, ...args);
  });
  await assert.rejects(workspace.save('file.gml', 'overwrite', original.revision), { code: 'REVISION_CONFLICT' });
  assert.equal(await fs.readFile(path.join(root, 'file.gml'), 'utf8'), 'external');
  assert.deepEqual(await fs.readdir(root), ['file.gml']);
});

test('search is a case-sensitive literal text search with one-based lines and a 200-result cap', async t => {
  const { workspace } = await fixture(t, {
    'code/script.gml': 'before\r\nvalue = "[a.*]";\r\n[A.*]\r\n',
    'root.patch': '+[a.*]\n',
    'sprites/hero.origin.txt': '1,2\n',
    'sprites/fake.png': '[a.*]',
    '.hidden/a.gml': '[a.*]',
    'binary.gml': Buffer.from([0, 1]),
    'large.gml': Buffer.alloc(4 * 1024 * 1024 + 1, 65),
    'many.diff': 'needle\n'.repeat(240)
  });
  assert.deepEqual(await workspace.search('[a.*]'), [
    { path: 'code/script.gml', line: 2, text: 'value = "[a.*]";' },
    { path: 'root.patch', line: 1, text: '+[a.*]' }
  ]);
  assert.deepEqual(await workspace.search('1,2'), [{ path: 'sprites/hero.origin.txt', line: 1, text: '1,2' }]);
  assert.deepEqual(await workspace.search(''), []);
  const capped = await workspace.search('needle');
  assert.equal(capped.length, 200);
  assert.equal(capped[199].line, 200);
  for (const invalid of [null, 123, /test/, 'a'.repeat(4097)]) await assert.rejects(workspace.search(invalid), { code: 'INVALID_QUERY' });
});

test('save applies its byte limit after restoring newline policy', async t => {
  const { root, workspace } = await fixture(t, { 'file.gml': 'old\r\n' });
  const original = await workspace.read('file.gml');
  await assert.rejects(workspace.save('file.gml', 'x\n'.repeat(1500000), original.revision), { code: 'TOO_LARGE' });
  assert.equal((await workspace.read('file.gml')).text, original.text);
  assert.deepEqual(await fs.readdir(root), ['file.gml']);
});

test('saving unchanged text preserves exact bytes even for mixed EOL and trailing blank lines', async t => {
  const { workspace } = await fixture(t, { 'file.gml': '\uFEFFa\r\nb\nc\r\n\r\n' });
  const original = await workspace.read('file.gml');
  assert.deepEqual(await workspace.save('file.gml', original.text, original.revision), { path: 'file.gml', revision: original.revision });
  assert.deepEqual(await workspace.read('file.gml'), original);
});

test('index ignores a symlink swapped in after directory enumeration', async t => {
  const { root: outside } = await fixture(t, { 'secret.gml': 'outside' });
  const { root, workspace } = await fixture(t, { 'swap.gml': 'inside', 'ok.gml': 'okay' });
  const readdir = fs.readdir;
  let swapped = false;
  t.mock.method(fs, 'readdir', async function (directory, ...args) {
    const result = await readdir.call(this, directory, ...args);
    if (!swapped && directory === root) {
      swapped = true;
      await fs.unlink(path.join(root, 'swap.gml'));
      await fs.symlink(outside, path.join(root, 'swap.gml'), process.platform === 'win32' ? 'junction' : 'dir');
    }
    return result;
  });
  assert.deepEqual((await workspace.index()).files.map(f => f.path), ['ok.gml']);
});

test('read and save reject file symlinks without modifying their targets', async t => {
  const { root: outside } = await fixture(t, { 'secret.gml': 'outside' });
  const { root, workspace } = await fixture(t, { 'ok.gml': 'inside' });
  try { await fs.symlink(path.join(outside, 'secret.gml'), path.join(root, 'linked.gml'), 'file'); }
  catch (error) {
    if (process.platform === 'win32' && error.code === 'EPERM') return t.skip('Windows account lacks file-symlink privilege; junction cases are tested separately');
    throw error;
  }
  assert.deepEqual((await workspace.index()).files.map(f => f.path), ['ok.gml']);
  await assert.rejects(workspace.read('linked.gml'), { code: 'UNSAFE_PATH' });
  await assert.rejects(workspace.save('linked.gml', 'bad', hash('outside')), { code: 'UNSAFE_PATH' });
  assert.equal(await fs.readFile(path.join(outside, 'secret.gml'), 'utf8'), 'outside');
});

test('generic GameMaker loose roots need no chapter folders or game manifest', async t => {
  const { workspace } = await fixture(t, {
    'gml_GlobalScript_start.gml': 'start();',
    'code/gml_Object_obj_mountain_Draw_0.gml': 'draw_self();',
    'sprites/spr_mountain.png': 'PNG',
    'sounds/snd_step.wav': 'RIFF'
  });
  const indexed = await workspace.index();
  assert.equal(indexed.files.length, 4);
  assert.ok(indexed.files.every(file => file.chapter === 'launcher'));
  assert.equal(indexed.files.find(file => file.kind === 'object').object, 'obj_mountain');
  assert.equal((await workspace.read('gml_GlobalScript_start.gml')).text, 'start();');
});

test('rename failures leave original text intact and clean up the temporary file', async t => {
  const { root, workspace } = await fixture(t, { 'file.gml': 'old' });
  t.mock.method(fs, 'rename', async () => { throw Object.assign(new Error('Injected rename failure'), { code: 'EACCES' }); });
  await assert.rejects(workspace.save('file.gml', 'new', hash('old')), { code: 'EACCES' });
  assert.equal(await fs.readFile(path.join(root, 'file.gml'), 'utf8'), 'old');
  assert.deepEqual(await fs.readdir(root), ['file.gml']);
});

test('save refuses a temporary file replaced by a different regular file', async t => {
  const { root, workspace } = await fixture(t, { 'file.gml': 'old' });
  const open = fs.open;
  t.mock.method(fs, 'open', async function (file, ...args) {
    const handle = await open.call(this, file, ...args);
    if (path.basename(String(file)).startsWith('.workspace-')) {
      const close = handle.close.bind(handle);
      handle.close = async () => {
        await close();
        // Keep the original inode alive so replacement cannot reuse it.
        await fs.rename(file, path.join(root, 'stolen.tmp'));
        await fs.writeFile(file, 'malicious');
      };
    }
    return handle;
  });
  await assert.rejects(workspace.save('file.gml', 'new', hash('old')), { code: 'UNSAFE_PATH' });
  assert.equal(await fs.readFile(path.join(root, 'file.gml'), 'utf8'), 'old');
});

test('index classifies supported loose assets with deterministic metadata', async t => {
  const files = {
    'chapter2_windows/code/gml_Object_obj_player_Step_0.append.gml': 'step();\n',
    'all_chapters/code/gml_Object_obj_ui_Create_0.prepend.patch': '+init();',
    'code/gml_GlobalScript_boot.gml': 'boot();',
    'chapter1_windows/code/change.diff': '+ok',
    'sprites/hero.png': Buffer.from([137, 80, 78, 71]),
    'sprites/hero.origin.txt': '0,0',
    'sounds/music.ogg': 'OggS',
    'sounds/hit.wav': 'RIFF',
    '.git/ignored.gml': 'hidden',
    'nested/.cache/ignored.gml': 'hidden',
    'mod.json': '{}',
    'notes.txt': 'ignored'
  };
  const { root, workspace } = await fixture(t, files);
  const result = await workspace.index();
  assert.equal(result.name, path.basename(root));
  assert.equal(result.root, root);
  const expected = Object.keys(files).filter(p => !p.includes('.git/') && !p.includes('/.cache/') && !['mod.json', 'notes.txt'].includes(p)).sort();
  assert.deepEqual(result.files.map(f => f.path), expected);
  for (const file of result.files) {
    assert.deepEqual(Object.keys(file).sort(), ['chapter', 'event', 'kind', 'name', 'object', 'path', 'size']);
    assert.equal(file.name, path.posix.basename(file.path));
    assert.equal(file.size, Buffer.byteLength(files[file.path]));
  }
  const object = result.files.find(f => f.path.endsWith('.append.gml'));
  assert.equal(object.kind, 'object');
  assert.equal(object.chapter, 'chapter2_windows');
  assert.equal(object.object, 'obj_player');
  assert.equal(object.event, 'Step_0');
  assert.equal(result.files.find(f => f.path.endsWith('.prepend.patch')).kind, 'object');
  assert.equal(result.files.find(f => f.path.endsWith('.diff')).kind, 'patch');
  assert.deepEqual(result.files.filter(f => f.chapter === 'launcher').map(f => f.kind).sort(), ['origin', 'script', 'sound', 'sound', 'sprite']);
});
