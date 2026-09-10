'use strict';
// Apply reviewed, exact call-site edits to the OWNER'S current Manager tree.
// No old complete file is copied over newer local UI. Default is dry-run.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
const marker = 'sm75-sm86-mfg: host integration v1';
const edits = {
  'src/product/fg-components.js': [[
    'module.exports = { createFgComponents, mergeUalConfig, validControl, DEFAULT_CONTROL };',
    `// ${marker}\nconst { createSm75Sm86Components, withSm75Sm86Components } = require('./sm75-sm86-mfg');\nmodule.exports = { createFgComponents: (options = {}) => withSm75Sm86Components(\n  createFgComponents(options), createSm75Sm86Components(options), options.detectHardware || detectGpu),\n  mergeUalConfig, validControl, DEFAULT_CONTROL };`
  ]],
  'src/renderer/launch-settings-ui.js': [
    ['    const info = data.fgComponents;',
     `    const info = data.fgComponents;\n    // ${marker}\n    if (info?.backend === 'dlssg-native-sm75-sm86') {\n      if (!scope.Sm75Sm86MfgUI) return '<p class="launch-error">MFG 界面模块未加载，请重新部署完整源码。</p>';\n      return scope.Sm75Sm86MfgUI.components(info, busy);\n    }`],
    ['RTX 20 / 30 尚无已确认的 FG 控制路线。',
     'RTX 20 / 30 请使用上方外部 MFG 组件面板；此处不提供实时倍率控制。'],
    ["        } else if (action === 'prepare') {\n          render();",
     "        } else if (action === 'prepare') {\n          const componentRequest = data?.fgComponents?.backend === 'dlssg-native-sm75-sm86'\n            ? scope.Sm75Sm86MfgUI.request(host, data.fgComponents) : {};\n          render();"],
    ["manager.prepareFgComponents(id, { allowAntiCheat: false })",
     "manager.prepareFgComponents(id, { ...componentRequest, allowAntiCheat: false })"],
    ["manager.prepareFgComponents(id, { allowAntiCheat: true })",
     "manager.prepareFgComponents(id, { ...componentRequest, allowAntiCheat: true })"]
  ],
  'src/renderer/index.html': [[
    '  <script src="launch-settings-ui.js"></script>',
    `  <!-- ${marker} -->\n  <script src="sm75-sm86-mfg-ui.js"></script>\n  <script src="launch-settings-ui.js"></script>`
  ]],
  'src/product/launch-coordinator.js': [
    ["  const hasComponentReceipt = id => fs.existsSync(path.join(service.gameDirectory(id), '_DLSS5_Backup', 'xiaofeng-fg-components.json'));",
     `  // ${marker}
  const hasComponentReceipt = id => ['xiaofeng-fg-components.json', 'xiaofeng-sm75-sm86-mfg.json']
    .some(name => fs.existsSync(path.join(service.gameDirectory(id), '_DLSS5_Backup', name)));`],
    ["      if (fg && fg.mode !== 'restore') {",
     "      if ((fg && fg.mode !== 'restore') || (components.hasNativeReceipt && await components.hasNativeReceipt(id))) {"],
    ["['xiaofeng-launch-settings.json', 'xiaofeng-fg-components.json']",
     "['xiaofeng-launch-settings.json', 'xiaofeng-fg-components.json', 'xiaofeng-sm75-sm86-mfg.json']"]
  ],
  'main.js': [[
    '    const result = await fgComponents.prepare(id, { allowAntiCheat: options?.allowAntiCheat === true });',
    `    // ${marker}\n    const result = await fgComponents.prepare(id, { allowAntiCheat: options?.allowAntiCheat === true,\n      settings: options?.settings, proxy: options?.proxy, previewToken: options?.previewToken });`
  ]]
};
const occurrences = (text, part) => text.split(part).length - 1;
function planSourceEdits(files) {
  const result = {};
  for (const [name, replacements] of Object.entries(edits)) {
    if (typeof files[name] !== 'string') throw new Error(`Missing source: ${name}`);
    const original = files[name], newline = original.includes('\r\n') ? '\r\n' : '\n';
    let value = original.replace(/\r\n/g, '\n');
    if (value.includes(marker)) {
      if (replacements.some(([, after]) => occurrences(value, after) !== 1))
        throw new Error(`Partial/changed integration; review locally, no files changed: ${name}`);
      result[name] = original;
      continue;
    }
    for (const [before, after] of replacements) {
      if (occurrences(value, before) !== 1) throw new Error(`Ambiguous/changed host seam; review locally, no files changed: ${name}`);
      value = value.replace(before, after);
    }
    if (name.endsWith('.js')) new vm.Script(value, { filename: name });
    result[name] = value.replace(/\n/g, newline);
  }
  return result;
}
function readRegular(file) {
  let cursor = path.resolve(file);
  for (;;) {
    const stat = fs.lstatSync(cursor);
    if (stat.isSymbolicLink() || (stat.isFile() && stat.nlink !== 1)) throw new Error(`Refusing linked source: ${file}`);
    const parent = path.dirname(cursor); if (parent === cursor) break; cursor = parent;
  }
  if (!fs.statSync(file).isFile()) throw new Error(`Not a source file: ${file}`);
  return fs.readFileSync(file, 'utf8');
}
function applyToRoot(root, apply = false) {
  root = path.resolve(root);
  for (const file of ['src/product/sm75-sm86-mfg.js', 'src/renderer/sm75-sm86-mfg-ui.js'])
    new vm.Script(readRegular(path.join(root, file)), { filename: file });
  const before = Object.fromEntries(Object.keys(edits).map(file => [file, readRegular(path.join(root, file))]));
  const after = planSourceEdits(before), changed = Object.keys(edits).filter(file => before[file] !== after[file]);
  if (!apply || !changed.length) return { applied: false, changed };
  const temporary = [], written = [];
  try {
    // All anchors/syntax and original bytes are checked before the first write.
    for (const file of Object.keys(edits)) if (readRegular(path.join(root, file)) !== before[file])
      throw new Error(`Concurrent source edit: ${file}`);
    for (const file of changed) {
      const full = path.join(root, file), temp = `${full}.${crypto.randomUUID()}.tmp`;
      fs.writeFileSync(temp, after[file], { flag: 'wx' }); temporary.push(temp);
      if (readRegular(full) !== before[file]) throw new Error(`Concurrent source edit: ${file}`);
      fs.renameSync(temp, full); written.push(file);
    }
    return { applied: true, changed };
  } catch (error) {
    // Revert only our unchanged writes; never overwrite another editor's bytes.
    const retained = [];
    for (const file of written.reverse()) {
      try {
        const full = path.join(root, file);
        if (readRegular(full) === after[file]) fs.writeFileSync(full, before[file]);
        else retained.push(file);
      } catch { retained.push(file); }
    }
    if (retained.length) error.message += `; review retained files: ${retained.join(', ')}`;
    throw error;
  } finally { for (const temp of temporary) { try { fs.unlinkSync(temp); } catch (error) { if (error.code !== 'ENOENT') throw error; } } }
}
if (require.main === module) {
  const args = process.argv.slice(2);
  if (args.some(x => !['--apply', '--check'].includes(x))) {
    console.error('Run from your Manager root: node scripts/integrate-sm75-sm86-mfg.js [--check|--apply]'); process.exitCode = 2;
  } else {
    try { console.log(JSON.stringify(applyToRoot(process.cwd(), args.includes('--apply')), null, 2)); }
    catch (error) { console.error(error.message); process.exitCode = 1; }
  }
}
module.exports = { planSourceEdits, applyToRoot };
