'use strict';
// Resolve an Electron binary that runs on this OS.
//
// node_modules/electron/dist holds the build for whichever OS last ran `npm ci`. A checkout
// shared between Windows and Linux (dual boot, one drive) therefore has a Windows electron.exe
// when opened on Linux. Instead of overwriting it, a pinned copy of the same Electron version
// is downloaded once into .electron-<platform>-<arch>/ (checksums from electron/checksums.json).
// RUNNER_STUDIO_ELECTRON overrides everything.
//
//   node scripts/electron-path.cjs            print the path
//   node scripts/electron-path.cjs --run ...  run Electron with the remaining arguments
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const binaryName = { win32: 'electron.exe', darwin: 'Electron.app/Contents/MacOS/Electron' }[process.platform] || 'electron';

async function electronPath() {
  if (process.env.RUNNER_STUDIO_ELECTRON) return process.env.RUNNER_STUDIO_ELECTRON;
  const pkg = path.dirname(require.resolve('electron/package.json'));
  let bundled = null;
  try { bundled = path.join(pkg, 'dist', fs.readFileSync(path.join(pkg, 'path.txt'), 'utf8')); } catch { /* not installed */ }
  if (bundled && path.basename(bundled) === path.basename(binaryName) && fs.existsSync(bundled)) return bundled;

  const { version } = require('electron/package.json');
  const dir = path.join(root, `.electron-${process.platform}-${process.arch}`);
  const binary = path.join(dir, binaryName);
  try {
    if (fs.readFileSync(path.join(dir, 'version'), 'utf8').trim().replace(/^v/, '') === version && fs.existsSync(binary)) return binary;
  } catch { /* download below */ }
  process.stderr.write(`node_modules has an Electron build for another OS; fetching Electron ${version} for ${process.platform}-${process.arch} into ${dir}\n`);
  const { downloadArtifact } = require('@electron/get');
  const zip = await downloadArtifact({ version, artifactName: 'electron', platform: process.platform, arch: process.arch,
    checksums: require('electron/checksums.json') });
  fs.rmSync(dir, { recursive: true, force: true });
  const { extract } = await import('@electron-internal/extract-zip');
  await extract(zip, { dir });
  if (!fs.existsSync(binary)) throw new Error(`Electron archive did not contain ${binaryName}`);
  return binary;
}

module.exports = { electronPath };

if (require.main === module) {
  electronPath().then(binary => {
    const run = process.argv.indexOf('--run');
    if (run < 0) return console.log(binary);
    const { spawn } = require('node:child_process');
    const child = spawn(binary, process.argv.slice(run + 1), { stdio: 'inherit', cwd: root });
    child.on('exit', (code, signal) => process.exit(signal ? 1 : code));
  }, error => { console.error(error.message); process.exit(1); });
}
