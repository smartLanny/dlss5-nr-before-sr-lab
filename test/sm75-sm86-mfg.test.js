'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const syncFs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const journal = require('../src/core/file-journal');
const { BACKEND, RECEIPT, INI, PROXIES, resolveRouter, configFor, formatIni,
  createSm75Sm86Components, withSm75Sm86Components } = require('../src/product/sm75-sm86-mfg');
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
function image(dll = false, marker = 0) {
  const bytes = Buffer.alloc(512); bytes.writeUInt16LE(0x5a4d, 0); bytes.writeUInt32LE(128,60);
  bytes.writeUInt32LE(0x4550,128); bytes.writeUInt16LE(0x8664,132);
  bytes.writeUInt16LE(dll ? 0x2000 : 0,150); bytes.writeUInt16LE(0x20b,152); bytes[500] = marker;
  return bytes;
}
async function fixture(t, overrides = {}) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'sm-mfg-test-'));
  t.after(() => fs.rm(root,{recursive:true,force:true}));
  const game=path.join(root,'game'), dir=path.join(game,'bin'), pkg=path.join(root,'pkg'), exe=path.join(dir,'game.exe');
  await fs.mkdir(dir,{recursive:true}); await fs.mkdir(path.join(pkg,'altnative'),{recursive:true});
  await fs.writeFile(exe,image());
  const m = {schemaVersion:1,backend:BACKEND,runtimeVersion:'0.2.4',upstream:'sdli1995/dlssg_for_sm86',
    sourceCommit:'a'.repeat(40),proxies:{}};
  for(let i=0;i<PROXIES.length;i++) {
    const name=PROXIES[i], file=name==='version.dll'?name:`altnative/${name}`, bytes=image(true,i+1);
    await fs.writeFile(path.join(pkg,file),bytes); m.proxies[name]={file,sha256:sha(bytes)};
  }
  await fs.writeFile(path.join(pkg,'manifest.json'),JSON.stringify(m));
  const state={ hardware:{family:'RTX40',series:['RTX30'],names:['NVIDIA GeForce RTX 3080 Ti'],source:'Win32_VideoController'},
    observed:{api:'dx12',streamlineFg:true}, closed:0, guarded:null };
  const opts={ gameDirectory:()=>game, gameExecutable:()=>exe, sm75Sm86Resources:pkg,
    detectHardware:async()=>state.hardware, scan:async()=>state.observed,
    assertGameClosed:async()=>{state.closed++;if(state.guarded)await state.guarded();}, antiCheatPresent:()=>false, ...overrides };
  const svc=createSm75Sm86Components(opts);
  return {root,game,dir,pkg,exe,m,state,opts,svc,file:name=>path.join(dir,name),
    saveManifest:()=>fs.writeFile(path.join(pkg,'manifest.json'),JSON.stringify(m))};
}
const absent = async file => { await assert.rejects(fs.access(file),{code:'ENOENT'}); };
test('RTX20/30 routing, never installation-family alias',()=>{
  assert.equal(resolveRouter({series:['RTX20']}),'SM75');
  assert.equal(resolveRouter({series:['RTX30']}),'SM86');
  for(const h of [{series:['RTX40']},{series:['RTX50']},{family:'RTX40'}, {series:['RTX20','RTX30']},
    {series:['RTX30'],family:'mixed'}, {series:['RTX30'],source:'unavailable'},
    {series:['RTX30'],names:['RTX 3080','RTX 3080']}, {series:['RTX30'],names:['RTX 3080','AMD Radeon RX 9070']}])
    assert.throws(()=>resolveRouter(h));
});
test('five public INI keys, restart and cap semantics',()=>{
  const text=formatIni('SM86');
  assert.match(text,/Router=SM86/);assert.match(text,/KernelImage=PTX/);assert.match(text,/HardwareBilinear=0/);
  assert.match(text,/MaxGeneratedFrames=3/);assert.match(text,/Level=1/);
  assert.equal(text.split('\r\n').filter(line=>/^[A-Za-z]+=/.test(line)).length,5);
  assert.match(formatIni('SM75',{maxGeneratedFrames:1}),/Router=SM75/);
  for(const n of [0,4,-1,2.5,'3',NaN]) assert.throws(()=>configFor('SM86',{maxGeneratedFrames:n}));
  for(const c of [{kernelImage:'Cubin'},{kernelImage:'PTX\nBad=1'},{logLevel:4},{logLevel:'1'},
    {hardwareBilinear:1},{unknown:true}]) assert.throws(()=>configFor('SM86',c));
  assert.throws(()=>configFor('SM75',{hardwareBilinear:true}));
});
test('inspect -> explicit prepare -> idempotent check -> restore with production WAL',async t=>{
  const f=await fixture(t); const p=await f.svc.inspect('g');
  assert.equal(p.canPrepare,true);assert.equal(p.runtimeVerified,false);
  const result=await f.svc.prepare('g',{previewToken:p.previewToken});
  assert.equal(result.prepared,true);assert.equal(result.generatedFramesVerified,false);
  assert.equal((await f.svc.inspect('g')).ready,true);
  assert.equal((await f.svc.prepare('g')).unchanged,true);
  assert.equal(f.state.closed,2); // even no-op passes closed/revalidation gates
  assert.equal(await fs.readFile(f.file(INI),'utf8'),formatIni('SM86'));
  await absent(journal.pendingPath(f.game));
  assert.equal((await f.svc.restore('g')).restored,true);
  await absent(f.file('version.dll'));await absent(f.file(INI));
  await absent(path.join(f.game,RECEIPT)); assert.equal((await f.svc.restore('g')).unchanged,true);
});
test('RTX20 uses SM75 without enabling approximate sampling',async t=>{
  const f=await fixture(t);f.state.hardware={series:['RTX20'],names:['RTX 2080']};
  await f.svc.prepare('g');assert.match(await fs.readFile(f.file(INI),'utf8'),/Router=SM75/);
});
for(const [name,change] of [
  ['DX11',f=>{f.state.observed.api='dx11';}],
  ['unknown API',f=>{f.state.observed.api='unknown';}],
  ['missing FG',f=>{f.state.observed.streamlineFg=false;}],
  ['mixed GPU',f=>{f.state.hardware.series=['RTX20','RTX30'];}],
  ['x86 EXE',async f=>{const b=image();b.writeUInt16LE(0x14c,132);await fs.writeFile(f.exe,b);}],
  ['bad package hash',f=>{f.m.proxies['version.dll'].sha256='b'.repeat(64);return f.saveManifest();}],
  ['wrong proxy identity',f=>{f.m.proxies['version.dll'].file='altnative/winmm.dll';return f.saveManifest();}],
  ['invalid manifest path',f=>{f.m.proxies['version.dll'].file='../outside.dll';return f.saveManifest();}],
  ['wrong runtime version',f=>{f.m.runtimeVersion='0.2.3';return f.saveManifest();}],
  ['legacy RTX40 receipt',async f=>{await fs.mkdir(path.join(f.game,'_DLSS5_Backup'),{recursive:true});await fs.writeFile(path.join(f.game,'_DLSS5_Backup/xiaofeng-fg-components.json'),'{}');}],
  ['external Opti owner',f=>fs.writeFile(f.file('OptiScaler.ini'),'[FrameGeneration]\nEnabled=true')],
  ['external RTX40 owner',f=>fs.writeFile(f.file('RTX40MFGCore.dll'),'foreign')],
  ['unmanaged INI',f=>fs.writeFile(f.file(INI),'[General]\nEnabled=0')]
]) test(`${name}: zero component writes`,async t=>{
  const f=await fixture(t);await change(f);
  const status=await f.svc.inspect('g');assert.equal(status.canPrepare,false);
  await assert.rejects(f.svc.prepare('g'));await absent(f.file('version.dll'));await absent(journal.pendingPath(f.game));
});
test('existing foreign loader is preserved, a different verified proxy selected',async t=>{
  const f=await fixture(t);await fs.writeFile(f.file('version.dll'),'other mod');
  const p=await f.svc.inspect('g');assert.equal(p.proxy,'winmm.dll');await f.svc.prepare('g',{previewToken:p.previewToken});
  await f.svc.restore('g');assert.equal(await fs.readFile(f.file('version.dll'),'utf8'),'other mod');await absent(f.file('winmm.dll'));
});
test('explicit occupied proxy cannot be overwritten',async t=>{
  const f=await fixture(t);await fs.writeFile(f.file('version.dll'),'other mod');
  await assert.rejects(f.svc.prepare('g',{proxy:'version.dll'}));assert.equal(await fs.readFile(f.file('version.dll'),'utf8'),'other mod');
});
test('known native DLL without receipt is not silently adopted or duplicated',async t=>{
  const f=await fixture(t);await fs.copyFile(path.join(f.pkg,'version.dll'),f.file('version.dll'));
  await assert.rejects(f.svc.prepare('g'));await absent(f.file('winmm.dll'));await absent(f.file(INI));
});
test('settings update only owned INI; externally edited INI is preserved',async t=>{
  const f=await fixture(t);await f.svc.prepare('g');const old=sha(await fs.readFile(f.file('version.dll')));
  await f.svc.prepare('g',{settings:{maxGeneratedFrames:1}});
  assert.equal(sha(await fs.readFile(f.file('version.dll'))),old);assert.match(await fs.readFile(f.file(INI),'utf8'),/MaxGeneratedFrames=1/);
  await fs.appendFile(f.file(INI),'; player edit\n');
  await assert.rejects(f.svc.prepare('g',{settings:{maxGeneratedFrames:3}}));
  await assert.rejects(f.svc.restore('g'));assert.match(await fs.readFile(f.file(INI),'utf8'),/player edit/);
});
test('stale preview catches target change and leaves external bytes',async t=>{
  const f=await fixture(t);const p=await f.svc.inspect('g');await fs.writeFile(f.file('version.dll'),'new mod');
  await assert.rejects(f.svc.prepare('g',{previewToken:p.previewToken}),{code:'SETTINGS_SM_MFG_STALE_PREVIEW'});
  await absent(f.file('winmm.dll'));assert.equal(await fs.readFile(f.file('version.dll'),'utf8'),'new mod');
});
test('change during game-closed guard is rejected before WAL',async t=>{
  const f=await fixture(t);f.state.guarded=()=>fs.writeFile(f.file('version.dll'),'raced');
  await assert.rejects(f.svc.prepare('g'));await absent(journal.pendingPath(f.game));await absent(f.file(INI));
});
test('no-op path rechecks game-closed and receipt state',async t=>{
  const f=await fixture(t);await f.svc.prepare('g');f.state.guarded=()=>fs.appendFile(f.file(INI),'; changed');
  await assert.rejects(f.svc.prepare('g'));assert.match(await fs.readFile(f.file(INI),'utf8'),/changed/);
});
test('restore survives missing package, GPU change and deleted owned binary',async t=>{
  const f=await fixture(t);await f.svc.prepare('g');await fs.rm(f.pkg,{recursive:true});
  f.state.hardware={series:['RTX50']};await fs.unlink(f.file('version.dll'));
  assert.equal((await f.svc.inspect('g')).canRestore,true);await f.svc.restore('g');await absent(f.file(INI));
});
test('EXE update blocks prepare but does not strand safe file recovery',async t=>{
  const f=await fixture(t);await f.svc.prepare('g');await fs.writeFile(f.exe,image(false,9));
  await assert.rejects(f.svc.prepare('g'));await f.svc.restore('g');await absent(f.file(INI));
});
test('restore rejects a different selected EXE',async t=>{
  const f=await fixture(t);await f.svc.prepare('g');const other=path.join(f.dir,'other.exe');await fs.writeFile(other,image());
  const svc=createSm75Sm86Components({...f.opts,gameExecutable:()=>other});await assert.rejects(svc.restore('g'));await fs.access(f.file('version.dll'));
});
test('corrupt and path-forged receipt is never destructive',async t=>{
  const f=await fixture(t);await f.svc.prepare('g');const file=f.svc.receiptFile('g');const r=JSON.parse(await fs.readFile(file,'utf8'));
  r.files[0].name='../game.exe';await fs.writeFile(file,JSON.stringify(r));
  assert.equal((await f.svc.inspect('g')).canRestore,false);await assert.rejects(f.svc.restore('g'));await fs.access(f.file('version.dll'));await fs.access(f.exe);
});
test('receipt change during restore guard is rejected, not incorporated',async t=>{
  const f=await fixture(t);await f.svc.prepare('g');f.state.guarded=()=>fs.appendFile(f.svc.receiptFile('g'),' ');
  await assert.rejects(f.svc.restore('g'));await fs.access(f.file('version.dll'));await absent(journal.pendingPath(f.game));
});
test('source must be DLL, not an EXE renamed with .dll',async t=>{
  const f=await fixture(t);const b=image(false,1);await fs.writeFile(path.join(f.pkg,'version.dll'),b);f.m.proxies['version.dll'].sha256=sha(b);await f.saveManifest();
  await assert.rejects(f.svc.prepare('g'));await absent(f.file('version.dll'));
});
test('transaction failure after DLL copy rolls back through production journal',async t=>{
  const f=await fixture(t);const injected={...journal,capture:async(root,file)=>{
    if(path.basename(file)===INI)throw new Error('fault before second capture');return journal.capture(root,file);
  }};
  const svc=createSm75Sm86Components({...f.opts,journal:injected});await assert.rejects(svc.prepare('g'),/fault/);
  await absent(f.file('version.dll'));await absent(f.file(INI));await absent(journal.pendingPath(f.game));
});
test('external file appearing after capture is retained with pending recovery',async t=>{
  const f=await fixture(t);const injected={...journal,capture:async(root,file)=>{
    await journal.capture(root,file);if(path.basename(file)===INI)await fs.writeFile(file,'external appeared');
  }};
  const svc=createSm75Sm86Components({...f.opts,journal:injected});
  await assert.rejects(svc.prepare('g'),e=>e.code==='errBackendRecovery'&&e.pendingPreserved===true);
  assert.equal(await fs.readFile(f.file(INI),'utf8'),'external appeared');await fs.access(journal.pendingPath(f.game));
});
test('anti-cheat confirmation and game-closed failure never install',async t=>{
  const f=await fixture(t,{antiCheatPresent:()=>true});
  await assert.rejects(f.svc.prepare('g'),{code:'ERR_ANTI_CHEAT_CONFIRM'});await absent(f.file('version.dll'));
  f.state.guarded=()=>{throw new Error('game running');};
  await assert.rejects(f.svc.prepare('g',{allowAntiCheat:true}),/game running/);await absent(journal.pendingPath(f.game));
});
test('per-game mutations are serialized across service instances',async t=>{
  const f=await fixture(t);let release, entered;
  const enteredPromise=new Promise(r=>{entered=r;});const gate=new Promise(r=>{release=r;});
  f.state.guarded=async()=>{entered();await gate;};const pending=f.svc.prepare('g');await enteredPromise;
  const another=createSm75Sm86Components(f.opts);await assert.rejects(another.prepare('g'),{code:'errJobBusy'});release();await pending;
});
test('symlink and hardlink destinations are refused',async t=>{
  const f=await fixture(t);const external=path.join(f.root,'external.dll');await fs.writeFile(external,'outside');
  try { await fs.symlink(external,f.file('version.dll')); } catch(e) { if(['EPERM','ENOTSUP'].includes(e.code)){t.skip('symlink unavailable');return;}throw e; }
  await assert.rejects(f.svc.prepare('g'));await fs.unlink(f.file('version.dll'));
  await fs.link(external,f.file('version.dll'));await assert.rejects(f.svc.prepare('g'));
  assert.equal(await fs.readFile(external,'utf8'),'outside');
});
test('host decorator routes components only; recovery wins over new hardware',async()=>{
  const calls=[];let owner=false,h={series:['RTX30']};
  const leg={inspect:async()=>{calls.push('legacy.inspect');},prepare:async()=>{calls.push('legacy.prepare');},restore:async()=>{calls.push('legacy.restore');},receiptFile:()=>'/legacy'};
  const native={hasReceipt:async()=>owner,inspect:async()=>{calls.push('native.inspect');},prepare:async()=>{calls.push('native.prepare');},restore:async()=>{calls.push('native.restore');}};
  const svc=withSm75Sm86Components(leg,native,async()=>h);
  await svc.inspect('g');h={series:['RTX40']};await svc.prepare('g');
  owner=true;h={series:['RTX50']};await svc.restore('g');await svc.inspect('g');
  assert.deepEqual(calls,['native.inspect','legacy.prepare','native.restore','native.inspect']);assert.equal(svc.receiptFile(),'/legacy');
});
