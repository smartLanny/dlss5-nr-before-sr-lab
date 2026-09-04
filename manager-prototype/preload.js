'use strict';

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('managerApi', {
  bootstrap: () => ipcRenderer.invoke('prototype:bootstrap'),
  diagnose: () => ipcRenderer.invoke('prototype:diagnose'),
  copyDiagnostics: payload => ipcRenderer.invoke('prototype:copy-diagnostics', payload)
});
