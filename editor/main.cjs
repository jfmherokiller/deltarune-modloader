const { app, BrowserWindow, ipcMain, dialog, Menu, session } = require('electron');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

let window;
let workspace;
const documentUrl = pathToFileURL(path.join(__dirname, 'ui', 'index.html')).href;
const argument = process.argv.find(value => value.startsWith('--workspace='));

function handle(name, handler) {
  ipcMain.handle(name, (event, ...args) => {
    if (event.sender !== window.webContents || event.senderFrame?.url !== documentUrl)
      throw new Error('Untrusted caller');
    return handler(...args);
  });
}
function current() {
  if (!workspace) throw new Error('Open a loose-mod folder first.');
  return workspace;
}
async function open(root) {
  const { Workspace } = require('./lib/workspace.cjs');
  const candidate = new Workspace(root);
  const index = await candidate.index();
  workspace = candidate;
  return index;
}

app.whenReady().then(async () => {
  Menu.setApplicationMenu(null);
  session.defaultSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  window = new BrowserWindow({
    width: 1460, height: 940, minWidth: 1000, minHeight: 680,
    backgroundColor: '#25272b', title: 'Runner Studio — Runtime Editor',
    webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true,
      nodeIntegration: false, sandbox: true, webSecurity: true }
  });
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  window.webContents.on('will-prevent-unload', event => {
    const choice = dialog.showMessageBoxSync(window, {
      type: 'warning', buttons: ['Keep editing', 'Discard and close'], defaultId: 0, cancelId: 0,
      title: 'Unsaved loose files', message: 'Close without saving your changes?'
    });
    if (choice === 1) event.preventDefault();
  });
  handle('workspace:initial', () => argument ? open(argument.slice('--workspace='.length)) : null);
  handle('workspace:choose', async () => {
    const result = await dialog.showOpenDialog(window, { title: 'Open loose-mod folder', properties: ['openDirectory'] });
    return result.canceled ? null : open(result.filePaths[0]);
  });
  handle('workspace:index', () => current().index());
  handle('workspace:read', relative => current().read(relative));
  handle('workspace:save', (relative, text, revision) => current().save(relative, text, revision));
  handle('workspace:search', query => current().search(query));
  handle('runtime:discover', () => require('./lib/runtime.cjs').discover());
  handle('runtime:snapshot', pid => require('./lib/runtime.cjs').snapshot(pid));
  handle('runtime:inspect', (pid, query) => require('./lib/runtime.cjs').inspect(pid, query));
  await window.loadURL(documentUrl);
});
app.on('window-all-closed', () => app.quit());
