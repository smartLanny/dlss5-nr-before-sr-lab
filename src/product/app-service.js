'use strict';
const fs=require('node:fs'),path=require('node:path');
const {PRODUCT}=require('./constants'),{createStore}=require('./state-store'),{createLibraryService}=require('./library-service');
const {createInstaller,readManifest}=require('./installer'),{inspectPayload,requirePayload,payloadRoot}=require('./payload');
const nr=require('./nr-config'),{safePath}=require('./safe-files'),{appError,normalizeError}=require('./errors');
function createAppService({userData,resourcesPath,appDir,version='0.2.0'}){
 const store=createStore(path.join(userData,'settings.json')),library=createLibraryService(),installer=createInstaller(),guards=require('../core/install-guards');
 const base=resourcesPath&&fs.existsSync(path.join(resourcesPath,'payload'))?resourcesPath:appDir;let games=[];const busy=new Set();
 const payloadDir=()=>store.read().payloadDirectory||payloadRoot(base);
 const find=id=>{const g=games.find(g=>g.id===id);if(!g)throw appError('ERR_UNKNOWN_GAME');return g;};
 async function job(id,fn){if(busy.has(id))throw appError('ERR_JOB_BUSY');busy.add(id);try{return await fn(find(id));}finally{busy.delete(id);}}
 async function refresh(){games=await library.scanAll(store.read());return games.map(({scan,...g})=>g);}
 function inspect(){try{return inspectPayload(payloadDir(),{allowMissingBundle:true});}catch(e){return{ready:false,error:normalizeError(e),missing:[],invalid:[],files:[]};}}
 function configFile(g){const m=readManifest(g.dir);if(!m)throw appError('ERR_NOT_INSTALLED');const exe=safePath(g.dir,m.game.exe);return safePath(g.dir,path.relative(g.dir,path.join(path.dirname(exe),'nr_before_sr.ini')));}
 async function save(g,patch,revision){const file=configFile(g);await guards.assertGameClosed(g.dir,safePath(g.dir,readManifest(g.dir).game.exe));return nr.writeConfig(file,patch,revision);}
 const service={
  product:{...PRODUCT,version},store,
  async withError(fn){try{return{ok:true,value:await fn()};}catch(e){return{ok:false,error:normalizeError(e)};}},
  async boot(){return{product:this.product,settings:store.read(),payload:inspect(),games:await refresh()};},refresh,
  gameDirectory:id=>find(id).dir,
  async addManualGame(dir){if(!path.isAbsolute(dir))throw appError('ERR_BAD_REQUEST');const s=store.read();await store.write({manualGames:[...s.manualGames,dir]});return refresh();},
  async addScanFolder(dir){if(!path.isAbsolute(dir))throw appError('ERR_BAD_REQUEST');const s=store.read();await store.write({scanFolders:[...s.scanFolders,dir]});return refresh();},
  async updateSettings(patch){if(!patch||Object.keys(patch).some(k=>k!=='scanDrives')||typeof patch.scanDrives!=='boolean')throw appError('ERR_BAD_REQUEST');return store.write(patch);},
  async selectPayload(dir){if(!path.isAbsolute(dir))throw appError('ERR_BAD_REQUEST');requirePayload(dir);await store.write({payloadDirectory:dir});return inspect();},
  install:id=>job(id,async g=>{const r=await installer.install({gameDir:g.dir,payload:requirePayload(payloadDir())});await refresh();return r;}),
  repair:id=>job(id,async g=>{const r=await installer.repair({gameDir:g.dir,payload:requirePayload(payloadDir())});await refresh();return r;}),
  uninstall:(id,removeSettings=false)=>job(id,async g=>{const r=await installer.uninstall({gameDir:g.dir,removeSettings});await refresh();return r;}),
  recover:id=>job(id,async g=>{const r=await installer.recover({gameDir:g.dir});await refresh();return r;}),
  diagnose:id=>job(id,g=>installer.diagnose({gameDir:g.dir})),
  readNrSettings:id=>job(id,g=>nr.readConfig(configFile(g))),
  writeNrSettings:(id,patch,revision)=>job(id,g=>save(g,patch,revision)),
  applyRecommended:(id,revision)=>job(id,async g=>{const defaults=nr.readConfig(requirePayload(payloadDir()).config.file),current=nr.readConfig(configFile(g));if(current.schema!==defaults.schema)throw appError('ERR_BAD_REQUEST');const keys=nr.allowed(current.schema),patch=Object.fromEntries(keys.filter(k=>Object.hasOwn(defaults.values,k)).map(k=>[k,defaults.values[k]]));return save(g,patch,revision);})
 };return service;
}
module.exports={createAppService};
