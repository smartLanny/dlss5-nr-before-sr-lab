'use strict';

const fs = require('fs');
const path = require('path');
const { INSTALLED_NAMES } = require('./constants');
const { appError } = require('./errors');
const { assess } = require('./game-support');
const { sha256 } = require('./payload');
const {
  manifestPath, backupPath, readManifest, newManifest, findEntry, validateEntry
} = require('./manifest');

function createInstaller(overrides = {}) {
  const journal = overrides.journal || require('../core/file-journal');
  const scanModule = overrides.scan || require('../core/scan');
  const guards = overrides.guards || require('../core/install-guards');
  const pe = overrides.pe || require('../core/pe');

  const safe = (gameDir, target) => journal.safePath(gameDir, path.relative(gameDir, target));

  function canWrite(dir) {
    const probe = path.join(dir, `.xiaofeng_write_test_${process.pid}_${Date.now()}`);
    try { fs.writeFileSync(probe, 'x'); fs.unlinkSync(probe); return true; }
    catch { return false; }
  }

  function isAddonReShade(file) {
    try {
      const bytes = fs.readFileSync(file);
      return bytes.includes(Buffer.from('ReShade')) && bytes.includes(Buffer.from('Searching for add-ons'));
    } catch { return false; }
  }

  async function copyFile(src, dest) {
    await fs.promises.mkdir(path.dirname(dest), { recursive: true });
    await fs.promises.copyFile(src, dest);
  }

  async function recordOriginal(gameDir, manifest, target, kind) {
    const rel = path.relative(gameDir, target);
    journal.safePath(gameDir, rel);
    let entry = findEntry(manifest, rel);
    if (entry) return entry;

    const existed = fs.existsSync(target);
    entry = {
      rel,
      kind,
      original: { existed, backupRel: null, sha256: existed ? sha256(target) : null },
      installedSha256: null
    };
    if (existed) {
      const backup = backupPath(gameDir, manifest.installId, rel);
      const backupRel = path.relative(gameDir, backup);
      journal.safePath(gameDir, backupRel);
      await journal.capture(gameDir, backup);
      await fs.promises.mkdir(path.dirname(backup), { recursive: true });
      await copyFile(target, backup);
      entry.original.backupRel = backupRel;
    }
    manifest.files.push(entry);
    return entry;
  }

  async function writeManaged(gameDir, manifest, source, target, kind) {
    safe(gameDir, target);
    await journal.capture(gameDir, target);
    const entry = await recordOriginal(gameDir, manifest, target, kind);
    await copyFile(source, target);
    entry.installedSha256 = sha256(target);
    entry.sourceName = path.basename(source);
    manifest.updatedAt = new Date().toISOString();
    return entry;
  }

  async function saveManifest(gameDir, manifest) {
    const file = manifestPath(gameDir);
    await journal.capture(gameDir, file);
    await fs.promises.mkdir(path.dirname(file), { recursive: true });
    await journal.atomicJson(file, manifest);
  }

  function installedFile(exeDir, kind) {
    return path.join(exeDir, INSTALLED_NAMES[kind]);
  }

  function verifyTarget(scan, payload) {
    const support = assess(scan);
    if (!support.supported) throw appError(support.code);
    if (!payload || !payload.addon || !payload.bridge || !payload.runtime || !payload.config || !payload.reshade) {
      throw appError('ERR_PAYLOAD_MISSING');
    }
    for (const kind of ['reshade', 'addon', 'bridge', 'runtime']) {
      if (pe.getBitness(payload[kind].file) !== 64) {
        throw appError('ERR_PAYLOAD_HASH', { file: payload[kind].name, reason: 'not-x64' });
      }
    }
    if (!isAddonReShade(payload.reshade.file)) throw appError('ERR_PAYLOAD_HASH', { file: payload.reshade.name });
  }

  function existingProxyConflict(exeDir, reshade) {
    const target = path.join(exeDir, INSTALLED_NAMES.reshade);
    return fs.existsSync(target) && (!reshade.installed || path.basename(reshade.file || '').toLowerCase() !== INSTALLED_NAMES.reshade);
  }

  async function install({ gameDir, payload, scan: suppliedScan }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    verifyTarget(scan, payload);
    const exePath = scan.chosen.path;
    const exeDir = path.dirname(exePath);
    if (!canWrite(exeDir)) throw appError('ERR_NO_WRITE_ACCESS');
    if (guards.antiCheatPresent(gameDir)) throw appError('ERR_ANTI_CHEAT');
    await guards.assertGameClosed(gameDir, exePath);

    return journal.transaction(gameDir, async () => {
      let manifest = readManifest(gameDir) || newManifest(gameDir, exePath, scan.chosen.api);
      const reshade = scanModule.inspectReShade(exeDir);
      if (existingProxyConflict(exeDir, reshade)) throw appError('ERR_RESHADER_CONFLICT');
      if (reshade.installed && !reshade.addonSupport) throw appError('ERR_RESHADER_NO_ADDON');

      if (!reshade.installed) {
        await writeManaged(gameDir, manifest, payload.reshade.file,
          installedFile(exeDir, 'reshade'), 'reshade');
      }
      await writeManaged(gameDir, manifest, payload.addon.file,
        installedFile(exeDir, 'addon'), 'addon');
      await writeManaged(gameDir, manifest, payload.bridge.file,
        installedFile(exeDir, 'bridge'), 'bridge');
      await writeManaged(gameDir, manifest, payload.runtime.file,
        installedFile(exeDir, 'runtime'), 'runtime');

      const configTarget = installedFile(exeDir, 'config');
      if (!fs.existsSync(configTarget)) {
        await writeManaged(gameDir, manifest, payload.config.file, configTarget, 'config');
      }

      // If a previous ReShade session disabled this add-on, remove only this
      // add-on's stem from DisabledAddons and leave every other setting intact.
      const reshadeIni = path.join(exeDir, 'ReShade.ini');
      if (fs.existsSync(reshadeIni)) {
        const text = fs.readFileSync(reshadeIni, 'utf8');
        const stem = path.basename(INSTALLED_NAMES.addon).replace(/\.addon(?:32|64)?$/i, '');
        const match = text.match(/^DisabledAddons=(.*)$/m);
        if (match && match[1].toLowerCase().includes(stem.toLowerCase())) {
          await journal.capture(gameDir, reshadeIni);
          const kept = match[1].split(',').filter(name => !name.toLowerCase().includes(stem.toLowerCase()));
          const next = text.replace(/^DisabledAddons=.*$/m, `DisabledAddons=${kept.join(',')}`);
          await fs.promises.writeFile(reshadeIni, next, 'utf8');
        }
      }

      await saveManifest(gameDir, manifest);
      return diagnose({ gameDir, payload, scan });
    });
  }

  async function diagnose({ gameDir, payload, scan: suppliedScan }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    const manifest = readManifest(gameDir);
    const support = assess(scan);
    const exeDir = scan && scan.chosen ? path.dirname(scan.chosen.path) : gameDir;
    const components = [];
    const expected = payload || {};

    const reshade = scan && scan.chosen ? scanModule.inspectReShade(exeDir) : { installed: false, addonSupport: false };
    components.push({
      key: 'reshade', label: 'ReShade Add-on 运行环境',
      ok: Boolean(reshade.installed && reshade.addonSupport),
      detail: reshade.installed && !reshade.addonSupport ? '已安装，但不支持 Add-on' : null
    });

    for (const kind of ['addon', 'bridge', 'runtime', 'config']) {
      const file = installedFile(exeDir, kind);
      const exists = fs.existsSync(file);
      const expectedHash = expected[kind] && expected[kind].actual;
      const actual = exists && kind !== 'config' ? sha256(file) : null;
      components.push({
        key: kind,
        label: { addon: 'NR→SR 插件', bridge: 'NR Bridge', runtime: 'AI Runtime', config: '配置文件' }[kind],
        ok: exists && (kind === 'config' || !expectedHash || actual === expectedHash),
        detail: !exists ? '文件缺失' : (kind !== 'config' && expectedHash && actual !== expectedHash ? '版本或校验不一致' : null)
      });
    }

    return {
      installed: Boolean(manifest),
      complete: Boolean(manifest) && components.every(row => row.ok),
      supported: support.supported,
      supportCode: support.code,
      manifest: manifest ? { installedAt: manifest.installedAt, updatedAt: manifest.updatedAt } : null,
      components
    };
  }

  async function repair({ gameDir, payload, scan: suppliedScan }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    if (!readManifest(gameDir)) throw appError('ERR_NOT_INSTALLED');
    return install({ gameDir, payload, scan });
  }

  async function uninstall({ gameDir, removeSettings = false, scan: suppliedScan }) {
    const manifest = readManifest(gameDir);
    if (!manifest) throw appError('ERR_NOT_INSTALLED');
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    const exePath = scan.chosen ? scan.chosen.path : path.join(gameDir, manifest.game.exe);
    await guards.assertGameClosed(gameDir, exePath);

    const warnings = [];
    return journal.transaction(gameDir, async () => {
      for (const row of manifest.files) validateEntry(gameDir, manifest, row, journal.safePath);
      for (const row of [...manifest.files].reverse()) {
        if (row.kind === 'config' && !removeSettings && !row.original.existed) continue;
        const target = journal.safePath(gameDir, row.rel);
        await journal.capture(gameDir, target);
        if (row.original.existed) {
          const backup = journal.safePath(gameDir, row.original.backupRel);
          await copyFile(backup, target);
        } else if (fs.existsSync(target)) {
          const current = fs.statSync(target).isFile() ? sha256(target) : null;
          if (row.installedSha256 && current !== row.installedSha256 && row.kind !== 'config') {
            warnings.push({ code: 'ERR_FILE_CHANGED', rel: row.rel });
            continue;
          }
          await fs.promises.unlink(target);
        }
      }
      if (!warnings.length) {
        const file = manifestPath(gameDir);
        await journal.capture(gameDir, file);
        if (fs.existsSync(file)) await fs.promises.unlink(file);
        const originals = path.join(gameDir, '_DLSS5_Backup', 'xiaofeng-originals', manifest.installId);
        await fs.promises.rm(originals, { recursive: true, force: true });
      } else {
        await saveManifest(gameDir, manifest);
      }
      return { removed: warnings.length === 0, settingsKept: !removeSettings, warnings };
    });
  }

  return { install, repair, uninstall, diagnose, isAddonReShade };
}

module.exports = { createInstaller };
