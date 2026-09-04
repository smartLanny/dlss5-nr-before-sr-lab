'use strict';
const {contextBridge,ipcRenderer}=require('electron');const invoke=(k,...v)=>ipcRenderer.invoke(k,...v);
contextBridge.exposeInMainWorld('manager',Object.freeze({
 boot:()=>invoke('boot'),refresh:()=>invoke('games-refresh'),pickGame:()=>invoke('pick-game'),pickScanFolder:()=>invoke('pick-scan-folder'),pickPayload:()=>invoke('pick-payload'),
 install:id=>invoke('game-install',id),repair:id=>invoke('game-repair',id),uninstall:(id,remove=false)=>invoke('game-uninstall',id,remove),recover:id=>invoke('game-recover',id),diagnose:id=>invoke('game-diagnose',id),
 readNr:id=>invoke('nr-read',id),writeNr:(id,patch,revision)=>invoke('nr-write',id,patch,revision),recommended:(id,revision)=>invoke('nr-recommended',id,revision),
 updateSettings:p=>invoke('settings-update',p),openExternal:k=>invoke('open-external',k),openFolder:id=>invoke('open-folder',id),copyText:t=>invoke('copy-text',t),minimize:()=>ipcRenderer.send('window-minimize'),close:()=>ipcRenderer.send('window-close')
}));
