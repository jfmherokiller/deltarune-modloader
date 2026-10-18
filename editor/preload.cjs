const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('editorAPI', {
  initial: () => ipcRenderer.invoke('workspace:initial'),
  choose: () => ipcRenderer.invoke('workspace:choose'),
  index: () => ipcRenderer.invoke('workspace:index'),
  read: path => ipcRenderer.invoke('workspace:read', path),
  save: (path, text, revision) => ipcRenderer.invoke('workspace:save', path, text, revision),
  search: query => ipcRenderer.invoke('workspace:search', query),
  discover: () => ipcRenderer.invoke('runtime:discover'),
  snapshot: pid => ipcRenderer.invoke('runtime:snapshot', pid),
  inspect: (pid, query) => ipcRenderer.invoke('runtime:inspect', pid, query)
});
