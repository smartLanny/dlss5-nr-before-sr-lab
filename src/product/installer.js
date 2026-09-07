'use strict';

const fs = require('fs');
const path = require('path');
const { INSTALLED_NAMES, DX11_COMPAT_VERSION } = require('./constants');
const { appError } = require('./errors');
const { assess, classifyApi, isDx11Only } = require('./game-support');
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

  async function sidecarBackup(gameDir, target) {
    if (!fs.existsSync(target) || !fs.statSync(target).isFile()) return null;
    const originalHash = sha256(target);
    for (let index = 0; index < 16; index += 1) {
      const suffix = index === 0 ? '.bak' : `.bak.${index}`;
      const candidate = `${target}${suffix}`;
      if (fs.existsSync(candidate)) {
        if (fs.statSync(candidate).isFile() && sha256(candidate) === originalHash) return path.basename(candidate);
        continue;
      }
      await journal.capture(gameDir, candidate);
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
    const existing = findEntry(manifest, path.relative(gameDir, target));
    if (existing && fs.existsSync(target)) {
      const current = fs.statSync(target).isFile() ? sha256(target) : null;
      if (!existing.installedSha256 || current !== existing.installedSha256) {
        throw appError('ERR_FILE_CHANGED', { rel: existing.rel });
      }
    }
    const sidecar = await sidecarBackup(gameDir, target);
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
    if (fs.existsSync(target)) {
      const current = fs.statSync(target).isFile() ? sha256(target) : null;
      if (!entry.installedSha256 || current !== entry.installedSha256) {
        throw appError('ERR_FILE_CHANGED', { rel });
      }
    }
    await journal.capture(gameDir, target);
    if (entry.original.existed) {
      const backup = journal.safePath(gameDir, entry.original.backupRel);
      if (!entry.original.sha256 || !fs.existsSync(backup) || sha256(backup) !== entry.original.sha256) {
        throw appError('ERR_BACKUP_INVALID', { rel });
      }
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

  async function retireManagedCarriers(gameDir, manifest, keepTarget = null) {
    const keep = keepTarget ? path.normalize(path.relative(gameDir, keepTarget)).toLowerCase() : null;
    const obsolete = manifest.files.filter(row => row.kind === 'carrier' &&
      (!keep || path.normalize(row.rel).toLowerCase() !== keep));
    for (const row of obsolete) {
      await removeManaged(gameDir, manifest, path.join(gameDir, row.rel), 'carrier');
    }
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
    const carrierRequired = isDx11Only(scan && scan.chosen);
    const allowDx11 = Boolean(payload && payload.versionInfo && payload.versionInfo.compatibility === 'dx11');
    const support = assess(scan, { allowDx11 });
    if (!support.supported) throw appError(support.code);
    if (!payload || !payload.addon || !payload.bridge || !payload.runtime || !payload.config || !payload.reshade) {
      throw appError('ERR_PAYLOAD_MISSING');
    }
    if (carrierRequired && !payload.carrier) throw appError('ERR_PAYLOAD_MISSING', { file: INSTALLED_NAMES.carrier });
    for (const kind of ['reshade', 'addon', 'bridge', 'runtime']) {
      if (pe.getBitness(payload[kind].file) !== 64) {
        throw appError('ERR_PAYLOAD_HASH', { file: payload[kind].name, reason: 'not-x64' });
      }
    }
    if (carrierRequired && pe.getBitness(payload.carrier.file) !== 64) {
      throw appError('ERR_PAYLOAD_HASH', { file: payload.carrier.name, reason: 'not-x64' });
    }
    if (!isAddonReShade(payload.reshade.file)) throw appError('ERR_PAYLOAD_HASH', { file: payload.reshade.name });
  }

  function existingProxyConflict(exeDir, reshade) {
    const target = path.join(exeDir, INSTALLED_NAMES.reshade);
    return fs.existsSync(target) && (!reshade.installed || path.basename(reshade.file || '').toLowerCase() !== INSTALLED_NAMES.reshade);
  }

  function splitDisabledAddons(value) {
    const items = [];
    let current = '';
    for (let index = 0; index < value.length; index += 1) {
      if (value[index] !== ',') { current += value[index]; continue; }
      if (value[index + 1] === ',') { current += ',,'; index += 1; continue; }
      items.push(current);
      current = '';
    }
    items.push(current);
    return items;
  }

  function editCarrierDisabled(text, remove = false) {
    const carrierFile = path.basename(INSTALLED_NAMES.carrier);
    // Fixed 28aed409 paired carrier NAME export; do not load an add-on to discover it.
    const carrierRegisteredName = 'DLSS 5 Bridge 1.4.12';
    let section = '';
    let found = false;
    let nameOnly = false;
    let changed = false;
    const lines = String(text || '').split('\n').map(line => {
      // ReShade trims ASCII whitespace and accepts text after the first ']'.
      // A UTF-8 BOM is consumed before parsing the first physical line.
      const header = line.match(/^(?:\uFEFF)?[ \t]*\[([^\]]+)\]/);
      if (header) {
        section = header[1].replace(/^[ \t]+|[ \t]+$/g, '');
        return line;
      }
      if (section !== 'ADDON') return line;
      const match = line.match(/^([ \t]*DisabledAddons[ \t]*=[ \t]*)(.*?)(\r?)$/);
      if (!match) return line;
      // ReShade trims the complete value before splitting it. Spaces inside
      // comma-separated elements remain part of the name or filename.
      const valueEnd = match[2].search(/[ \t]*$/);
      const valueTail = match[2].slice(valueEnd);
      const items = splitDisabledAddons(match[2].slice(0, valueEnd));
      let lineChanged = false;
      const kept = items.filter(item => {
        const value = item;
        if (value === carrierRegisteredName) {
          found = true;
          nameOnly = true;
          return true;
        }
        // ReShade compares the filename after the first '@' byte-for-byte.
        const at = value.indexOf('@');
        const disablesCarrier = at >= 0 && value.slice(at + 1) === carrierFile;
        if (!disablesCarrier) return true;
        found = true;
        if (remove) { changed = true; lineChanged = true; }
        return !remove;
      });
      return lineChanged ? `${match[1]}${kept.join(',')}${valueTail}${match[3]}` : line;
    });
    return { found, nameOnly, changed, text: lines.join('\n') };
  }

  async function enableCarrier(gameDir, exeDir) {
    const file = path.join(exeDir, 'ReShade.ini');
    if (!fs.existsSync(file)) return;
    const edited = editCarrierDisabled(fs.readFileSync(file, 'utf8'), true);
    if (!edited.changed) return;
    await sidecarBackup(gameDir, file);
    await journal.capture(gameDir, file);
    await fs.promises.writeFile(file, edited.text, 'utf8');
  }

  async function install({ gameDir, payload, scan: suppliedScan, allowAntiCheat = false }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    verifyTarget(scan, payload);
    const exePath = scan.chosen.path;
    const exeDir = path.dirname(exePath);
    const carrierRequired = isDx11Only(scan.chosen);
    if (!canWrite(exeDir)) throw appError('ERR_NO_WRITE_ACCESS');
    if (guards.antiCheatPresent(gameDir) && allowAntiCheat !== true) {
      throw appError('ERR_ANTI_CHEAT_CONFIRM', { operation: 'install' });
    }
    await guards.assertGameClosed(gameDir, exePath);

    return journal.transaction(gameDir, async () => {
      let manifest = readManifest(gameDir) || newManifest(gameDir, exePath, scan.chosen.api);
      try {
        const reshade = scanModule.inspectReShade(exeDir);
        if (existingProxyConflict(exeDir, reshade)) throw appError('ERR_RESHADER_CONFLICT');
        if (reshade.installed && !reshade.addonSupport) throw appError('ERR_RESHADER_NO_ADDON');

        // Retire a carrier managed under an older filename first. If that
        // restores a pre-existing carrier, the conflict pass below moves it
        // aside before the matched compatibility set is installed.
        const carrierTarget = installedFile(exeDir, 'carrier');
        await retireManagedCarriers(gameDir, manifest, carrierRequired ? carrierTarget : null);

        // Move only known DLSS/NR/NGX add-ons into a reversible manager backup.
        // ReShade itself, game DLSS DLLs and ordinary RenoDX add-ons are never
        // part of this list.
        const movedConflicts = await moveConflicts(gameDir, manifest.installId, {
          exclude: manifest.files.map(row => row.rel),
          capture: target => journal.capture(gameDir, target)
        });
        manifest.conflicts = [...(manifest.conflicts || []), ...movedConflicts];
        manifest.payloadVersion = payload.version || null;
        manifest.hardwareFamily = payload.hardwareFamily || null;
        manifest.deploymentApi = classifyApi(scan.chosen);

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

        if (carrierRequired) {
          await writeManaged(gameDir, manifest, payload.carrier.file,
            carrierTarget, 'carrier');
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
        if (carrierRequired) await enableCarrier(gameDir, exeDir);

        await saveManifest(gameDir, manifest);
        return diagnose({ gameDir, payload, scan, payloadVersion: payload.version });
      } catch (error) {
        throw error;
      }
    });
  }

  function verifyAddon(addon, options = {}) {
    if (!addon || typeof addon.file !== 'string' || !fs.existsSync(addon.file) ||
        !fs.statSync(addon.file).isFile() || !/\.addon64$/i.test(addon.file)) {
      throw appError('ERR_ADDON_INVALID');
    }
    if (addon.bridgeFile && (!fs.existsSync(addon.bridgeFile) || !fs.statSync(addon.bridgeFile).isFile())) {
      throw appError('ERR_ADDON_INVALID');
    }
    if (addon.carrierFile && (!fs.existsSync(addon.carrierFile) || !fs.statSync(addon.carrierFile).isFile() ||
        !/\.addon64$/i.test(addon.carrierFile))) {
      throw appError('ERR_ADDON_INVALID');
    }
    if (options.carrierRequired && !addon.bridgeFile) {
      throw appError('ERR_PAYLOAD_MISSING', { file: INSTALLED_NAMES.bridge });
    }
    if (options.carrierRequired && !addon.carrierFile) {
      throw appError('ERR_PAYLOAD_MISSING', { file: INSTALLED_NAMES.carrier });
    }
    if (options.carrierRequired && addon.compatibility !== 'dx11') {
      throw appError('ERR_ADDON_INVALID', { reason: 'dx11-compatibility-metadata' });
    }
    for (const [key, hashKey] of [
      ['file', 'addonSha256'],
      ['bridgeFile', 'bridgeSha256'],
      ['carrierFile', 'carrierSha256']
    ]) {
      const file = addon[key];
      const expected = addon[hashKey];
      if (!file) {
        if (expected) throw appError('ERR_ADDON_INVALID', { reason: 'missing-file', file: key });
        continue;
      }
      if (pe.getBitness(file) !== 64) throw appError('ERR_ADDON_INVALID', { reason: 'not-x64', file: path.basename(file) });
      if (expected && sha256(file) !== String(expected).toLowerCase()) {
        throw appError('ERR_ADDON_INVALID', { reason: 'hash', file: path.basename(file) });
      }
    }
  }

  async function diagnose({ gameDir, payload, payloadVersion, payloadVersionLabel, scan: suppliedScan }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    const manifest = readManifest(gameDir);
    const support = assess(scan, {
      allowDx11: Boolean(
        (payload && payload.versionInfo && payload.versionInfo.compatibility === 'dx11') ||
        (manifest && (manifest.payloadVersion === DX11_COMPAT_VERSION || manifest.deploymentApi === 'dx11'))
      )
    });
    const exeDir = scan && scan.chosen ? path.dirname(scan.chosen.path) : gameDir;
    const carrierRequired = isDx11Only(scan && scan.chosen);
    const selectedApi = classifyApi(scan && scan.chosen);
    const deploymentApi = manifest && (manifest.deploymentApi ||
      (manifest.files.some(row => row.kind === 'carrier') ? 'dx11' : 'dx12'));
    const routeMismatch = Boolean(manifest && deploymentApi !== selectedApi);
    const components = [];
    const expected = payload || {};
    const reshadeIni = path.join(exeDir, 'ReShade.ini');
    const carrierDisabledState = carrierRequired && fs.existsSync(reshadeIni)
      ? editCarrierDisabled(fs.readFileSync(reshadeIni, 'utf8'))
      : { found: false, nameOnly: false };
    const carrierDisabled = carrierDisabledState.found;

    const reshade = scan && scan.chosen ? scanModule.inspectReShade(exeDir) : { installed: false, addonSupport: false };
    components.push({
      key: 'reshade', label: 'ReShade Add-on 运行环境',
      ok: Boolean(reshade.installed && reshade.addonSupport),
      detail: reshade.installed && !reshade.addonSupport ? '已安装，但不支持 Add-on' : null
    });

    const managedAddons = manifest
      ? manifest.files.filter(row => row.kind === 'addon' || (carrierRequired && row.kind === 'carrier')).map(row => row.rel)
      : [];
    const activeConflicts = scanConflicts(gameDir, { exclude: managedAddons });
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

    const kinds = ['addon', 'bridge', 'carrier', 'runtime', 'config'];
    for (const kind of kinds) {
      const file = installedFile(exeDir, kind);
      const exists = fs.existsSync(file);
      const expectedHash = kind === 'carrier' && !carrierRequired ? null : expected[kind] && expected[kind].actual;
      const actual = exists && kind !== 'config' ? sha256(file) : null;
      const routeOk = kind === 'carrier' && !carrierRequired ? !exists : exists && !(kind === 'carrier' && carrierDisabled);
      components.push({
        key: kind,
        label: { addon: 'NR→SR 插件', bridge: 'NR Bridge', carrier: 'DX11 Native Carrier', runtime: 'AI Runtime', config: '配置文件' }[kind],
        ok: routeOk && (kind === 'config' || !expectedHash || actual === expectedHash),
        detail: kind === 'carrier' && !carrierRequired && exists
          ? 'DX12 路线不应部署 DX11 Carrier，点击修复移出并备份'
          : (kind === 'carrier' && carrierDisabled
            ? (carrierDisabledState.nameOnly
              ? 'DX11 Carrier 注册名仍在 ReShade.ini 中禁用；Manager 保留名称项，请在 ReShade 中手动启用'
              : 'DX11 Carrier 已在 ReShade.ini 中禁用，点击修复重新启用')
            : (!exists && routeOk === false ? '文件缺失' : (kind !== 'config' && expectedHash && actual !== expectedHash ? '版本或校验不一致' : null)))
      });
    }

    return {
      installed: Boolean(manifest),
      complete: Boolean(manifest) && !routeMismatch && components.every(row => row.ok),
      supported: support.supported,
      supportCode: support.code,
      selectedApi,
      deploymentApi: deploymentApi || null,
      routeMismatch,
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
        await sidecarBackup(gameDir, dxgi);
        await journal.capture(gameDir, dxgi);
        await journal.capture(gameDir, d3d12);
        await fs.promises.rename(dxgi, d3d12);
        manifest.reshadeRoute = 'd3d12';
      } else {
        if (!fs.existsSync(d3d12)) throw appError('ERR_NOT_INSTALLED');
        if (fs.existsSync(dxgi)) throw appError('ERR_D3D12_CONFLICT');
        await sidecarBackup(gameDir, d3d12);
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
    const manifest = readManifest(gameDir);
    if (!manifest) throw appError('ERR_NOT_INSTALLED');
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    const allowDx11 = addon.compatibility === 'dx11' ||
      (!addon.compatibility && manifest.payloadVersion === DX11_COMPAT_VERSION);
    const support = assess(scan, { allowDx11 });
    if (!support.supported) throw appError(support.code);
    const carrierRequired = isDx11Only(scan.chosen);
    verifyAddon(addon, { carrierRequired });
    const exePath = scan.chosen && scan.chosen.path
      ? scan.chosen.path
      : path.join(gameDir, manifest.game.exe);
    const exeDir = path.dirname(exePath);
    const target = installedFile(exeDir, 'addon');
    const carrierTarget = installedFile(exeDir, 'carrier');
    if (!canWrite(exeDir)) throw appError('ERR_NO_WRITE_ACCESS');
    if (guards.antiCheatPresent(gameDir) && allowAntiCheat !== true) {
      throw appError('ERR_ANTI_CHEAT_CONFIRM', { operation: 'upgrade-addon' });
    }
    await guards.assertGameClosed(gameDir, exePath);

    return journal.transaction(gameDir, async () => {
      try {
        await retireManagedCarriers(gameDir, manifest, carrierRequired ? carrierTarget : null);
        const found = await moveConflicts(gameDir, manifest.installId, {
          exclude: manifest.files.map(row => row.rel),
          capture: conflictTarget => journal.capture(gameDir, conflictTarget)
        });
        manifest.conflicts = [...(manifest.conflicts || []), ...found];
        await writeManaged(gameDir, manifest, addon.file, target, 'addon');
        if (addon.bridgeFile) {
          await writeManaged(gameDir, manifest, addon.bridgeFile,
            installedFile(exeDir, 'bridge'), 'bridge');
        }
        if (carrierRequired) {
          await writeManaged(gameDir, manifest, addon.carrierFile, carrierTarget, 'carrier');
          await enableCarrier(gameDir, exeDir);
        }
        manifest.payloadVersion = version || addon.id || null;
        manifest.deploymentApi = classifyApi(scan.chosen);
        manifest.updatedAt = new Date().toISOString();
        await saveManifest(gameDir, manifest);
        const expected = {
          addon: { file: target, actual: sha256(target), name: path.basename(target) }
        };
        if (addon.bridgeFile) expected.bridge = {
          file: installedFile(exeDir, 'bridge'),
          actual: sha256(installedFile(exeDir, 'bridge')),
          name: path.basename(installedFile(exeDir, 'bridge'))
        };
        if (carrierRequired) expected.carrier = {
          file: carrierTarget, actual: sha256(carrierTarget), name: path.basename(carrierTarget)
        };
        return diagnose({
          gameDir,
          payload: expected,
          payloadVersion: manifest.payloadVersion,
          scan
        });
      } catch (error) {
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
      for (const row of manifest.files.filter(row => row.original.existed)) {
        const backup = journal.safePath(gameDir, row.original.backupRel);
        if (!row.original.sha256 || sha256(backup) !== row.original.sha256) throw appError('ERR_BACKUP_INVALID', { rel: row.rel });
      }
      for (const row of manifest.conflicts || []) {
        journal.safePath(gameDir, row.sourceRel);
        const backup = journal.safePath(gameDir, row.backupRel);
        if (!fs.existsSync(backup) || !row.sha256 || sha256(backup) !== row.sha256) throw appError('ERR_BACKUP_INVALID', { rel: row.sourceRel });
      }
      // Refuse the whole uninstall before touching anything when a managed
      // binary was changed after installation. In particular, never overwrite
      // a changed carrier with the pre-install backup.
      for (const row of manifest.files) {
        if (row.kind === 'config' && !removeSettings && !row.original.existed) continue;
        const routeRel = row.kind === 'reshade' && manifest.reshadeRoute === 'd3d12'
          ? path.relative(gameDir, path.join(path.dirname(exePath), 'd3d12.dll'))
          : row.rel;
        const target = journal.safePath(gameDir, routeRel);
        if (!fs.existsSync(target)) continue;
        const current = fs.statSync(target).isFile() ? sha256(target) : null;
        const protectChange = row.original.existed || row.kind !== 'config';
        if (protectChange && (!row.installedSha256 || current !== row.installedSha256)) {
          warnings.push({ code: 'ERR_FILE_CHANGED', rel: row.rel });
        }
      }
      const managedTargets = new Set(manifest.files.map(row => path.normalize(
        row.kind === 'reshade' && manifest.reshadeRoute === 'd3d12'
          ? path.relative(gameDir, path.join(path.dirname(exePath), 'd3d12.dll'))
          : row.rel
      ).toLowerCase()));
      for (const row of manifest.conflicts || []) {
        const source = journal.safePath(gameDir, row.sourceRel);
        const backup = journal.safePath(gameDir, row.backupRel);
        if (fs.existsSync(source) && fs.existsSync(backup) &&
            !managedTargets.has(path.normalize(row.sourceRel).toLowerCase())) {
          warnings.push({ code: 'ERR_FILE_CHANGED', rel: row.sourceRel });
        }
      }
      if (warnings.length) return { removed: false, settingsKept: !removeSettings, warnings };

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
          await fs.promises.unlink(target);
        }
      }
      warnings.push(...await restoreConflicts(gameDir, manifest.conflicts, {
        capture: target => journal.capture(gameDir, target)
      }));
      if (!warnings.length) {
        const file = manifestPath(gameDir);
        await journal.capture(gameDir, file);
        if (fs.existsSync(file)) await fs.promises.unlink(file);
      } else {
        await saveManifest(gameDir, manifest);
      }
      return { removed: warnings.length === 0, settingsKept: !removeSettings, warnings };
    });
  }

  return { install, repair, upgradeAddon, toggleD3D12, uninstall, diagnose, isAddonReShade };
}

module.exports = { createInstaller };
