'use strict';
const path = require('node:path');
const { addonValues } = require('./reshade-layout');

const PATH_KEYS = new Set([
  'GENERAL/EffectSearchPaths', 'GENERAL/TextureSearchPaths', 'GENERAL/PresetPath',
  'GENERAL/StartupPresetPath', 'GENERAL/IntermediateCachePath', 'GENERAL/ScreenshotPath',
  'SCREENSHOT/SavePath', 'SCREENSHOT/PostSaveCommand'
]);
const scalar = value => String(value).replace(/,/g, ',,');
function lines(text) { return String(text || '').split(/(?<=\n)/); }
function entries(text) {
  let section = '', counts = new Map();
  return lines(text).map((line, index) => {
    const header = line.replace(/^\uFEFF/, '').match(/^[ \t]*\[([^\]]+)\]/);
    if (header) section = header[1].trim();
    const match = line.match(/^([ \t]*([^=;\s][^=]*?)[ \t]*=[ \t]*)(.*?)(\r?\n|$)$/);
    if (!match || /^[;#\/]/.test(line.trim())) return { line, index, section };
    const key = match[2].trim(), identity = section + '/' + key;
    const occurrence = counts.get(identity) || 0; counts.set(identity, occurrence + 1);
    return { line, index, section, key, identity, occurrence, prefix: match[1], value: match[3], end: match[4] };
  });
}
function setScalar(text, section, key, value) {
  const rows = entries(text), wanted = rows.filter(row => row.section === section && row.key === key);
  if (wanted.length) {
    const first = wanted[0].index;
    return rows.map(row => row.section === section && row.key === key
      ? row.index === first && value !== null ? row.prefix + scalar(value) + row.end : ''
      : row.line).join('');
  }
  if (value === null) return text;
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  const header = rows.find(row => row.section === section && /^\s*\[/.test(row.line.replace(/^\uFEFF/, '')));
  if (header) {
    rows[header.index].line = rows[header.index].line.replace(/(?:\r?\n)?$/, eol) + key + '=' + scalar(value) + eol;
    return rows.map(row => row.line).join('');
  }
  return text + (text && !text.endsWith('\n') ? eol : '') + '[' + section + ']' + eol + key + '=' + scalar(value) + eol;
}
function expandEnvironment(value, environment = process.env) {
  return value.replace(/%([^%]+)%/g, (whole, key) => environment[key] || whole);
}
function externalConfig(original, oldBase, environment = process.env) {
  const rewritten = entries(original).map(row => {
    if (!PATH_KEYS.has(row.identity) || row.identity === 'SCREENSHOT/PostSaveCommand') return row.line;
    const values = addonValues('[' + row.section + ']\n' + row.key + '=' + row.value, row.section).get(row.key);
    if (!values) return row.line;
    const mapped = values.map(value => {
      if (!value || /^[A-Za-z][A-Za-z0-9+.-]*:\/\//.test(value)) return value;
      const expanded = expandEnvironment(value, environment);
      return path.isAbsolute(expanded) ? expanded : path.resolve(oldBase, expanded);
    });
    return row.prefix + mapped.map(scalar).join(',') + row.end;
  }).join('');
  return setScalar(setScalar(rewritten, 'INSTALL', 'BasePath', null), 'ADDON', 'AddonPath', '.');
}
function localConfig(current, original, oldBase, environment = process.env) {
  // Restore the original spelling only for path fields the user did not edit.
  // Other settings and comments from the active external INI survive migration.
  const expected = entries(externalConfig(original, oldBase, environment));
  const before = new Map(entries(original).filter(row => row.key).map(row => [row.identity + ':' + row.occurrence, row]));
  const rebased = new Map(expected.filter(row => row.key).map(row => [row.identity + ':' + row.occurrence, row]));
  let restored = entries(current).map(row => {
    if (!row.key) return row.line;
    const id = row.identity + ':' + row.occurrence, a = before.get(id), b = rebased.get(id);
    return a && b && row.line === b.line && PATH_KEYS.has(row.identity) ? a.line : row.line;
  }).join('');
  const originalAddon = addonValues(original).get('AddonPath')?.[0];
  restored = setScalar(restored, 'ADDON', 'AddonPath', originalAddon || '.');
  restored = setScalar(restored, 'INSTALL', 'BasePath', null);
  // Avoid adding a synthetic ADDON section when the original and active file
  // have no other changes: a complete migration round trip keeps exact bytes.
  if (current === externalConfig(original, oldBase, environment)) return original;
  return restored;
}
function loaderConfig(original, runtimeDir) {
  return setScalar(original, 'INSTALL', 'BasePath', runtimeDir);
}
module.exports = { externalConfig, localConfig, loaderConfig, setScalar, PATH_KEYS };
