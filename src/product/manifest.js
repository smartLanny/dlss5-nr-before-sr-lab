'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { appError } = require('./errors');

const BACKUP_DIR = '_DLSS5_Backup';
const PRODUCT_MANIFEST = 'xiaofeng-manager.json';

function manifestPath(gameDir) {
  return path.join(gameDir, BACKUP_DIR, PRODUCT_MANIFEST);
}

function backupPath(gameDir, installId, rel) {
  const normalized = path.normalize(rel);
  return path.join(gameDir, BACKUP_DIR, 'xiaofeng-originals', installId, normalized);
}

function readManifest(gameDir) {
  const file = manifestPath(gameDir);
  if (!fs.existsSync(file)) return null;
  let data;
  try { data = JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch { throw appError('ERR_BACKUP_INVALID'); }
  if (!data || data.version !== 1 || data.product !== 'xiaofeng-dlss5-manager' ||
      !data.game || !Array.isArray(data.files) || !/^[a-f0-9-]{36}$/i.test(data.installId || '')) {
    throw appError('ERR_BACKUP_INVALID');
  }
  return data;
}

function newManifest(gameDir, exePath, api) {
  return {
    version: 1,
    product: 'xiaofeng-dlss5-manager',
    installId: crypto.randomUUID(),
    installedAt: new Date().toISOString(),
    updatedAt: new Date().toISOString(),
    game: {
      dir: path.resolve(gameDir),
      exe: path.relative(gameDir, exePath),
      api
    },
    files: []
  };
}

function findEntry(manifest, rel) {
  const key = path.normalize(rel).toLowerCase();
  return manifest.files.find(row => path.normalize(row.rel).toLowerCase() === key) || null;
}

function validateEntry(gameDir, manifest, row, safePath) {
  if (!row || typeof row.rel !== 'string' || !row.original || typeof row.original.existed !== 'boolean') {
    throw appError('ERR_BACKUP_INVALID');
  }
  safePath(gameDir, row.rel);
  if (row.original.existed) {
    if (typeof row.original.backupRel !== 'string') throw appError('ERR_BACKUP_INVALID');
    const file = safePath(gameDir, row.original.backupRel);
    if (!fs.existsSync(file)) throw appError('ERR_BACKUP_INVALID', { rel: row.rel });
  }
}

module.exports = {
  BACKUP_DIR,
  PRODUCT_MANIFEST,
  manifestPath,
  backupPath,
  readManifest,
  newManifest,
  findEntry,
  validateEntry
};
