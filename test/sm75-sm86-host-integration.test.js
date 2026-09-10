'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const vm = require('node:vm');
const ui = require('../src/renderer/sm75-sm86-mfg-ui');
const { planSourceEdits, applyToRoot } = require('../scripts/integrate-sm75-sm86-mfg');
const info = { backend: ui.BACKEND, ready: false, canPrepare: true, managed: false, canRestore: false,
  router: 'SM86', proxy: 'version.dll', settings: {maxGeneratedFrames:3, kernelImage:'PTX', hardwareBilinear:false,logLevel:1},
  previewToken:'preview', blockers:[] };
function sourceFixture(newline = '\n') {
  // Exact observed host seams from Manager #195 / 29ccaeb5; surrounding code
  // is a minimal runnable harness, not a claimed complete Electron UI test.
  const files = {
    'src/product/fg-components.js': "'use strict';\nconst detectGpu = async () => ({series:['RTX40']});\nfunction createFgComponents() { return {inspect:async()=>({route:'compatibility'}),prepare:async()=>({prepared:true}),restore:async()=>({restored:true})}; }\nfunction mergeUalConfig() {}\nfunction validControl() {}\nconst DEFAULT_CONTROL='legacy';\nmodule.exports = { createFgComponents, mergeUalConfig, validControl, DEFAULT_CONTROL };\n",
    'src/renderer/launch-settings-ui.js': `(function(scope){
  function fgComponentMarkup(data, busy) {
    const info = data.fgComponents;
    return info;
  }
  const unavailable = 'RTX 20 / 30 尚无已确认的 FG 控制路线。';
  async function perform(action) {
    if (action === 'noop') {
        } else if (action === 'prepare') {
          render();
          let response = await manager.prepareFgComponents(id, { allowAntiCheat: false });
          if (response?.ok === false && response.error?.code === 'ERR_ANTI_CHEAT_CONFIRM') {
            response = await manager.prepareFgComponents(id, { allowAntiCheat: true });
          }
    }
  }
  scope.componentMarkup = fgComponentMarkup;
  scope.perform = perform;
})(globalThis);
`,
    'src/renderer/index.html': '<!doctype html>\n<!-- owner local layout must survive -->\n  <script src="launch-settings-ui.js"></script>\n',
    'src/product/launch-coordinator.js': `const path = require('path'), fs = require('fs');
function createLaunchCoordinator(service, components) {
  const hasComponentReceipt = id => fs.existsSync(path.join(service.gameDirectory(id), '_DLSS5_Backup', 'xiaofeng-fg-components.json'));
  async function launch(id, fg) {
      if (fg && fg.mode !== 'restore') {
        const result = await components.inspect(id); if (!result.ready) throw new Error('blocked');
      }
      return 'launch';
  }
  const receipts = ['xiaofeng-launch-settings.json', 'xiaofeng-fg-components.json'];
  return {launch, hasComponentReceipt, receipts};
}
`,
    'main.js': "async function prepare(id, options) {\n    const result = await fgComponents.prepare(id, { allowAntiCheat: options?.allowAntiCheat === true });\n    return result;\n}\n"
  };
  return Object.fromEntries(Object.entries(files).map(([name, text]) => [name, text.replace(/\n/g,newline)]));
}
test('component UI is explicit, cap-only and never claims generated frames',()=>{
  const html = ui.components(info);
  assert.match(html,/最高 4×/);assert.match(html,/实际倍率由游戏请求/);assert.match(html,/默认不安装/);
  assert.match(html,/data-ls-action="prepare"/);assert.doesNotMatch(html,/data-ls-field=|最高 5×|最高 6×/);
  assert.match(ui.components({...info,ready:true,canPrepare:false,managed:true,canRestore:true}),/游戏内效果尚未验证/);
  assert.match(ui.components({...info,ready:true,canPrepare:false,canRestore:true}),/remove-components/);
  assert.equal(ui.components({route:'compatibility'}),'');
});
test('blocked/invalid receipt UI cannot offer destructive recovery',()=>{
  const html=ui.components({...info,canPrepare:false,canRestore:false,managed:true,blockers:['bad receipt']});
  assert.doesNotMatch(html,/data-ls-action="prepare"|remove-components/);
  assert.match(html,/请提交管理器反馈/);assert.match(html,/ disabled/);
});
test('all diagnostic and path text is escaped',()=>{
  const html=ui.components({...info,proxy:'<img onerror="bad">',router:'<SM>',blockers:['<script>x</script>']});
  assert.doesNotMatch(html,/<img|<script>|<SM>/);assert.match(html,/&lt;img/);
});
test('busy blocks both mutation buttons',()=>{
  const html=ui.components({...info,canRestore:true},true);
  assert.match(html,/data-ls-action="prepare" disabled/);assert.match(html,/data-ls-action="remove-components" disabled/);
});
test('request captures cap before busy render, avoids false same-config token',()=>{
  const host={querySelector:()=>({value:'3',disabled:false})};
  assert.equal(ui.request(host,info).previewToken,'preview');
  const changed=ui.request({querySelector:()=>({value:'1',disabled:false})},info);
  assert.equal(changed.settings.maxGeneratedFrames,1);assert.equal(changed.previewToken,undefined);
  assert.equal(changed.settings.kernelImage,'PTX');assert.equal(changed.proxy,'version.dll');
  assert.deepEqual(ui.request(null,{route:'native'}),{});
  for(const value of ['','0','4','NaN','2.5']) assert.throws(()=>ui.request({querySelector:()=>({value})},info));
  assert.throws(()=>ui.request({querySelector:()=>({value:'3',disabled:true})},info));
});
test('all five exact source seams patch, preserve owner content and are idempotent',()=>{
  const before=sourceFixture(), after=planSourceEdits(before);
  assert.equal(Object.keys(after).length,5);assert.deepEqual(planSourceEdits(after),after);
  assert.match(after['src/product/fg-components.js'],/withSm75Sm86Components/);
  assert.match(after['src/renderer/index.html'],/owner local layout must survive/);
  assert(after['src/renderer/index.html'].indexOf('sm75-sm86-mfg-ui.js')<after['src/renderer/index.html'].indexOf('src="launch-settings-ui.js'));
  assert.match(after['main.js'],/previewToken: options\?\.previewToken/);
});
test('CRLF and BOM survive integration',()=>{
  const before=sourceFixture('\r\n');before['main.js']='\uFEFF'+before['main.js'];
  const after=planSourceEdits(before);
  for(const text of Object.values(after)) assert.equal(text.replace(/\r\n/g,'').includes('\n'),false);
  assert.equal(after['main.js'].charCodeAt(0),0xfeff);assert.deepEqual(planSourceEdits(after),after);
});
test('changed, duplicate and partial newer-local seams fail closed',()=>{
  for(const mode of ['changed','duplicate','partial']) {
    const files=sourceFixture();
    if(mode==='changed')files['main.js']=files['main.js'].replace('options?.allowAntiCheat','opts?.allowAntiCheat');
    if(mode==='duplicate')files['src/renderer/index.html']+=files['src/renderer/index.html'];
    if(mode==='partial')files['main.js']='// sm75-sm86-mfg: host integration v1\n'+files['main.js'];
    const snapshot=JSON.stringify(files);assert.throws(()=>planSourceEdits(files));assert.equal(JSON.stringify(files),snapshot);
  }
});
test('patched host UI executes real renderer helper and delegates only native component',()=>{
  const after=planSourceEdits(sourceFixture()), scope={Sm75Sm86MfgUI:ui};vm.createContext(scope);
  vm.runInContext(after['src/renderer/launch-settings-ui.js'],scope);
  assert.match(scope.componentMarkup({fgComponents:info},false),/RTX 20 \/ 30/);
  const legacy={route:'compatibility'};assert.equal(scope.componentMarkup({fgComponents:legacy},false),legacy);
  delete scope.Sm75Sm86MfgUI;assert.match(scope.componentMarkup({fgComponents:info},false),/模块未加载/);
});
test('patched prepare carries cap through busy render and anti-cheat retry',async()=>{
  const requests=[], after=planSourceEdits(sourceFixture());
  let cap='1';const scope={Sm75Sm86MfgUI:ui,data:{fgComponents:info},id:'game',
    host:{querySelector:()=>({value:cap,disabled:false})},render:()=>{cap='3';},
    manager:{prepareFgComponents:async(id,request)=>{requests.push(request);return requests.length===1?{ok:false,error:{code:'ERR_ANTI_CHEAT_CONFIRM'}}:{ok:true};}}};
  vm.createContext(scope);vm.runInContext(after['src/renderer/launch-settings-ui.js'],scope);await scope.perform('prepare');
  assert.equal(requests.length,2);assert.equal(requests[0].settings.maxGeneratedFrames,1);assert.equal(requests[1].settings.maxGeneratedFrames,1);
  assert.equal(requests[0].allowAntiCheat,false);assert.equal(requests[1].allowAntiCheat,true);
});
test('patched IPC passes only expected optional fields, not arbitrary payload',async()=>{
  const after=planSourceEdits(sourceFixture());let received;
  const scope={fgComponents:{prepare:async(id,opts)=>{received=opts;return {prepared:true};}}};
  vm.createContext(scope);vm.runInContext(after['main.js'],scope);
  await scope.prepare('game',{allowAntiCheat:'yes',settings:{maxGeneratedFrames:2},proxy:'version.dll',previewToken:'x',command:'ignore'});
  assert.equal(received.allowAntiCheat,false);assert.equal(received.settings.maxGeneratedFrames,2);assert.equal(received.command,undefined);
});
test('dry-run/apply actual temp source tree; failed seam leaves every file unchanged',t=>{
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'sm-host-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
  const before=sourceFixture();for(const [name,text]of Object.entries(before)){fs.mkdirSync(path.dirname(path.join(root,name)),{recursive:true});fs.writeFileSync(path.join(root,name),text);}
  for(const name of ['src/product/sm75-sm86-mfg.js','src/renderer/sm75-sm86-mfg-ui.js'])fs.copyFileSync(path.join(__dirname,'..',name),path.join(root,name));
  assert.equal(applyToRoot(root).changed.length,5);assert.equal(fs.readFileSync(path.join(root,'main.js'),'utf8'),before['main.js']);
  fs.appendFileSync(path.join(root,'main.js'),'// sm75-sm86-mfg: host integration v1\n');
  assert.throws(()=>applyToRoot(root,true));assert.equal(fs.readFileSync(path.join(root,'src/product/fg-components.js'),'utf8'),before['src/product/fg-components.js']);
  fs.writeFileSync(path.join(root,'main.js'),before['main.js']);assert.equal(applyToRoot(root,true).applied,true);
  assert.equal(applyToRoot(root,true).changed.length,0);
});

test('patched launch guard checks standalone MFG without a saved legacy FG request',async()=>{
  const after=planSourceEdits(sourceFixture());let checked=0;
  const scope={require};vm.createContext(scope);vm.runInContext(after['src/product/launch-coordinator.js'],scope);
  const coordinator=scope.createLaunchCoordinator({gameDirectory:()=>'.'},
    {hasNativeReceipt:async()=>true,inspect:async()=>{checked++;return {ready:false};}});
  await assert.rejects(coordinator.launch('g',null),/blocked/);assert.equal(checked,1);
  assert(coordinator.receipts.includes('xiaofeng-sm75-sm86-mfg.json'));
  const legacy=scope.createLaunchCoordinator({}, {inspect:async()=>{throw Error('unexpected');}});
  assert.equal(await legacy.launch('g',null),'launch');
});
