'use strict';

const fs = require('fs');
const path = require('path');
const { noLinks, atomicJson } = require('./launch-safety');
const { createReframeworkPreparation, RECEIPT: REF_RECEIPT } = require('./reframework-preparation');
const { REFRAMEWORK_ADAPTERS, OFFICIAL_REFRAMEWORK_01417: REF_OFFICIAL } = require('./reframework-compatibility');
const { INSTALLED_NAMES, DX11_COMPAT_VERSION } = require('./constants');
const { appError, MESSAGES } = require('./errors');
const { inspectAddonLayout, requireAddonLayout } = require('./reshade-layout');
const { assess, classifyApi, isDx11Only } = require('./game-support');
const { sha256 } = require('./payload');
const coreVersionText = value => {
  if (/^0\.3\.3-(?:dev-)?r4\b/.test(String(value || ''))) return '0.3.3.4 · 稳定兼容';
  if (/^(?:beta)?0\.4\.7(?:-?beta)?$/i.test(String(value || ''))) return 'beta0.4.7';
  const raw = String(value || ''), version = raw.match(/^(0\.\d+\.\d+(?:\.\d+)?(?:-(?:hotfix|beta)\.\d+)?)/)?.[1];
  if (!version) return raw;
  if (version.startsWith('0.2.')) return version.replace(/-beta\.\d+$/i, '');
  if (version.startsWith('0.3.')) return `${version} · 历史对照`;
  return version.startsWith('0.4.') ? `${version} · Beta` : version;
};
const { scanConflicts, moveConflicts, restoreConflicts } = require('./conflicts');
const {
  manifestPath, backupPath, readManifest, newManifest, findEntry, validateEntry,
  manifestExecutable, assertManifestExecutable
} = require('./manifest');

