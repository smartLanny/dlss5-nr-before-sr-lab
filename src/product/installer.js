'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {INSTALLED_NAMES}=require('./constants'),{appError}=require('./errors'),{assess}=require('./game-support');
const {sha256}=require('./payload'),{safePath,readBounded,atomicWrite}=require('./safe-files');
const journal=require('./transaction');
const MANIFEST='_DLSS5_Backup/xiaofeng-manager.json';
const kinds=new Set(Object.keys(INSTALLED_NAMES));
function readManifest(root){const file=safePath(root,MANIFEST);if(!fs.existsSync(file))return null;let m;try{m=JSON.parse(readBounded(file));}catch{throw appError('ERR_BACKUP_INVALID');}
 if(m.version!==2||m.product!=='xiaofeng-dlss5-manager'||!/^[a-f0-9-]{36}$/.test(m.installId)||!Array.isArray(m.files)||m.files.length>16||!m.game)throw appError('ERR_BACKUP_INVALID');
 const exe=safePath(root,m.game.exe);const seen=new Set();
 for(const f of m.files){if(!kinds.has(f.kind)||path.basename(f.rel)!==INSTALLED_NAMES[f.kind]||seen.has(f.rel.toLowerCase())||!f.original||typeof f.original.existed!=='boolean'||typeof f.owned!=='boolean'||!/^[a-f0-9]{64}$/.test(f.installedSha256))throw appError('ERR_BACKUP_INVALID');seen.add(f.rel.toLowerCase());if(path.dirname(safePath(root,f.rel))!==path.dirname(exe))throw appError('ERR_BACKUP_INVALID');
  if(f.original.existed&&f.owned){const expect=`_DLSS5_Backup/xiaofeng-originals/${m.installId}/${f.kind}.bin`;if(f.original.backupRel!==expect||!/^[a-f0-9]{64}$/.test(f.original.sha256))throw appError('ERR_BACKUP_INVALID');safePath(root,expect);}}
 return m;
}
function createInstaller(deps={}){
 const scanModule=deps.scan||require('../core/scan'),guards=deps.guards||require('../core/install-guards'),pe=deps.pe||require('../core/pe');
 const tx=deps.transaction||journal;
 async function getScan(root){return scanModule.scanGame(root);}
 async function copy(source,target){await fs.promises.mkdir(path.dirname(target),{recursive:true});await fs.promises.copyFile(source,target);}
 async function checks(root,scan,payload){
  const s=assess(scan);if(!s.supported)throw appError(s.code);safePath(root,path.relative(root,scan.chosen.path));
  if(guards.antiCheatPresent(root))throw appError('ERR_ANTI_CHEAT');await guards.assertGameClosed(root,scan.chosen.path);
  for(const[k,p]of Object.entries(payload||{})){if(!kinds.has(k)||!p.expected||sha256(p.file)!==p.expected)throw appError('ERR_PAYLOAD_HASH');if(!['config','quickbarConfig'].includes(k)&&pe.getBitness(p.file)!==64)throw appError('ERR_PAYLOAD_HASH');}
  for(const k of ['reshade','addon','bridge','runtime','config'])if(!payload?.[k])throw appError('ERR_PAYLOAD_MISSING');
 }
 async function install({gameDir:root,payload,repair=false}){
  const scan=await getScan(root);await checks(root,scan,payload);const dir=path.dirname(scan.chosen.path);
  const prior=readManifest(root);if(prior&&prior.game.exe!==path.relative(root,scan.chosen.path))throw appError('ERR_CORE_CONFLICT');
  const other=fs.readdirSync(dir).filter(n=>/\.addon64$/i.test(n)&&/renodx.*dlss|nr.before.sr|DLSS5-AI/i.test(n)&&(!prior||!prior.files.some(f=>path.basename(f.rel)===n)));
  if(other.length)throw appError('ERR_CORE_CONFLICT',{files:other});
  const rs=scanModule.inspectReShade(dir),proxy=safePath(root,path.relative(root,path.join(dir,'dxgi.dll')));
  if(fs.existsSync(proxy)&&(!rs.installed||rs.file!=='dxgi.dll'))throw appError('ERR_RESHADER_CONFLICT');
  if(rs.installed&&!rs.addonSupport)throw appError('ERR_RESHADER_NO_ADDON');
  if(payload.quickbar&&rs.installed&&!/^6\.8\./.test(rs.version||pe.getFileVersion?.(path.join(dir,rs.file))||''))throw appError('ERR_RESHADER_NO_ADDON',{reason:'Quickbar requires tested ReShade 6.8.x'});
  const ri=path.join(dir,'ReShade.ini');if(fs.existsSync(ri)){safePath(root,path.relative(root,ri));const text=readBounded(ri).toString('utf8');const disabled=text.match(/^\s*DisabledAddons\s*=(.*)$/mi)?.[1]||'';if(/nr.before.sr|zjz.quickbar/i.test(disabled))throw appError('ERR_RESHADER_DISABLED');}
  await tx.transaction(root,async transaction=>{
   const m=prior||{version:2,product:'xiaofeng-dlss5-manager',installId:crypto.randomUUID(),installedAt:new Date().toISOString(),game:{exe:path.relative(root,scan.chosen.path)},files:[]};
   for(const[k,p]of Object.entries(payload)){
    if(k==='reshade'&&rs.installed&&!m.files.some(f=>f.kind===k))continue; // Existing user's runtime is never adopted.
    const target=safePath(root,path.relative(root,path.join(dir,INSTALLED_NAMES[k]))),rel=path.relative(root,target);
    let row=m.files.find(f=>f.kind===k);const exists=fs.existsSync(target),mutable=k==='config'||k==='quickbarConfig';
    if(mutable&&exists){if(!row)m.files.push({kind:k,rel,owned:false,original:{existed:true},installedSha256:sha256(target)});continue;}
    if(row&&exists&&!mutable&&sha256(target)!==row.installedSha256&&!repair)throw appError('ERR_FILE_CHANGED',{rel});
    if(row?.original.existed&&row.owned){const b=safePath(root,row.original.backupRel);if(!fs.existsSync(b)||sha256(b)!==row.original.sha256)throw appError('ERR_BACKUP_INVALID');}
    await transaction.capture(target);
    if(!row){row={kind:k,rel,owned:true,original:{existed:exists},installedSha256:p.expected};if(exists){row.original.backupRel=`_DLSS5_Backup/xiaofeng-originals/${m.installId}/${k}.bin`;row.original.sha256=sha256(target);const backup=safePath(root,row.original.backupRel);await transaction.capture(backup);await copy(target,backup);}m.files.push(row);}
    await copy(p.file,target);if(sha256(target)!==p.expected)throw appError('ERR_PAYLOAD_HASH');row.installedSha256=p.expected;
   }
   m.updatedAt=new Date().toISOString();const mf=safePath(root,MANIFEST);await transaction.capture(mf);await atomicWrite(mf,JSON.stringify(m,null,2));
  });return diagnose({gameDir:root,payload});
 }
 async function diagnose({gameDir:root,payload={}}){
  const m=readManifest(root),scan=await getScan(root);const dir=m?path.dirname(safePath(root,m.game.exe)):scan.chosen?path.dirname(scan.chosen.path):root;
  const rs=scanModule.inspectReShade(dir);const components=[{key:'reshade',label:'ReShade 宿主',ok:!!(rs.installed&&rs.addonSupport),detail:'文件检查；不是运行证明'}];
  const present=new Set(['addon','bridge','runtime','config',...(m?.files.map(f=>f.kind)||[])]);present.delete('reshade');
  for(const k of present){const f=m?.files.find(f=>f.kind===k),file=safePath(root,f?.rel||path.relative(root,path.join(dir,INSTALLED_NAMES[k])));const exists=fs.existsSync(file),mutable=['config','quickbarConfig'].includes(k),expected=f?.installedSha256||payload[k]?.expected;let ok=exists&&(mutable||Boolean(expected)&&sha256(file)===expected),detail=exists?(ok?'完整':'与安装记录不一致'):'缺失';
   if(mutable&&exists&&k==='config')try{require('./nr-config').readConfig(file);}catch{ok=false;detail='配置格式异常';}
   components.push({key:k,label:{addon:'NR 核心',bridge:'NR Bridge',runtime:'AI Runtime',config:'核心配置',quickbar:'Quickbar 面板',quickbarConfig:'面板配置'}[k],ok,detail});}
  return {installed:!!m,complete:!!m&&components.every(x=>x.ok),components,recoveryPending:fs.existsSync(safePath(root,journal.PENDING)),runtimeVerified:false};
 }
 async function uninstall({gameDir:root,removeSettings=false}){
  const m=readManifest(root);if(!m)throw appError('ERR_NOT_INSTALLED');await guards.assertGameClosed(root,safePath(root,m.game.exe));
  // Preflight ALL files and backups before touching the first one.
  for(const f of m.files){if(!f.owned||(['config','quickbarConfig'].includes(f.kind)&&!removeSettings))continue;const target=safePath(root,f.rel);
   if(!['config','quickbarConfig'].includes(f.kind)&&fs.existsSync(target)&&sha256(target)!==f.installedSha256)throw appError('ERR_FILE_CHANGED',{rel:f.rel});
   if(f.original.existed){const b=safePath(root,f.original.backupRel);if(!fs.existsSync(b)||sha256(b)!==f.original.sha256)throw appError('ERR_BACKUP_INVALID');}}
  return tx.transaction(root,async transaction=>{for(const f of [...m.files].reverse()){
   if(!f.owned||(['config','quickbarConfig'].includes(f.kind)&&!removeSettings))continue;const target=safePath(root,f.rel);await transaction.capture(target);
   if(f.original.existed)await copy(safePath(root,f.original.backupRel),target);else if(fs.existsSync(target))await fs.promises.unlink(target);
  }const mf=safePath(root,MANIFEST);await transaction.capture(mf);await fs.promises.unlink(mf);return{removed:true,settingsKept:!removeSettings,warnings:[]};});
 }
 async function repair(opts){if(!readManifest(opts.gameDir))throw appError('ERR_NOT_INSTALLED');return install({...opts,repair:true});}
 async function recover({gameDir:root}){const scan=await getScan(root);await guards.assertGameClosed(root,scan.chosen?.path);return tx.recover(root);}
 return{install,repair,diagnose,uninstall,recover};
}
module.exports={createInstaller,readManifest,MANIFEST};
