'use strict';

const { contextBridge, ipcRenderer } = require('electron');

const invoke = (name, ...args) => ipcRenderer.invoke(name, ...args);
contextBridge.exposeInMainWorld('manager', Object.freeze({
  boot: () => invoke('boot'),
  refresh: () => invoke('games-refresh'),
  pickGame: () => invoke('pick-game'),
  pickScanFolder: () => invoke('pick-scan-folder'),
  install: id => invoke('game-install', id),
  repair: id => invoke('game-repair', id),
  uninstall: (id, removeSettings = false) => invoke('game-uninstall', id, removeSettings),
  diagnose: id => invoke('game-diagnose', id),
  readNr: id => invoke('nr-read', id),
  writeNr: (id, patch) => invoke('nr-write', id, patch),
  recommended: id => invoke('nr-recommended', id),
  updateSettings: patch => invoke('settings-update', patch),
  openExternal: key => invoke('open-external', key),
  openFolder: id => invoke('open-folder', id),
  copyText: text => invoke('copy-text', text),
  minimize: () => ipcRenderer.send('window-minimize'),
  close: () => ipcRenderer.send('window-close')
}));
