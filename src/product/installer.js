'use strict';

const fs = require('fs');
const path = require('path');
const { INSTALLED_NAMES, DX11_COMPAT_VERSION } = require('./constants');
const { appError } = require('./errors');
const { assess } = require('./game-support');
const { sha256 } = require('./payload');
const { scanConflicts, moveConflicts, restoreConflicts } = require('./conflicts');
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

  async function sidecarBackup(target) {
    if (!fs.existsSync(target) || !fs.statSync(target).isFile()) return null;
    const originalHash = sha256(target);
    for (let index = 0; index < 16; index += 1) {
      const suffix = index === 0 ? '.bak' : `.bak.${index}`;
      const candidate = `${target}${suffix}`;
      if (fs.existsSync(candidate)) {
        if (fs.statSync(candidate).isFile() && sha256(candidate) === originalHash) return path.basename(candidate);
        continue;
      }
      await copyFile(target, candidate);
      if (sha256(candidate) !== originalHash) throw appError('ERR_BACKUP_INVALID');
      return path.basename(candidate);
    }
    throw appError('ERR_BACKUP_INVALID');
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
    const sidecar = await sidecarBackup(target);
    await journal.capture(gameDir, target);
    const entry = await recordOriginal(gameDir, manifest, target, kind);
    await copyFile(source, target);
    entry.installedSha256 = sha256(target);
    entry.sourceName = path.basename(source);
    if (sidecar) entry.sidecarBak = sidecar;
    manifest.updatedAt = new Date().toISOString();
    return entry;
  }

  async function removeManaged(gameDir, manifest, target, kind) {
    const rel = path.relative(gameDir, target);
    const entry = findEntry(manifest, rel);
    if (!entry || (kind && entry.kind !== kind)) return;
    safe(gameDir, target);
    await journal.capture(gameDir, target);
    if (entry.original.existed) {
      const backup = journal.safePath(gameDir, entry.original.backupRel);
      await copyFile(backup, target);
    } else if (fs.existsSync(target)) {
      const current = fs.statSync(target).isFile() ? sha256(target) : null;
      if (entry.installedSha256 && current !== entry.installedSha256) {
        throw appError('ERR_FILE_CHANGED', { rel });
      }
      await fs.promises.unlink(target);
    }
    manifest.files = manifest.files.filter(row => row !== entry);
    manifest.updatedAt = new Date().toISOString();
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
    const allowDx11 = Boolean(payload && payload.versionInfo && payload.versionInfo.compatibility === 'dx11');
    const support = assess(scan, { allowDx11 });
    if (!support.supported) throw appError(support.code);
    if (!payload || !payload.addon || !payload.bridge || !payload.runtime || !payload.config || !payload.reshade) {
      throw appError('ERR_PAYLOAD_MISSING');
    }
    for (const kind of ['reshade', 'addon', 'bridge', 'runtime']) {
      if (pe.getBitness(payload[kind].file) !== 64) {
        throw appError('ERR_PAYLOAD_HASH', { file: payload[kind].name, reason: 'not-x64' });
      }
    }
    if (payload.carrier && pe.getBitness(payload.carrier.file) !== 64) {
      throw appError('ERR_PAYLOAD_HASH', { file: payload.carrier.name, reason: 'not-x64' });
    }
    if (!isAddonReShade(payload.reshade.file)) throw appError('ERR_PAYLOAD_HASH', { file: payload.reshade.name });
  }

  function existingProxyConflict(exeDir, reshade) {
    const target = path.join(exeDir, INSTALLED_NAMES.reshade);
    return fs.existsSync(target) && (!reshade.installed || path.basename(reshade.file || '').toLowerCase() !== INSTALLED_NAMES.reshade);
  }

  async function install({ gameDir, payload, scan: suppliedScan, allowAntiCheat = false }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    verifyTarget(scan, payload);
    const exePath = scan.chosen.path;
    const exeDir = path.dirname(exePath);
    if (!canWrite(exeDir)) throw appError('ERR_NO_WRITE_ACCESS');
    if (guards.antiCheatPresent(gameDir) && allowAntiCheat !== true) {
      throw appError('ERR_ANTI_CHEAT_CONFIRM', { operation: 'install' });
    }
    await guards.assertGameClosed(gameDir, exePath);

    return journal.transaction(gameDir, async () => {
      let manifest = readManifest(gameDir) || newManifest(gameDir, exePath, scan.chosen.api);
      let movedConflicts = [];
      try {
        const reshade = scanModule.inspectReShade(exeDir);
        if (existingProxyConflict(exeDir, reshade)) throw appError('ERR_RESHADER_CONFLICT');
        if (reshade.installed && !reshade.addonSupport) throw appError('ERR_RESHADER_NO_ADDON');

        // Move only known DLSS/NR/NGX add-ons into a reversible manager backup.
        // ReShade itself, game DLSS DLLs and ordinary RenoDX add-ons are never
        // part of this list.
        movedConflicts = await moveConflicts(gameDir, manifest.installId, {
          exclude: manifest.files.map(row => row.rel)
        });
        manifest.conflicts = [...(manifest.conflicts || []), ...movedConflicts];
        manifest.payloadVersion = payload.version || null;
        manifest.hardwareFamily = payload.hardwareFamily || null;

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

        if (payload.carrier) {
          await writeManaged(gameDir, manifest, payload.carrier.file,
            installedFile(exeDir, 'carrier'), 'carrier');
        } else {
          await removeManaged(gameDir, manifest, installedFile(exeDir, 'carrier'), 'carrier');
        }

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
        return diagnose({ gameDir, payload, scan, payloadVersion: payload.version });
      } catch (error) {
        if (movedConflicts.length) await restoreConflicts(gameDir, movedConflicts);
        throw error;
      }
    });
  }

  function verifyAddon(addon) {
    if (!addon || typeof addon.file !== 'string' || !fs.existsSync(addon.file) ||
        !fs.statSync(addon.file).isFile() || !/\.addon64$/i.test(addon.file)) {
      throw appError('ERR_ADDON_INVALID');
    }
    if (addon.bridgeFile && (!fs.existsSync(addon.bridgeFile) || !fs.statSync(addon.bridgeFile).isFile())) {
      throw appError('ERR_ADDON_INVALID');
    }
  }

  async function diagnose({ gameDir, payload, payloadVersion, payloadVersionLabel, scan: suppliedScan }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    const manifest = readManifest(gameDir);
    const support = assess(scan, {
      allowDx11: Boolean(
        (payload && payload.versionInfo && payload.versionInfo.compatibility === 'dx11') ||
        (manifest && manifest.payloadVersion === DX11_COMPAT_VERSION)
      )
    });
    const exeDir = scan && scan.chosen ? path.dirname(scan.chosen.path) : gameDir;
    const components = [];
    const expected = payload || {};

    const reshade = scan && scan.chosen ? scanModule.inspectReShade(exeDir) : { installed: false, addonSupport: false };
    components.push({
      key: 'reshade', label: 'ReShade Add-on 运行环境',
      ok: Boolean(reshade.installed && reshade.addonSupport),
      detail: reshade.installed && !reshade.addonSupport ? '已安装，但不支持 Add-on' : null
    });

    const managedAddon = manifest && manifest.files.find(row => row.kind === 'addon');
    const activeConflicts = scanConflicts(gameDir, { exclude: managedAddon ? [managedAddon.rel] : [] });
    const savedConflicts = manifest && Array.isArray(manifest.conflicts) ? manifest.conflicts : [];
    components.push({
      key: 'conflicts', label: '冲突 Addon',
      ok: activeConflicts.length === 0,
      detail: activeConflicts.length
        ? `发现 ${activeConflicts.length} 个，点击修复会移出并备份`
        : (savedConflicts.length ? `已移出并保留 ${savedConflicts.length} 个备份` : '未发现需要移出的冲突插件')
    });

    if (manifest && payloadVersion) {
      components.push({
        key: 'version', label: 'Addon 版本',
        ok: manifest.payloadVersion === payloadVersion,
        detail: manifest.payloadVersion === payloadVersion
          ? `当前为 ${payloadVersionLabel || payloadVersion}`
          : `已安装 ${manifest.payloadVersion || '旧版本'}，当前选择 ${payloadVersionLabel || payloadVersion}；点击修复切换`
      });
    }

    const kinds = ['addon', 'bridge', 'runtime', 'config'];
    if (manifest && manifest.files.some(row => row.kind === 'carrier')) kinds.splice(2, 0, 'carrier');
    for (const kind of kinds) {
      const file = installedFile(exeDir, kind);
      const exists = fs.existsSync(file);
      const expectedHash = expected[kind] && expected[kind].actual;
      const actual = exists && kind !== 'config' ? sha256(file) : null;
      components.push({
        key: kind,
        label: { addon: 'NR→SR 插件', bridge: 'NR Bridge', carrier: 'DX11 Native Carrier', runtime: 'AI Runtime', config: '配置文件' }[kind],
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

  async function repair({ gameDir, payload, scan: suppliedScan, allowAntiCheat = false }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    if (!readManifest(gameDir)) throw appError('ERR_NOT_INSTALLED');
    return install({ gameDir, payload, scan, allowAntiCheat });
  }

  async function toggleD3D12({ gameDir, enabled, scan: suppliedScan, allowAntiCheat = false }) {
    const manifest = readManifest(gameDir);
    if (!manifest) throw appError('ERR_NOT_INSTALLED');
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    if (!scan || !scan.chosen || !scan.chosen.path) throw appError('ERR_NO_GAME_EXE');
    const exePath = scan.chosen.path;
    const exeDir = path.dirname(exePath);
    const dxgi = path.join(exeDir, INSTALLED_NAMES.reshade);
    const d3d12 = path.join(exeDir, 'd3d12.dll');
    const current = manifest.reshadeRoute === 'd3d12' ? 'd3d12' : 'dxgi';
    const want = enabled === true;
    if ((want && current === 'd3d12') || (!want && current === 'dxgi')) return diagnose({ gameDir, payload: null, payloadVersion: manifest.payloadVersion, scan });
    if (!canWrite(exeDir)) throw appError('ERR_NO_WRITE_ACCESS');
    if (guards.antiCheatPresent(gameDir) && allowAntiCheat !== true) {
      throw appError('ERR_ANTI_CHEAT_CONFIRM', { operation: 'd3d12' });
    }
    await guards.assertGameClosed(gameDir, exePath);

    return journal.transaction(gameDir, async () => {
      if (want) {
        if (!fs.existsSync(dxgi)) throw appError('ERR_NOT_INSTALLED');
        if (fs.existsSync(d3d12)) throw appError('ERR_D3D12_CONFLICT');
        await sidecarBackup(dxgi);
        await journal.capture(gameDir, dxgi);
        await journal.capture(gameDir, d3d12);
        await fs.promises.rename(dxgi, d3d12);
        manifest.reshadeRoute = 'd3d12';
      } else {
        if (!fs.existsSync(d3d12)) throw appError('ERR_NOT_INSTALLED');
        if (fs.existsSync(dxgi)) throw appError('ERR_D3D12_CONFLICT');
        await sidecarBackup(d3d12);
        await journal.capture(gameDir, d3d12);
        await journal.capture(gameDir, dxgi);
        await fs.promises.rename(d3d12, dxgi);
        manifest.reshadeRoute = 'dxgi';
      }
      manifest.updatedAt = new Date().toISOString();
      await saveManifest(gameDir, manifest);
      return diagnose({ gameDir, payload: null, payloadVersion: manifest.payloadVersion, scan });
    });
  }

  async function upgradeAddon({ gameDir, addon, version, scan: suppliedScan, allowAntiCheat = false }) {
    verifyAddon(addon);
    const manifest = readManifest(gameDir);
    if (!manifest) throw appError('ERR_NOT_INSTALLED');
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    const allowDx11 = addon.compatibility === 'dx11' ||
      (!addon.compatibility && manifest.payloadVersion === DX11_COMPAT_VERSION);
    const support = assess(scan, { allowDx11 });
    if (!support.supported) throw appError(support.code);
    const exePath = scan.chosen && scan.chosen.path
      ? scan.chosen.path
      : path.join(gameDir, manifest.game.exe);
    const exeDir = path.dirname(exePath);
    const target = installedFile(exeDir, 'addon');
    if (!canWrite(exeDir)) throw appError('ERR_NO_WRITE_ACCESS');
    if (guards.antiCheatPresent(gameDir) && allowAntiCheat !== true) {
      throw appError('ERR_ANTI_CHEAT_CONFIRM', { operation: 'upgrade-addon' });
    }
    await guards.assertGameClosed(gameDir, exePath);

    return journal.transaction(gameDir, async () => {
      const movedConflicts = [];
      try {
        const found = await moveConflicts(gameDir, manifest.installId, {
          exclude: manifest.files.map(row => row.rel)
        });
        movedConflicts.push(...found);
        manifest.conflicts = [...(manifest.conflicts || []), ...found];
        await writeManaged(gameDir, manifest, addon.file, target, 'addon');
        if (addon.bridgeFile) {
          await writeManaged(gameDir, manifest, addon.bridgeFile,
            installedFile(exeDir, 'bridge'), 'bridge');
        }
        await removeManaged(gameDir, manifest, path.join(exeDir, INSTALLED_NAMES.carrier), 'carrier');
        manifest.payloadVersion = version || addon.id || null;
        manifest.updatedAt = new Date().toISOString();
        await saveManifest(gameDir, manifest);
        return diagnose({
          gameDir,
          payload: { addon: { file: target, actual: sha256(target), name: path.basename(target) } },
          payloadVersion: manifest.payloadVersion,
          scan
        });
      } catch (error) {
        if (movedConflicts.length) await restoreConflicts(gameDir, movedConflicts);
        throw error;
      }
    });
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
      for (const row of manifest.conflicts || []) {
        journal.safePath(gameDir, row.sourceRel);
        journal.safePath(gameDir, row.backupRel);
      }
      for (const row of [...manifest.files].reverse()) {
        if (row.kind === 'config' && !removeSettings && !row.original.existed) continue;
        const routeRel = row.kind === 'reshade' && manifest.reshadeRoute === 'd3d12'
          ? path.relative(gameDir, path.join(path.dirname(exePath), 'd3d12.dll'))
          : row.rel;
        const target = journal.safePath(gameDir, routeRel);
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
      warnings.push(...await restoreConflicts(gameDir, manifest.conflicts));
      if (!warnings.length) {
        const file = manifestPath(gameDir);
        await journal.capture(gameDir, file);
        if (fs.existsSync(file)) await fs.promises.unlink(file);
        const originals = path.join(gameDir, '_DLSS5_Backup', 'xiaofeng-originals', manifest.installId);
        await fs.promises.rm(originals, { recursive: true, force: true });
        const conflicts = path.join(gameDir, '_DLSS5_Backup', 'conflicts', manifest.installId);
        await fs.promises.rm(conflicts, { recursive: true, force: true });
      } else {
        await saveManifest(gameDir, manifest);
      }
      return { removed: warnings.length === 0, settingsKept: !removeSettings, warnings };
    });
  }

  return { install, repair, upgradeAddon, toggleD3D12, uninstall, diagnose, isAddonReShade };
}

module.exports = { createInstaller };