function createInstaller(overrides = {}) {
  const journal = overrides.journal || require('../core/file-journal');
  const scanModule = overrides.scan || require('../core/scan');
  const guards = overrides.guards || require('../core/install-guards');
  const pe = overrides.pe || require('../core/pe');
  const copy = overrides.copyFile || ((source, target) => fs.promises.copyFile(source, target));
  const refDigest = overrides.reframeworkFileDigest || sha256;

  const safe = (gameDir, target) => journal.safePath(gameDir, path.relative(gameDir, target));

  function refError(code, message, details) { throw Object.assign(new Error(message), { code, details }); }
  const samePath = (a, b) => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
  const relKey = value => path.normalize(value).toLowerCase();
  async function checkedHash(gameDir, file) {
    safe(gameDir, file); await noLinks(file);
    if (!fs.existsSync(file)) return null;
    const stat = fs.lstatSync(file);
    if (!stat.isFile() || stat.nlink > 1) throw appError('ERR_FILE_CHANGED', { rel: path.relative(gameDir, file) });
    return sha256(file);
  }
  function trustedHashes(version, info) {
    if (!/^0\.4\.7-?beta$/i.test(String(version || '')) || info?.id && !/^0\.4\.7-?beta$/i.test(info.id) ||
        !Array.isArray(info?.trustedUpgradeFrom) || info.trustedUpgradeFrom.length > 16) return new Set();
    if (!info.trustedUpgradeFrom.every(value => typeof value === 'string' && /^[a-f0-9]{64}$/i.test(value))) return new Set();
    return new Set(info.trustedUpgradeFrom.map(value => value.toLowerCase()));
  }
  async function preflightWrites(gameDir, manifest, writes, version, info) {
    const trusted = trustedHashes(version, info), adoptions = [];
    for (const write of writes) {
      await noLinks(write.source);
      if (!write.expected || !fs.existsSync(write.source) || !fs.statSync(write.source).isFile() || sha256(write.source) !== write.expected.toLowerCase())
        throw appError('ERR_PAYLOAD_HASH', { file: path.basename(write.source), reason: 'source-changed' });
      const row = findEntry(manifest, path.relative(gameDir, write.target)), current = await checkedHash(gameDir, write.target);
      if (!row) continue;
      validateEntry(gameDir, manifest, row, journal.safePath);
      if (row.original.existed) {
        const backup = journal.safePath(gameDir, row.original.backupRel); await noLinks(backup);
        if (!row.original.sha256 || sha256(backup) !== row.original.sha256) throw appError('ERR_BACKUP_INVALID', { rel: row.rel });
      }
      if (current && current !== row.installedSha256) {
        if (write.kind !== 'addon' || row.kind !== 'addon' || !trusted.has(current)) throw appError('ERR_FILE_CHANGED', { rel: row.rel });
        adoptions.push({ row, current, target: write.target, next: write.expected.toLowerCase(), version });
      }
    }
    return adoptions;
  }
  async function adoptTrusted(gameDir, manifest, adoptions, exePath) {
    for (const adoption of adoptions) {
      await guards.assertGameClosed(gameDir, exePath);
      if (await checkedHash(gameDir, adoption.target) !== adoption.current) throw appError('ERR_FILE_CHANGED', { rel: adoption.row.rel });
      const backupRel = path.join('_DLSS5_Backup', 'trusted-core-upgrades', manifest.installId, `${adoption.row.rel}.${adoption.current}.bin`);
      const backup = journal.safePath(gameDir, backupRel); await noLinks(backup);
      const exists = fs.existsSync(backup);
      if (exists && (!fs.statSync(backup).isFile() || sha256(backup) !== adoption.current)) throw appError('ERR_BACKUP_INVALID', { rel: backupRel });
      if (!exists) {
        await journal.capture(gameDir, backup); await copyFile(adoption.target, backup);
        if (sha256(backup) !== adoption.current) throw appError('ERR_BACKUP_INVALID', { rel: backupRel });
      }
      if (manifest.trustedCoreUpgrades !== undefined && !Array.isArray(manifest.trustedCoreUpgrades)) throw appError('ERR_BACKUP_INVALID');
      manifest.trustedCoreUpgrades = [...(manifest.trustedCoreUpgrades || []), { rel: adoption.row.rel, fromSha256: adoption.current,
        toSha256: adoption.next, version: adoption.version, backupRel, acceptedAt: new Date().toISOString() }];
      adoption.row.installedSha256 = adoption.current;
    }
    if (adoptions.length) await saveManifest(gameDir, manifest);
  }
  function refInTransaction(gameDir) {
    // This adapter is private to an already-open installer journal. REF's
    // metadata confirmation must capture into it, never start a second WAL.
    return createReframeworkPreparation({ overrides: { pe, fileDigest: refDigest, guards, journal: { ...journal,
      transaction: async (root, work) => {
        if (!samePath(root, gameDir)) throw appError('ERR_BACKUP_INVALID');
        return work();
      } } } });
  }
  async function preflightRef(gameDir, exePath, manifest, rootRows, allowConfirm, adoptions = []) {
    const adapter = REFRAMEWORK_ADAPTERS.find(row => row.executable.toLowerCase() === path.basename(exePath).toLowerCase());
    if (!adapter?.storage || !samePath(path.dirname(exePath), gameDir) || pe.getBitness(exePath) !== 64) return null;
    const input = { gameDir, exe: exePath, engine: adapter.engine };
    const receiptFile = journal.safePath(gameDir, REF_RECEIPT); await noLinks(receiptFile);
    let record = null, receiptHash = null;
    if (fs.existsSync(receiptFile)) {
      const stat = fs.statSync(receiptFile);
      if (!stat.isFile() || stat.size > 128 * 1024) refError('REF_RECEIPT_INVALID', 'REFramework 收据无效，请先检查兼容组件。');
      receiptHash = sha256(receiptFile);
      try { record = JSON.parse(fs.readFileSync(receiptFile, 'utf8')); } catch { refError('REF_RECEIPT_INVALID', 'REFramework 收据无法解析，请保留文件并保存反馈。'); }
      if (record.version !== 1 || record.product !== 'xiaofeng-reframework-preparation' || record.adapter !== adapter.id ||
          !record.game || !samePath(record.game.dir, gameDir) || record.game.exe?.toLowerCase() !== adapter.executable.toLowerCase() || !Array.isArray(record.mirrors))
        refError('REF_RECEIPT_INVALID', 'REFramework 收据没有绑定当前游戏。');
    }
    const loader = path.join(gameDir, 'dinput8.dll'); await noLinks(loader);
    const official = fs.existsSync(loader) && fs.statSync(loader).isFile() && fs.statSync(loader).size === REF_OFFICIAL.dll_bytes &&
      refDigest(loader) === REF_OFFICIAL.dll_sha256 && pe.getBitness(loader) === 64;
    if (!official) {
      if (record?.mirrors?.length) refError('REF_COMPONENT_HASH', '已有受管 Core 镜像，但官方 REFramework loader 缺失或变化；请先恢复固定兼容组件。');
      return null;
    }
    const claims = [], mirrors = [];
    for (const row of rootRows) {
      if (path.dirname(path.resolve(gameDir, row.rel)) !== path.resolve(gameDir) || !/\.addon64$/i.test(row.rel)) continue;
      const rootFile = journal.safePath(gameDir, row.rel), mirrorRel = `_storage_/${path.basename(row.rel).toLowerCase()}`;
      const mirrorFile = journal.safePath(gameDir, mirrorRel), rootHash = await checkedHash(gameDir, rootFile), mirrorHash = await checkedHash(gameDir, mirrorFile);
      const owned = record?.mirrors.find(item => relKey(item.rootRel || '') === relKey(row.rel));
      const trusted = adoptions.some(item => item.row === row && item.current === rootHash);
      if (rootHash && rootHash !== row.installedSha256 && !trusted) throw appError('ERR_FILE_CHANGED', { rel: row.rel });
      if (owned) {
        if (owned.rootInstallId !== manifest.installId || relKey(owned.mirrorRel || '') !== relKey(mirrorRel) || owned.sha256 !== mirrorHash)
          refError('REF_MIRROR_CHANGED', '已确认 Core 镜像已变化，请保留 _storage_ 并保存反馈；不会覆盖。', { file: mirrorRel });
        mirrors.push({ row, mirrorRel, rootHash, mirrorHash });
      } else if (mirrorHash) {
        if (!allowConfirm || !record || rootHash !== mirrorHash || !rootHash)
          refError('REF_MIRROR_UNOWNED', '发现未确认的 Core 镜像。请先准备 REFramework 组件，再选择核心版本安装/升级确认；不匹配文件须保留并核对。', { file: mirrorRel });
        claims.push({ rootRel: row.rel, mirrorRel, sha256: mirrorHash }); mirrors.push({ row, mirrorRel, rootHash, mirrorHash });
      }
    }
    if (!allowConfirm && record?.mirrors.some(item => !mirrors.some(row => relKey(row.mirrorRel) === relKey(item.mirrorRel))))
      refError('REF_MIRROR_UNOWNED', '镜像收据没有对应的当前主 Core 归属，卸载已停止；请保存反馈。');
    return { input, receiptFile, receiptHash, record, claims, mirrors, service: refInTransaction(gameDir), plan: null };
  }
  async function planRefUpdate(context, nextHash) {
    if (!context || !context.mirrors.length) return;
    if (context.receiptHash !== (fs.existsSync(context.receiptFile) ? sha256(context.receiptFile) : null)) refError('REF_STATE_CHANGED', 'Core 预检后 REFramework 收据发生变化。');
    if (context.claims.length) await context.service.confirmMirrors(context.input, { confirm: true, mirrors: context.claims });
    context.plan = await context.service.planMirrors(context.input, { operation: 'replace', files: context.mirrors.map(item => ({
      rootRel: item.row.rel, sha256: nextHash || item.rootHash })) });
  }
  async function archiveMirror(gameDir, operation, exePath) {
    if (!operation.archive) return;
    const from = journal.safePath(gameDir, operation.archive.sourceRel), to = journal.safePath(gameDir, operation.archive.targetRel);
    await guards.assertGameClosed(gameDir, exePath);
    if (await checkedHash(gameDir, from) !== operation.archive.sha256) refError('REF_MIRROR_CHANGED', '备份前 Core 镜像发生变化。');
    if (await checkedHash(gameDir, to) !== null) refError('REF_MIRROR_BACKUP_CHANGED', 'Core 镜像备份目标已出现，不会覆盖。');
    await journal.capture(gameDir, to); await copyFile(from, to);
    if (sha256(to) !== operation.archive.sha256) refError('REF_MIRROR_BACKUP_CHANGED', 'Core 镜像备份读回不一致。');
  }
  async function saveRefPlan(context, next) {
    if (!context?.plan) return;
    await noLinks(context.receiptFile);
    if (sha256(context.receiptFile) !== context.plan.receiptBeforeSha256) refError('REF_STATE_CHANGED', '写入前 REFramework 收据发生变化。');
    await journal.capture(context.input.gameDir, context.receiptFile); await atomicJson(context.receiptFile, next);
  }
  async function applyRefUpdate(context) {
    if (!context?.plan) return;
    const { gameDir, exe } = context.input;
    for (const operation of context.plan.operations) {
      await archiveMirror(gameDir, operation, exe); await guards.assertGameClosed(gameDir, exe);
      const root = journal.safePath(gameDir, operation.rootRel), mirror = journal.safePath(gameDir, operation.mirrorRel);
      if (await checkedHash(gameDir, root) !== operation.sha256 || await checkedHash(gameDir, mirror) !== operation.expectedMirrorSha256)
        refError('REF_MIRROR_CHANGED', 'Core 写入前 root/mirror 状态改变，整笔升级将回滚。');
      await journal.capture(gameDir, mirror); await copyFile(root, mirror);
      if (sha256(mirror) !== operation.sha256) refError('REF_MIRROR_CHANGED', 'Core 镜像写入后读回不一致，整笔升级将回滚。');
    }
    await saveRefPlan(context, context.plan.receiptNext);
  }
  async function applyRefUninstall(context, manifest, conflictRestores) {
    if (!context?.plan) return [];
    const { gameDir, exe } = context.input, restored = [], next = structuredClone(context.plan.receiptNext);
    for (const operation of context.plan.operations) {
      const row = findEntry(manifest, operation.rootRel); await archiveMirror(gameDir, operation, exe); await guards.assertGameClosed(gameDir, exe);
      const mirror = journal.safePath(gameDir, operation.mirrorRel);
      if (await checkedHash(gameDir, mirror) !== operation.expectedMirrorSha256) refError('REF_MIRROR_CHANGED', '卸载前 Core 镜像已变化，整笔卸载将回滚。');
      const conflict = conflictRestores.find(item => relKey(item.sourceRel) === relKey(row.rel));
      const restoredHash = row.original.existed ? row.original.sha256 : conflict?.sha256 || null;
      const root = journal.safePath(gameDir, row.rel);
      if (await checkedHash(gameDir, root) !== restoredHash) refError('REF_MIRROR_CHANGED', '主 Core 的实际恢复结果与原收据不符，镜像未单独写入。');
      await journal.capture(gameDir, mirror);
      if (restoredHash) {
        await copyFile(root, mirror);
        if (sha256(mirror) !== restoredHash) refError('REF_MIRROR_CHANGED', '恢复 Core 镜像后读回不一致。');
      } else await fs.promises.unlink(mirror);
      restored.push({ rootRel: row.rel, mirrorRel: operation.mirrorRel, restoredSha256: restoredHash });
    }
    next.retiredMirrors = [...(next.retiredMirrors || []), ...next.mirrors.filter(row => restored.some(item => relKey(item.mirrorRel) === relKey(row.mirrorRel)))];
    next.mirrors = next.mirrors.filter(row => !restored.some(item => relKey(item.mirrorRel) === relKey(row.mirrorRel)));
    await saveRefPlan(context, next); return restored;
  }

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
    await copy(src, dest);
  }

  async function sidecarBackup(gameDir, target, manifest) {
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
      // Only a newly created sidecar is ours. Equal bytes in a pre-existing
      // .bak do not confer ownership (older managers also reused these).
      manifest.sidecars = [...(manifest.sidecars || []), {
        rel: path.relative(gameDir, candidate), sha256: originalHash,
        sourceRel: path.relative(gameDir, target)
      }];
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

  async function writeManaged(gameDir, manifest, source, target, kind, expectedHash) {
    safe(gameDir, target);
    const expected = typeof expectedHash === 'string' && /^[a-f0-9]{64}$/i.test(expectedHash) ? expectedHash.toLowerCase() : null;
    let sourceHash = null;
    try { sourceHash = fs.statSync(source).isFile() ? sha256(source) : null; } catch {}
    if (!expected || sourceHash !== expected) throw appError('ERR_PAYLOAD_HASH', { file: path.basename(source), reason: 'source-changed' });
    const existing = findEntry(manifest, path.relative(gameDir, target));
    if (existing && fs.existsSync(target)) {
      const current = fs.statSync(target).isFile() ? sha256(target) : null;
      if (!existing.installedSha256 || current !== existing.installedSha256) {
        throw appError('ERR_FILE_CHANGED', { rel: existing.rel });
      }
    }
    // Repeated repair of an unchanged owned file needs no extra disk writes.
    if (existing && fs.existsSync(target) && sha256(target) === expected) return existing;
    const sidecar = await sidecarBackup(gameDir, target, manifest);
    await journal.capture(gameDir, target);
    const entry = await recordOriginal(gameDir, manifest, target, kind);
    if (kind === 'addon') {
      await guards.assertGameClosed(gameDir, manifestExecutable(gameDir, manifest)); await noLinks(source); await noLinks(target);
      if (sha256(source) !== expected) throw appError('ERR_PAYLOAD_HASH', { file: path.basename(source), reason: 'source-changed' });
      if (existing && fs.existsSync(target) && sha256(target) !== existing.installedSha256) throw appError('ERR_FILE_CHANGED', { rel: existing.rel });
    }
    await copyFile(source, target);
    const installedHash = fs.statSync(target).isFile() ? sha256(target) : null;
    if (installedHash !== expected) throw appError('ERR_PAYLOAD_HASH', { file: path.basename(source), reason: 'copy-changed' });
    entry.installedSha256 = installedHash;
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

  function managedTargetForRow(gameDir, manifest, row) {
    const original = journal.safePath(gameDir, row.rel);
    if (row.kind !== 'reshade' || manifest.reshadeRoute !== 'd3d12') return { target: original, original, routed: null, ambiguous: false };
    const routed = journal.safePath(gameDir, path.relative(gameDir, path.join(path.dirname(original), 'd3d12.dll')));
    if (path.resolve(original).toLowerCase() === path.resolve(routed).toLowerCase()) {
      return { target: original, original, routed, ambiguous: false };
    }
    const matches = file => {
      try { return fs.statSync(file).isFile() && row.installedSha256 && sha256(file) === row.installedSha256; }
      catch { return false; }
    };
    const originalMatches = matches(original), routedMatches = matches(routed);
    const ambiguous = fs.existsSync(original) && fs.existsSync(routed);
    if (originalMatches !== routedMatches) return { target: originalMatches ? original : routed, original, routed, ambiguous };
    const boundDir = path.dirname(manifestExecutable(gameDir, manifest)).toLowerCase();
    return { target: path.dirname(original).toLowerCase() === boundDir ? routed : original, original, routed, ambiguous };
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

  async function enableCarrier(gameDir, exeDir, manifest) {
    const file = path.join(exeDir, 'ReShade.ini');
    if (!fs.existsSync(file)) return;
    const edited = editCarrierDisabled(fs.readFileSync(file, 'utf8'), true);
    if (!edited.changed) return;
    await sidecarBackup(gameDir, file, manifest);
    await journal.capture(gameDir, file);
    await fs.promises.writeFile(file, edited.text, 'utf8');
  }

  function editAddonDisabled(text, remove = false) {
    const addonFile = path.basename(INSTALLED_NAMES.addon);
    let section = '';
    let changed = false;
    const lines = String(text || '').split('\n').map(line => {
      const header = line.match(/^(?:\uFEFF)?[ \t]*\[([^\]]+)\]/);
      if (header) {
        section = header[1].replace(/^[ \t]+|[ \t]+$/g, '');
        return line;
      }
      if (section !== 'ADDON') return line;
      const match = line.match(/^([ \t]*DisabledAddons[ \t]*=[ \t]*)(.*?)(\r?)$/);
      if (!match) return line;
      const valueEnd = match[2].search(/[ \t]*$/);
      const valueTail = match[2].slice(valueEnd);
      const items = splitDisabledAddons(match[2].slice(0, valueEnd));
      let lineChanged = false;
      const kept = items.filter(item => {
        const at = item.indexOf('@');
        const fileName = at >= 0 ? item.slice(at + 1) : item;
        const disablesAddon = fileName === addonFile;
        if (!disablesAddon || !remove) return true;
        changed = true;
        lineChanged = true;
        return false;
      });
      return lineChanged
        ? `${match[1]}${kept.join(',')}${valueTail}${match[3]}`
        : line;
    });
    return { changed, text: lines.join('\n') };
  }

  async function install({ gameDir, payload, scan: suppliedScan, allowAntiCheat = false }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    const priorManifest = readManifest(gameDir);
    if (priorManifest) assertManifestExecutable(gameDir, priorManifest, scan?.chosen?.path);
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
      assertManifestExecutable(gameDir, manifest, exePath);
      try {
        const reshade = scanModule.inspectReShade(exeDir);
        if (existingProxyConflict(exeDir, reshade)) throw appError('ERR_RESHADER_CONFLICT');
        if (reshade.installed && !reshade.addonSupport) throw appError('ERR_RESHADER_NO_ADDON');
        requireAddonLayout(exeDir);
        const coreTarget = installedFile(exeDir, 'addon');
        const writes = ['addon', 'bridge', 'runtime', ...(!reshade.installed ? ['reshade'] : []), ...(carrierRequired ? ['carrier'] : [])]
          .map(kind => ({ kind, source: payload[kind].file, target: installedFile(exeDir, kind), expected: payload[kind].actual }));
        const adoptions = await preflightWrites(gameDir, manifest, writes, payload.version, payload.versionInfo);
        const rootRow = findEntry(manifest, path.relative(gameDir, coreTarget));
        const refContext = await preflightRef(gameDir, exePath, manifest,
          [rootRow || { rel: path.relative(gameDir, coreTarget), kind: 'addon', installedSha256: null }], true, adoptions);
        await adoptTrusted(gameDir, manifest, adoptions, exePath);
        await planRefUpdate(refContext, payload.addon.actual);

        // Retire a carrier managed under an older filename first. If that
        // restores a pre-existing carrier, the conflict pass below moves it
        // aside before the matched compatibility set is installed.
        const carrierTarget = installedFile(exeDir, 'carrier');
        await retireManagedCarriers(gameDir, manifest, carrierRequired ? carrierTarget : null);

        // Move only known DLSS/NR/NGX add-ons into a reversible manager backup.
        // ReShade itself, game DLSS DLLs and ordinary RenoDX add-ons are never
        // part of this list.
        const movedConflicts = await moveConflicts(gameDir, manifest.installId, {
          addonDir: exeDir,
          exclude: manifest.files.map(row => row.rel),
          capture: target => journal.capture(gameDir, target)
        });
        manifest.conflicts = [...(manifest.conflicts || []), ...movedConflicts];
        manifest.payloadVersion = payload.version || null;
        manifest.hardwareFamily = payload.hardwareFamily || null;
        manifest.deploymentApi = classifyApi(scan.chosen);
        manifest.carrierDisabledByUser = carrierRequired ? false : manifest.carrierDisabledByUser === true;

        if (!reshade.installed) {
          await writeManaged(gameDir, manifest, payload.reshade.file,
            installedFile(exeDir, 'reshade'), 'reshade', payload.reshade.actual);
        }
        await writeManaged(gameDir, manifest, payload.addon.file,
          installedFile(exeDir, 'addon'), 'addon', payload.addon.actual);
        await applyRefUpdate(refContext);
        await writeManaged(gameDir, manifest, payload.bridge.file,
          installedFile(exeDir, 'bridge'), 'bridge', payload.bridge.actual);
        await writeManaged(gameDir, manifest, payload.runtime.file,
          installedFile(exeDir, 'runtime'), 'runtime', payload.runtime.actual);

        if (carrierRequired) {
          await writeManaged(gameDir, manifest, payload.carrier.file,
            carrierTarget, 'carrier', payload.carrier.actual);
        }

        const configTarget = installedFile(exeDir, 'config');
        if (!fs.existsSync(configTarget)) {
          await writeManaged(gameDir, manifest, payload.config.file, configTarget, 'config', payload.config.actual);
        }

        // If a previous ReShade session disabled this add-on, remove only its
        // exact filename from [ADDON]. Preserve other sections, names and
        // ReShade's escaped comma entries.
        const reshadeIni = path.join(exeDir, 'ReShade.ini');
        if (fs.existsSync(reshadeIni)) {
          const edited = editAddonDisabled(fs.readFileSync(reshadeIni, 'utf8'), true);
          if (edited.changed) {
            await sidecarBackup(gameDir, reshadeIni, manifest);
            await journal.capture(gameDir, reshadeIni);
            await fs.promises.writeFile(reshadeIni, edited.text, 'utf8');
          }
        }
        if (carrierRequired) await enableCarrier(gameDir, exeDir, manifest);

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
      if (typeof expected !== 'string' || !/^[a-f0-9]{64}$/i.test(expected)) throw appError('ERR_ADDON_INVALID', { reason: 'missing-hash', file: path.basename(file) });
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
    const selectedExecutable = scan?.chosen?.path || null;
    const installedExecutable = manifest ? manifestExecutable(gameDir, manifest) : selectedExecutable;
    const executableMismatch = Boolean(manifest && selectedExecutable &&
      path.resolve(selectedExecutable).toLowerCase() !== installedExecutable.toLowerCase());
    const exeDir = installedExecutable ? path.dirname(installedExecutable) : gameDir;
    const carrierRequired = isDx11Only(scan && scan.chosen);
    const selectedApi = classifyApi(scan && scan.chosen);
    const deploymentApi = manifest && (manifest.deploymentApi ||
      (manifest.files.some(row => row.kind === 'carrier') ? 'dx11' : 'dx12'));
    const routeMismatch = Boolean(manifest && (deploymentApi !== selectedApi || executableMismatch));
    const components = [];
    if (manifest) components.push({
      key: 'executable', label: '安装目标程序', ok: !executableMismatch,
      detail: executableMismatch ? '当前选择了另一个游戏程序；请重新选择原安装程序并恢复或卸载后，再切换目标。' : null
    });
    const expected = payload || {};
    const reshadeIni = path.join(exeDir, 'ReShade.ini');
    const addonLayout = inspectAddonLayout(exeDir);
    components.push({ key: 'addon-layout', label: '插件加载位置', ok: addonLayout.ok,
      detail: addonLayout.ok ? null : MESSAGES[addonLayout.code] });
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

    const currentCarrierRel = path.normalize(path.relative(
      gameDir, installedFile(exeDir, 'carrier'))).toLowerCase();
    const managedCarrierRows = manifest
      ? manifest.files.filter(row => row.kind === 'carrier')
      : [];
    const managedAddons = manifest
      ? manifest.files.filter(row => row.kind === 'addon' ||
        (row.kind === 'carrier' && path.normalize(row.rel).toLowerCase() === currentCarrierRel))
        .map(row => row.rel)
      : [];
    const activeConflicts = scanConflicts(gameDir, { exclude: managedAddons, addonDir: exeDir });
    const activeManagedCarriers = activeConflicts.filter(conflict => managedCarrierRows.some(row =>
      path.normalize(row.rel).toLowerCase() === path.normalize(conflict.rel).toLowerCase()));
    const activeExternalConflicts = activeConflicts.length - activeManagedCarriers.length;
    const savedConflicts = manifest && Array.isArray(manifest.conflicts) ? manifest.conflicts : [];
    const conflictDetail = activeManagedCarriers.length && activeExternalConflicts
      ? `发现 ${activeManagedCarriers.length} 个受管旧 Carrier 残留及 ${activeExternalConflicts} 个外部或未受管冲突插件，点击修复会分别退役、移出并保留备份`
      : activeManagedCarriers.length
        ? `发现 ${activeManagedCarriers.length} 个受管旧 Carrier 残留，点击修复会退役并恢复原始备份`
        : activeExternalConflicts
          ? `发现 ${activeExternalConflicts} 个外部或未受管冲突插件，点击修复会移出并备份`
          : (savedConflicts.length ? `已移出并保留 ${savedConflicts.length} 个外部冲突备份` : '未发现需要移出的冲突插件');
    components.push({
      key: 'conflicts', label: '冲突 Addon',
      ok: activeConflicts.length === 0,
      detail: conflictDetail
    });

    if (manifest && payloadVersion) {
      components.push({
        key: 'version', label: '核心版本',
        ok: manifest.payloadVersion === payloadVersion,
        detail: manifest.payloadVersion === payloadVersion
          ? `当前为 ${coreVersionText(payloadVersionLabel || payloadVersion)}`
          : `已安装 ${coreVersionText(manifest.payloadVersion || '旧版本')}，当前选择 ${coreVersionText(payloadVersionLabel || payloadVersion)}；点击修复切换`
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
        label: { addon: 'NR→SR 插件', bridge: '共用运行组件', carrier: '兼容桥接', runtime: 'AI Runtime', config: '配置文件' }[kind],
        ok: routeOk && (kind === 'config' || !expectedHash || actual === expectedHash),
        detail: kind === 'carrier' && !carrierRequired && !exists
          ? (selectedApi === 'dx12' ? '无需启用（DirectX 12）' : '未部署，需先确认 API 与配套组件')
          : kind === 'carrier' && !carrierRequired && exists
          ? (selectedApi === 'dx12'
            ? '目标 DirectX 12 路线仍有受管 DX11 Carrier，点击修复退役并恢复原始备份'
            : '当前 API 未确认，受管 DX11 Carrier 暂不自动移动；请先确认 DirectX 11 或 DirectX 12 路线')
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
      executableMismatch,
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
    if (enabled === true && classifyApi(scan.chosen) !== 'dx12') throw appError('ERR_UNSUPPORTED_API');
    assertManifestExecutable(gameDir, manifest, scan.chosen.path);
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
        if (!findEntry(manifest, path.relative(gameDir, dxgi))) {
          const entry = await recordOriginal(gameDir, manifest, dxgi, 'reshade');
          entry.installedSha256 = sha256(dxgi);
          entry.sourceName = path.basename(dxgi);
        }
        await sidecarBackup(gameDir, dxgi, manifest);
        await journal.capture(gameDir, dxgi);
        await journal.capture(gameDir, d3d12);
        await fs.promises.rename(dxgi, d3d12);
        manifest.reshadeRoute = 'd3d12';
      } else {
        if (!fs.existsSync(d3d12)) throw appError('ERR_NOT_INSTALLED');
        if (fs.existsSync(dxgi)) throw appError('ERR_D3D12_CONFLICT');
        await sidecarBackup(gameDir, d3d12, manifest);
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

  async function upgradeAddon({ gameDir, addon, version, versionInfo, scan: suppliedScan, allowAntiCheat = false }) {
    const manifest = readManifest(gameDir);
    if (!manifest) throw appError('ERR_NOT_INSTALLED');
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    assertManifestExecutable(gameDir, manifest, scan?.chosen?.path);
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
        requireAddonLayout(exeDir);
        const writes = [{ kind: 'addon', source: addon.file, target, expected: addon.addonSha256 }];
        if (addon.bridgeFile) writes.push({ kind: 'bridge', source: addon.bridgeFile, target: installedFile(exeDir, 'bridge'), expected: addon.bridgeSha256 });
        if (carrierRequired) writes.push({ kind: 'carrier', source: addon.carrierFile, target: carrierTarget, expected: addon.carrierSha256 });
        const adoptions = await preflightWrites(gameDir, manifest, writes, version || addon.id, versionInfo || addon.versionInfo);
        const rootRow = findEntry(manifest, path.relative(gameDir, target));
        const refContext = await preflightRef(gameDir, exePath, manifest,
          [rootRow || { rel: path.relative(gameDir, target), kind: 'addon', installedSha256: null }], true, adoptions);
        await adoptTrusted(gameDir, manifest, adoptions, exePath);
        await planRefUpdate(refContext, addon.addonSha256);
        await retireManagedCarriers(gameDir, manifest, carrierRequired ? carrierTarget : null);
        const found = await moveConflicts(gameDir, manifest.installId, {
          addonDir: exeDir,
          exclude: manifest.files.map(row => row.rel),
          capture: conflictTarget => journal.capture(gameDir, conflictTarget)
        });
        manifest.conflicts = [...(manifest.conflicts || []), ...found];
        await writeManaged(gameDir, manifest, addon.file, target, 'addon', addon.addonSha256);
        await applyRefUpdate(refContext);
        if (addon.bridgeFile) {
          await writeManaged(gameDir, manifest, addon.bridgeFile,
            installedFile(exeDir, 'bridge'), 'bridge', addon.bridgeSha256);
        }
        if (carrierRequired) {
          await writeManaged(gameDir, manifest, addon.carrierFile, carrierTarget, 'carrier', addon.carrierSha256);
          await enableCarrier(gameDir, exeDir, manifest);
        }
        manifest.payloadVersion = version || addon.id || null;
        manifest.deploymentApi = classifyApi(scan.chosen);
        manifest.carrierDisabledByUser = carrierRequired ? false : manifest.carrierDisabledByUser === true;
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

  async function disableCarrier({ gameDir, scan: suppliedScan, allowAntiCheat = false, manual = true }) {
    const scan = suppliedScan || await scanModule.scanGame(gameDir);
    if (!scan || !scan.chosen || !scan.chosen.path) throw appError('ERR_NO_GAME_EXE');
    const currentManifest = readManifest(gameDir);
    if (!currentManifest) throw appError('ERR_NOT_INSTALLED');
    assertManifestExecutable(gameDir, currentManifest, scan.chosen.path);
    if (!canWrite(path.dirname(scan.chosen.path))) throw appError('ERR_NO_WRITE_ACCESS');
    if (guards.antiCheatPresent(gameDir) && allowAntiCheat !== true) throw appError('ERR_ANTI_CHEAT_CONFIRM', { operation: 'disable-carrier' });
    await guards.assertGameClosed(gameDir, scan.chosen.path);
    return journal.transaction(gameDir, async () => {
      const manifest = readManifest(gameDir);
      if (!manifest) throw appError('ERR_NOT_INSTALLED');
      assertManifestExecutable(gameDir, manifest, scan.chosen.path);
      await retireManagedCarriers(gameDir, manifest);
      const moved = await moveConflicts(gameDir, manifest.installId, {
        addonDir: path.dirname(scan.chosen.path),
        categories: ['native-carrier'],
        exclude: manifest.files.map(row => row.rel),
        capture: target => journal.capture(gameDir, target)
      });
      manifest.conflicts = [...(manifest.conflicts || []), ...moved];
      if (manual) manifest.carrierDisabledByUser = true;
      if (classifyApi(scan.chosen) === 'dx12') manifest.deploymentApi = 'dx12';
      manifest.updatedAt = new Date().toISOString();
      await saveManifest(gameDir, manifest);
      return diagnose({ gameDir, scan });
    });
  }

  async function uninstall({ gameDir, removeSettings = false, scan: suppliedScan }) {
    const manifest = readManifest(gameDir);
    if (!manifest) throw appError('ERR_NOT_INSTALLED');
    const exePath = manifestExecutable(gameDir, manifest);
    await guards.assertGameClosed(gameDir, exePath);

    const warnings = [];
    return journal.transaction(gameDir, async () => {
      for (const row of manifest.files) validateEntry(gameDir, manifest, row, journal.safePath);
      const refContext = await preflightRef(gameDir, exePath, manifest, manifest.files.filter(row => row.kind === 'addon'), false);
      const settingsKept = Boolean(refContext) || !removeSettings;
      // Restoring a filename twice cannot restore both versions. The first
      // quarantine contains the pre-install state; later copies remain in the
      // indexed history instead of producing a half-completed uninstall.
      const conflictRestores = [...new Map([...(manifest.conflicts || [])].reverse()
        .map(row => [path.normalize(row.sourceRel).toLowerCase(), row])).values()];
      const ownedSidecars = [], retainedSidecars = [], archivedSidecars = [];
      for (const row of manifest.sidecars || []) {
        if (!row || typeof row.rel !== 'string' || typeof row.sourceRel !== 'string' ||
            !/^[a-f0-9]{64}$/i.test(row.sha256 || '') ||
            !row.rel.startsWith(`${row.sourceRel}.bak`) ||
            !/^[.]bak(?:[.]\d+)?$/.test(row.rel.slice(row.sourceRel.length))) throw appError('ERR_BACKUP_INVALID');
        journal.safePath(gameDir, row.sourceRel);
        const file = journal.safePath(gameDir, row.rel);
        if (!fs.existsSync(file)) continue;
        if (fs.statSync(file).isFile() && sha256(file) === row.sha256) ownedSidecars.push({ ...row, file });
        else retainedSidecars.push(row.rel);
      }
      const rowTargets = new Map(manifest.files.map(row => [row, managedTargetForRow(gameDir, manifest, row)]));
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
        if (refContext && row.kind === 'config') continue;
        if (row.kind === 'config' && !removeSettings && !row.original.existed) continue;
        const resolution = rowTargets.get(row), target = resolution.target;
        if (resolution.ambiguous) {
          warnings.push({ code: 'ERR_FILE_CHANGED', rel: row.rel, paths: [path.relative(gameDir, resolution.original), path.relative(gameDir, resolution.routed)] });
          continue;
        }
        if (!fs.existsSync(target)) continue;
        const current = fs.statSync(target).isFile() ? sha256(target) : null;
        const protectChange = row.original.existed || row.kind !== 'config';
        if (protectChange && (!row.installedSha256 || current !== row.installedSha256)) {
          warnings.push({ code: 'ERR_FILE_CHANGED', rel: row.rel });
        }
      }
      const managedTargets = new Set(manifest.files.map(row => path.normalize(
        path.relative(gameDir, rowTargets.get(row).target)).toLowerCase()));
      const hasReshadeReceipt = manifest.files.some(row => row.kind === 'reshade');
      const legacyRouteSource = path.join(path.dirname(exePath), 'd3d12.dll');
      if (manifest.reshadeRoute === 'd3d12' && !hasReshadeReceipt && fs.existsSync(legacyRouteSource)) {
        warnings.push({ code: 'ERR_BACKUP_INVALID', rel: path.relative(gameDir, legacyRouteSource), reason: 'unverified-legacy-reshade-route' });
      }
      for (const row of conflictRestores) {
        const source = journal.safePath(gameDir, row.sourceRel);
        const backup = journal.safePath(gameDir, row.backupRel);
        if (fs.existsSync(source) && fs.existsSync(backup) &&
            !managedTargets.has(path.normalize(row.sourceRel).toLowerCase()) &&
            (!fs.statSync(source).isFile() || sha256(source) !== row.sha256)) {
          warnings.push({ code: 'ERR_FILE_CHANGED', rel: row.sourceRel });
        }
      }
      if (warnings.length) return { removed: false, settingsKept, warnings };
      await planRefUpdate(refContext);

      for (const row of [...manifest.files].reverse()) {
        if (refContext && row.kind === 'config') continue;
        if (row.kind === 'config' && !removeSettings && !row.original.existed) continue;
        const resolution = rowTargets.get(row), target = resolution.target;
        await journal.capture(gameDir, target);
        if (row.original.existed) {
          const backup = journal.safePath(gameDir, row.original.backupRel);
          if (resolution.original !== target) {
            await journal.capture(gameDir, resolution.original);
            await copyFile(backup, resolution.original);
            if (fs.existsSync(target)) await fs.promises.unlink(target);
          } else await copyFile(backup, target);
        } else if (fs.existsSync(target)) {
          await fs.promises.unlink(target);
        }
      }
      const restoreWarnings = await restoreConflicts(gameDir, conflictRestores, {
        capture: target => journal.capture(gameDir, target)
      });
      if (restoreWarnings.length) throw appError(restoreWarnings[0].code, { warnings: restoreWarnings });
      const reframeworkMirrors = await applyRefUninstall(refContext, manifest, conflictRestores);
      for (const row of ownedSidecars) {
        // Recheck after asynchronous restore operations. A changed sidecar is
        // retained; it must never prevent restoring the active game files.
        if (!fs.existsSync(row.file)) continue;
        if (!fs.statSync(row.file).isFile() || sha256(row.file) !== row.sha256) { retainedSidecars.push(row.rel); continue; }
        const backupRel = path.join('_DLSS5_Backup', 'xiaofeng-sidecars', manifest.installId, row.rel);
        const backup = journal.safePath(gameDir, backupRel);
        if (fs.existsSync(backup)) throw appError('ERR_FILE_CHANGED', { rel: backupRel });
        await journal.capture(gameDir, row.file);
        await journal.capture(gameDir, backup);
        await fs.promises.mkdir(path.dirname(backup), { recursive: true });
        await fs.promises.rename(row.file, backup);
        archivedSidecars.push({ sourceRel: row.rel, backupRel, sha256: row.sha256 });
      }
      const historyRel = path.join('_DLSS5_Backup', 'xiaofeng-history', `${manifest.installId}.json`);
      const history = journal.safePath(gameDir, historyRel);
      if (fs.existsSync(history)) throw appError('ERR_FILE_CHANGED', { rel: historyRel });
      await journal.capture(gameDir, history);
      await journal.atomicJson(history, { ...manifest, restoredAt: new Date().toISOString(),
        restoration: { conflictSources: conflictRestores.map(row => row.sourceRel), retainedSidecars, archivedSidecars, reframeworkMirrors } });
      const file = manifestPath(gameDir);
      await journal.capture(gameDir, file);
      if (fs.existsSync(file)) await fs.promises.unlink(file);
      return { removed: true, settingsKept, warnings, reframeworkMirrors,
        historyRel, retainedSidecars, archivedConflictCopies: (manifest.conflicts || []).length - conflictRestores.length };
    });
  }

  return { install, repair, upgradeAddon, toggleD3D12, disableCarrier, uninstall, diagnose, isAddonReShade };
}

module.exports = { createInstaller };
