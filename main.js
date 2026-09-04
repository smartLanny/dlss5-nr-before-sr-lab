'use strict';
const path=require('node:path'),{pathToFileURL}=require('node:url');
const {app,BrowserWindow,ipcMain,dialog,shell,clipboard}=require('electron');
const {createAppService}=require('./src/product/app-service');
let win,service;const page=path.join(__dirname,'src','renderer','index.html');const expected=pathToFileURL(page).href;
function trusted(event){return win&&!win.isDestroyed()&&event.sender===win.webContents&&event.senderFrame===win.webContents.mainFrame&&event.senderFrame.url===expected;}
async function confirm(title,detail){return (await dialog.showMessageBox(win,{type:'warning',title,message:title,detail,buttons:['取消','继续'],defaultId:0,cancelId:0,noLink:true})).response===1;}
function register(){
 const call=(name,fn)=>ipcMain.handle(name,async(e,...args)=>{if(!trusted(e))return{ok:false,error:{message:'拒绝不受信任的请求。'}};return service.withError(()=>fn(...args));});
 call('boot',()=>service.boot());call('games-refresh',()=>service.refresh());
 call('game-install',async id=>{if(!await confirm('安装 NR 核心与面板','将备份并部署已校验组件。不会更换游戏自带的 SR/RR/FG DLL。请关闭游戏；反作弊检测不能保证账号安全。'))return{cancelled:true};return service.install(id);});
 call('game-repair',async id=>{if(!await confirm('修复已管理的组件','会用所选组件包恢复缺失或改动过的组件，保留参数。请先关闭游戏。'))return{cancelled:true};return service.repair(id);});
 call('game-uninstall',async(id,remove)=>{if(!await confirm('卸载本管理器安装的组件',remove?'同时删除由本管理器创建的参数文件。':'保留参数；不删除外部管理的文件。'))return{cancelled:true};return service.uninstall(id,remove===true);});
 call('game-recover',async id=>{if(!await confirm('恢复未完成的操作','请关闭游戏。恢复将使用上次事务快照，放弃该操作中未提交的改动。'))return{cancelled:true};return service.recover(id);});
 call('game-diagnose',id=>service.diagnose(id));call('nr-read',id=>service.readNrSettings(id));
 call('nr-write',(id,patch,revision)=>service.writeNrSettings(id,patch,revision));call('nr-recommended',(id,revision)=>service.applyRecommended(id,revision));
 call('settings-update',patch=>service.updateSettings(patch));
 for(const[name,title,method]of [['pick-game','选择游戏安装目录','addManualGame'],['pick-scan-folder','选择游戏库目录','addScanFolder'],['pick-payload','选择含 bundle.json 的受信任组件目录','selectPayload']])call(name,async()=>{const r=await dialog.showOpenDialog(win,{title,properties:['openDirectory']});return r.canceled?null:service[method](r.filePaths[0]);});
 call('open-external',async key=>{if(!['bilibiliUrl','releaseUrl'].includes(key))return false;const url=service.product[key];if(typeof url!=='string'||!url.startsWith('https://'))return false;await shell.openExternal(url);return true;});
 call('open-folder',async id=>(await shell.openPath(service.gameDirectory(id)))==='');
 call('copy-text',text=>{if(typeof text!=='string'||text.length>131072)return false;clipboard.writeText(text);return true;});
 for(const[key,action]of [['window-minimize',()=>win.minimize()],['window-close',()=>win.close()]])ipcMain.on(key,e=>{if(trusted(e))action();});
}
app.setAppUserModelId('com.xiaofeng.dlss5.manager');
if(!app.requestSingleInstanceLock())app.quit();else{
 app.on('second-instance',()=>{if(win){if(win.isMinimized())win.restore();win.focus();}});
 app.whenReady().then(()=>{service=createAppService({userData:app.getPath('userData'),resourcesPath:process.resourcesPath,appDir:__dirname,version:app.getVersion()});register();
 win=new BrowserWindow({width:1100,height:780,minWidth:860,minHeight:600,backgroundColor:'#101419',autoHideMenuBar:true,show:false,webPreferences:{preload:path.join(__dirname,'preload.js'),contextIsolation:true,nodeIntegration:false,sandbox:true,webviewTag:false}});
 win.webContents.setWindowOpenHandler(()=>({action:'deny'}));win.webContents.on('will-navigate',e=>e.preventDefault());win.webContents.session.setPermissionRequestHandler((_w,_p,cb)=>cb(false));
 win.loadFile(page);win.once('ready-to-show',()=>win.show());});
 app.on('window-all-closed',()=>app.quit());
}
