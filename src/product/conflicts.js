'use strict';

const fs = require('fs');
const path = require('path');
const { sha256 } = require('./payload');

const BACKUP_DIR = '_DLSS5_Backup';
const ADDON_EXT = /\.addon(?:32|64)?$/i;

function isProtectedName(name) {
  return /^(?:dxgi|d3d12|nvngx_dlss|nvngx_dlssg)\.dll$/i.test(name);
}

function classifyAddon(name, file) {
  const lower = name.toLowerCase();
  if (lower === 'renodx-dlss5.addon64') {
    return { category: 'dlss5-tool', reason: '原版 DLSS5 Tool（RHI 中显示为 DLSS5 Tool）' };
  }
  if (/nr[-_ ]?before[-_ ]?sr/i.test(lower)) {
    return { category: 'legacy-nr-before-sr', reason: '旧版 NR-before-SR Addon' };
  }
  // Ordinary RenoDX color/HDR addons do not touch the DLSS/NR/NGX path and
  // are explicitly left in place.
  if (lower.includes('renodx') && !/(dlss|ngx|nr)/i.test(lower)) return null;
  if (/(dlss|dlssg|ngx|nr|optiscaler|streamline)/i.test(lower)) {
    return { category: 'other-dlss-nr-ngx', reason: '可能修改 DLSS、NR 或 NGX 的 ReShade Addon' };
  }

  // Some addons use neutral filenames. A small string check catches the
  // common self-identifying binaries without touching ordinary DLLs.
  if (file) {
    try {
      const bytes = fs.readFileSync(file);
      const text = bytes.toString('latin1');
      if (/(DLSS|DLSSG|NVNGX|NRBeforeSR|OptiScaler)/i.test(text)) {
        return { category: 'other-dlss-nr-ngx', reason: 'Addon 内容声明修改 DLSS、NR 或 NGX' };
      }
    } catch {}
  }
  return null;
}

function walkAddons(root, maxDepth = 4) {
  const result = [];
  function visit(dir, depth) {
    if (depth > maxDepth) return;
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const entry of entries) {
      if (entry.name === BACKUP_DIR || entry.name === 'node_modules' || entry.name.startsWith('.')) continue;
      const file = path.join(dir, entry.name);
      if (entry.isDirectory()) visit(file, depth + 1);
      else if (entry.isFile() && ADDON_EXT.test(entry.name) && !isProtectedName(entry.name)) result.push(file);
    }
  }
  visit(path.resolve(root), 0);
  return result;
}

function scanConflicts(gameDir, options = {}) {
  const root = path.resolve(gameDir);
  const excluded = new Set((options.exclude || []).map(rel => path.normalize(rel).toLowerCase()));
  return walkAddons(root).flatMap(file => {
    const name = path.basename(file);
    const rel = path.relative(root, file);
    if (excluded.has(path.normalize(rel).toLowerCase())) return [];
    const classification = classifyAddon(name, file);
    if (!classification) return [];
    return [{
      source: file,
      rel,
      name,
      ...classification,
      sha256: sha256(file)
    }];
  });
}

function conflictBackupPath(gameDir, installId, rel, index = 0) {
  const safeRel = path.normalize(rel);
  const suffix = index ? `.conflict.${index}` : '';
  return path.join(gameDir, BACKUP_DIR, 'conflicts', installId, `${safeRel}${suffix}`);
}

async function moveConflicts(gameDir, installId, options = {}) {
  const moved = [];
  for (const conflict of scanConflicts(gameDir, options)) {
    if (!fs.existsSync(conflict.source)) continue;
    let destination = conflictBackupPath(gameDir, installId, conflict.rel);
    let index = 0;
    while (fs.existsSync(destination)) {
      index += 1;
      destination = conflictBackupPath(gameDir, installId, conflict.rel, index);
    }
    await fs.promises.mkdir(path.dirname(destination), { recursive: true });
    await fs.promises.rename(conflict.source, destination);
    moved.push({
      sourceRel: conflict.rel,
      backupRel: path.relative(gameDir, destination),
      name: conflict.name,
      category: conflict.category,
      reason: conflict.reason,
      sha256: conflict.sha256
    });
  }
  return moved;
}

async function restoreConflicts(gameDir, rows) {
  const warnings = [];
  for (const row of Array.isArray(rows) ? rows : []) {
    if (!row || typeof row.sourceRel !== 'string' || typeof row.backupRel !== 'string') continue;
    const source = path.join(gameDir, row.sourceRel);
    const backup = path.join(gameDir, row.backupRel);
    if (!fs.existsSync(backup)) continue;
    if (fs.existsSync(source)) {
      warnings.push({ code: 'ERR_FILE_CHANGED', rel: row.sourceRel });
      continue;
    }
    await fs.promises.mkdir(path.dirname(source), { recursive: true });
    await fs.promises.rename(backup, source);
  }
  return warnings;
}

module.exports = {
  isProtectedName,
  classifyAddon,
  scanConflicts,
  moveConflicts,
  restoreConflicts
};
