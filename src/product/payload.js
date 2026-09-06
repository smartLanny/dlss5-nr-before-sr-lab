'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { PAYLOAD_FILES } = require('./constants');
const { FAMILIES } = require('./gpu');
const { appError } = require('./errors');

const OPTIONAL_PAYLOAD_FILES = Object.freeze({
  carrier: 'dlss5-native-carrier-exp1.addon64'
});

function sha256(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function payloadRoot(baseDir) {
  return path.join(baseDir, 'payload', 'nr-before-sr');
}

function readBundle(dir) {
  const file = path.join(dir, 'bundle.json');
  if (!fs.existsSync(file)) throw appError('ERR_PAYLOAD_MISSING', { file: 'bundle.json' });
  let bundle;
  try { bundle = JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch { throw appError('ERR_PAYLOAD_HASH', { file: 'bundle.json' }); }
  const legacy = bundle && bundle.version === 1 && bundle.files && typeof bundle.files === 'object';
  const variants = bundle && bundle.version === 2 && bundle.variants && typeof bundle.variants === 'object';
  const versions = bundle && (bundle.version === 3 || bundle.version === 4) && bundle.versions && typeof bundle.versions === 'object';
  if (!legacy && !variants && !versions) {
    throw appError('ERR_PAYLOAD_HASH', { file: 'bundle.json' });
  }
  return bundle;
}

function inspectFiles(dir, expectedFiles) {
  return inspectFilesAt(dir, expectedFiles);
}

function inspectFilesAt(dir, expectedFiles, customPaths = {}) {
  return Object.entries(PAYLOAD_FILES).map(([kind, name]) => {
    const file = customPaths[kind] || path.join(dir, name);
    const exists = fs.existsSync(file) && fs.statSync(file).isFile();
    const actual = exists ? sha256(file) : null;
    const expected = expectedFiles && expectedFiles[name] ? String(expectedFiles[name]).toLowerCase() : null;
    return { kind, name, file, exists, actual, expected, valid: exists && (!expected || actual === expected) };
  });
}

function inspectOptionalFile(file, expected, kind, name) {
  const exists = fs.existsSync(file) && fs.statSync(file).isFile();
  const actual = exists ? sha256(file) : null;
  const normalizedExpected = expected ? String(expected).toLowerCase() : null;
  return {
    kind, name, file, exists, actual, expected: normalizedExpected,
    valid: exists && Boolean(normalizedExpected) && actual === normalizedExpected
  };
}

function inspectOne(dir, expectedFiles) {
  const files = inspectFiles(dir, expectedFiles);
  return {
    dir, files,
    ready: files.every(row => row.valid && row.expected),
    missing: files.filter(row => !row.exists).map(row => row.name),
    invalid: files.filter(row => row.exists && (!row.expected || row.actual !== row.expected)).map(row => row.name)
  };
}

function inspectVersion(dir, entry) {
  const variants = {};
  for (const family of FAMILIES) {
    const variant = entry && entry.variants && entry.variants[family];
    variants[family] = inspectOne(path.join(dir, family), variant && variant.files);
  }
  return {
    id: entry && entry.id,
    label: entry && entry.label,
    notes: entry && entry.notes,
    source: entry && entry.source,
    compatibility: entry && entry.compatibility,
    ota: Boolean(entry && entry.ota),
    variants,
    ready: FAMILIES.every(family => variants[family].ready)
  };
}

function inspectCompactVersion(root, id, entry, fixed) {
  const variants = {};
  for (const family of FAMILIES) {
    const fixedEntry = fixed && fixed[family];
    const versionDir = path.join(root, 'versions', id);
    const fixedDir = path.join(root, 'fixed', family);
    const fixedExpected = fixedEntry && fixedEntry.files ? fixedEntry.files : {};
    const versionExpected = entry && entry.files ? entry.files : {};
    const expected = { ...fixedExpected, ...versionExpected };
    const files = inspectFilesAt('', expected, {
      reshade: path.join(fixedDir, PAYLOAD_FILES.reshade),
      bridge: path.join(fixedDir, PAYLOAD_FILES.bridge),
      runtime: path.join(fixedDir, PAYLOAD_FILES.runtime),
      addon: path.join(versionDir, PAYLOAD_FILES.addon),
      config: path.join(versionDir, PAYLOAD_FILES.config)
    });
    variants[family] = {
      dir: versionDir,
      files,
      ready: files.every(row => row.valid && row.expected),
      missing: files.filter(row => !row.exists).map(row => row.name),
      invalid: files.filter(row => row.exists && (!row.expected || row.actual !== row.expected)).map(row => row.name)
    };
    if (entry && entry.files && entry.files[OPTIONAL_PAYLOAD_FILES.carrier]) {
      const optional = inspectOptionalFile(
        path.join(versionDir, OPTIONAL_PAYLOAD_FILES.carrier),
        entry.files[OPTIONAL_PAYLOAD_FILES.carrier],
        'carrier', OPTIONAL_PAYLOAD_FILES.carrier
      );
      variants[family].files.push(optional);
      variants[family].ready = variants[family].files.every(row => row.valid && row.expected);
      variants[family].missing = variants[family].files.filter(row => !row.exists).map(row => row.name);
      variants[family].invalid = variants[family].files.filter(row => row.exists && !row.valid).map(row => row.name);
    }
  }
  return {
    id,
    label: entry && entry.label,
    notes: entry && entry.notes,
    source: entry && entry.source,
    compatibility: entry && entry.compatibility,
    ota: Boolean(entry && entry.ota),
    variants,
    ready: FAMILIES.every(family => variants[family].ready)
  };
}

function inspectPayload(dir, options = {}) {
  let bundle = null;
  try { bundle = readBundle(dir); }
  catch (error) {
    if (!options.allowMissingBundle) throw error;
  }

  if (bundle && bundle.version === 2) {
    const variants = {};
    for (const family of FAMILIES) {
      const entry = bundle.variants[family];
      variants[family] = inspectOne(path.join(dir, family), entry && entry.files);
    }
    const selectedFamily = options.hardwareFamily;
    const selected = FAMILIES.includes(selectedFamily) ? variants[selectedFamily] : null;
    return {
      dir, bundle, variants, hardwareFamily: selectedFamily || null,
      files: selected ? selected.files : [],
      ready: selected ? selected.ready : FAMILIES.every(family => variants[family].ready),
      missing: [...new Set(FAMILIES.flatMap(family => variants[family].missing.map(name => `${family}/${name}`)))],
      invalid: [...new Set(FAMILIES.flatMap(family => variants[family].invalid.map(name => `${family}/${name}`)))]
    };
  }

  if (bundle && bundle.version === 3) {
    const versions = {};
    for (const [id, entry] of Object.entries(bundle.versions || {})) {
      versions[id] = inspectVersion(path.join(dir, 'versions', id), { ...entry, id });
    }
    const ids = Object.keys(versions);
    const selectedVersion = ids.includes(options.version)
      ? options.version
      : (ids.includes(bundle.defaultVersion) ? bundle.defaultVersion : ids[0]);
    const selected = selectedVersion ? versions[selectedVersion] : null;
    const family = options.hardwareFamily;
    const selectedVariant = selected && FAMILIES.includes(family) ? selected.variants[family] : null;
    const files = selectedVariant ? selectedVariant.files : [];
    return {
      dir, bundle, versions, selectedVersion: selectedVersion || null,
      hardwareFamily: family || null, files,
      ready: Boolean(selectedVariant ? selectedVariant.ready : selected && selected.ready),
      missing: files.filter(row => !row.exists || !row.expected).map(row => row.name),
      invalid: files.filter(row => row.exists && !row.valid).map(row => row.name)
    };
  }

  if (bundle && bundle.version === 4) {
    const versions = {};
    for (const [id, entry] of Object.entries(bundle.versions || {})) {
      versions[id] = inspectCompactVersion(dir, id, { ...entry, id }, bundle.fixed || {});
    }
    const ids = Object.keys(versions);
    const selectedVersion = ids.includes(options.version)
      ? options.version
      : (ids.includes(bundle.defaultVersion) ? bundle.defaultVersion : ids[0]);
    const selected = selectedVersion ? versions[selectedVersion] : null;
    const family = options.hardwareFamily;
    const selectedVariant = selected && FAMILIES.includes(family) ? selected.variants[family] : null;
    const files = selectedVariant ? selectedVariant.files : [];
    return {
      dir, bundle, versions, selectedVersion: selectedVersion || null,
      hardwareFamily: family || null, files,
      ready: Boolean(selectedVariant ? selectedVariant.ready : selected && selected.ready),
      missing: files.filter(row => !row.exists || !row.expected).map(row => row.name),
      invalid: files.filter(row => row.exists && !row.valid).map(row => row.name)
    };
  }

  const legacy = inspectOne(dir, bundle && bundle.files);
  return { dir, bundle, files: legacy.files, ready: Boolean(bundle) && legacy.ready, missing: legacy.missing, invalid: legacy.invalid };
}

function requirePayload(dir, hardwareFamily, version) {
  const result = inspectPayload(dir, { hardwareFamily, version });
  if ((result.variants || result.versions) && !FAMILIES.includes(hardwareFamily)) throw appError('ERR_GPU_UNSUPPORTED');
  if (result.versions && !result.selectedVersion) throw appError('ERR_PAYLOAD_MISSING', { file: 'bundle.versions' });
  if (result.missing.length) throw appError('ERR_PAYLOAD_MISSING', { files: result.missing });
  if (!result.ready) throw appError('ERR_PAYLOAD_HASH', { files: result.invalid });
  const payload = Object.fromEntries(result.files.map(row => [row.kind, row]));
  payload.hardwareFamily = hardwareFamily || null;
  payload.version = result.selectedVersion || null;
  payload.versionInfo = result.versions && result.versions[result.selectedVersion]
      ? { id: result.selectedVersion, label: result.versions[result.selectedVersion].label, notes: result.versions[result.selectedVersion].notes,
        compatibility: result.versions[result.selectedVersion].compatibility || null,
        ota: Boolean(result.versions[result.selectedVersion].ota) }
    : null;
  return payload;
}

function createBundle(dir) {
  const files = {};
  for (const name of Object.values(PAYLOAD_FILES)) {
    const file = path.join(dir, name);
    if (!fs.existsSync(file)) throw appError('ERR_PAYLOAD_MISSING', { file: name });
    files[name] = sha256(file);
  }
  return { version: 1, generatedAt: new Date().toISOString(), files };
}

function createVariantsBundle(dir) {
  const variants = Object.fromEntries(FAMILIES.map(family => [family, { files: createBundle(path.join(dir, family)).files }]));
  return { version: 2, generatedAt: new Date().toISOString(), variants };
}

function createVersionedBundle(dir, entries, defaultVersion) {
  const versions = {};
  for (const entry of entries) {
    const id = String(entry.id);
    const base = path.join(dir, 'versions', id);
    versions[id] = {
      label: entry.label || id,
      notes: entry.notes || '',
      source: entry.source || '',
      variants: Object.fromEntries(FAMILIES.map(family => [family, {
        files: createBundle(path.join(base, family)).files
      }]))
    };
  }
  return { version: 3, generatedAt: new Date().toISOString(), defaultVersion, versions };
}

function createCompactBundle(dir, entries, defaultVersion) {
  const fixed = Object.fromEntries(FAMILIES.map(family => [family, {
    files: Object.fromEntries(['reshade', 'bridge', 'runtime'].map(kind => {
      const name = PAYLOAD_FILES[kind];
      const file = path.join(dir, 'fixed', family, name);
      if (!fs.existsSync(file)) throw appError('ERR_PAYLOAD_MISSING', { file: `fixed/${family}/${name}` });
      return [name, sha256(file)];
    }))
  }]));
  const versions = Object.fromEntries(entries.map(entry => {
    const id = String(entry.id);
    const base = path.join(dir, 'versions', id);
    const files = {};
    for (const kind of ['addon', 'config']) {
      const name = PAYLOAD_FILES[kind];
      const file = path.join(base, name);
      if (!fs.existsSync(file)) throw appError('ERR_PAYLOAD_MISSING', { file: `versions/${id}/${name}` });
      files[name] = sha256(file);
    }
    const carrier = path.join(base, OPTIONAL_PAYLOAD_FILES.carrier);
    if (fs.existsSync(carrier)) files[OPTIONAL_PAYLOAD_FILES.carrier] = sha256(carrier);
    return [id, { label: entry.label || id, notes: entry.notes || '', source: entry.source || '', compatibility: entry.compatibility || null, ota: Boolean(entry.ota), files }];
  }));
  return { version: 4, generatedAt: new Date().toISOString(), defaultVersion, fixed, versions };
}

module.exports = {
  sha256, payloadRoot, readBundle, inspectPayload, requirePayload,
  createBundle, createVariantsBundle, createVersionedBundle, createCompactBundle, OPTIONAL_PAYLOAD_FILES
};
