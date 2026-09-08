'use strict';
const fs = require('fs/promises');
const path = require('path');
const crypto = require('crypto');

function fail(code, message, details) {
  throw Object.assign(new Error(message), { code, details });
}
function sha256(data) { return crypto.createHash('sha256').update(data).digest('hex'); }
function inside(parent, child) {
  const rel = path.relative(path.resolve(parent), path.resolve(child));
  return rel === '' || (rel !== '..' && !rel.startsWith(`..${path.sep}`) && !path.isAbsolute(rel));
}
async function noLinks(target) {
  const full = path.resolve(target), parsed = path.parse(full); let current = parsed.root;
  for (const part of full.slice(parsed.root.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, part);
    try {
      const stat = await fs.lstat(current);
      if (stat.isSymbolicLink() || (stat.isFile() && stat.nlink > 1)) fail('SETTINGS_LINK_BLOCKED', '检测到链接或硬链接，未修改配置。');
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
}
async function digestFile(file) {
  try { return sha256(await fs.readFile(file)); } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}
async function atomicJson(file, value) {
  await fs.mkdir(path.dirname(file), { recursive: true });
  const temp = `${file}.tmp`;
  await fs.writeFile(temp, `${JSON.stringify(value, null, 2)}\n`, 'utf8');
  await fs.rename(temp, file);
}
module.exports = { fail, sha256, inside, noLinks, digestFile, atomicJson };
