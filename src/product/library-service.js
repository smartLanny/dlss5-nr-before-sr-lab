'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { pathToFileURL } = require('url');
const { assess, isDx11Only } = require('./game-support');
const { annotateApi } = require('./api-evidence');
const { createLaunchContext } = require('./launch-evidence');
const { DX11_COMPAT_VERSION } = require('./constants');
const { readManifest } = require('./manifest');
const { MESSAGES } = require('./errors');
const pe = require('../core/pe');

function idFor(dir) {
  return crypto.createHash('sha1').update(path.resolve(dir).toLowerCase()).digest('hex').slice(0, 16);
}

function posterUrl(poster) {
  if (!poster) return null;
  if (typeof poster === 'string') return poster;
  if (!poster.file) return null;
  return pathToFileURL(poster.file).href;
}

function steamArtworkFor(game, directoryCache = new Map()) {
  if (!game || game.launcher !== 'Steam' || !game.steamRoot || !game.id) return game;
  const appid = String(game.id);
  const cacheDirs = [
    path.join(game.steamRoot, 'appcache', 'librarycache', appid),
    path.join(game.steamRoot, 'appcache', 'librarycache')
  ];
  const files = [];
  for (const dir of cacheDirs) {
    let cached = directoryCache.get(dir);
    if (!cached) {
      try { cached = fs.readdirSync(dir, { withFileTypes: true }).filter(entry => entry.isFile()).map(entry => ({ name: entry.name, file: path.join(dir, entry.name) })); }
      catch { cached = []; }
      directoryCache.set(dir, cached);
    }
    files.push(...cached);
  }
  const match = (pattern) => files.find(row => pattern.test(row.name));
  const cover = match(new RegExp(`^(?:${appid}_)?library_600x900(?:_[^.]*)?\\.(?:jpg|jpeg|png)$`, 'i'));
  const header = match(new RegExp(`^(?:${appid}_)?(?:library_header|header)(?:_[^.]*)?\\.(?:jpg|jpeg|png)$`, 'i'));
  const hero = match(new RegExp(`^(?:${appid}_)?library_hero(?:_[^.]*)?\\.(?:jpg|jpeg|png)$`, 'i'));
  const logo = match(new RegExp(`^(?:${appid}_)?logo\\.(?:png|jpg|jpeg)$`, 'i'));
  const currentPoster = game.poster && game.poster.file && fs.existsSync(game.poster.file) ? game.poster : null;
  const poster = cover || header || currentPoster || hero;
  return {
    ...game,
    poster: poster ? { file: poster.file || poster, tall: /library_600x900/i.test(poster.name || poster.file || '') } : null,
    steamIcon: logo ? pathToFileURL(logo.file).href : game.steamIcon || null
  };
}

const UNITY_API_MARKERS = [
  'D3D12CreateDevice', 'D3D12SDKPath', 'D3D12SDKVersion',
  'D3D11CreateDevice', 'CreateDXGIFactory'
];

function findFileCaseInsensitive(dir, wanted) {
  try {
    const name = fs.readdirSync(dir).find(entry => entry.toLowerCase() === wanted.toLowerCase());
    return name ? path.join(dir, name) : null;
  } catch {
    return null;
  }
}

// UnityPlayer.dll imports OpenGL on some builds even when the game selects a
// DirectX renderer dynamically. When the same Unity module contains explicit
// D3D markers and the game ships native DLSS, OpenGL is a false first match:
// the game is a valid DXGI/ReShade candidate and should not be rejected.
function normalizeUnityDynamicApi(scan, root, markerReader = pe.findMarkers) {
  if (!scan || !scan.chosen || String(scan.chosen.api).toLowerCase() !== 'opengl') return scan;
  if (!scan.primaryDlss && !(scan.dlssFiles && scan.dlssFiles.length)) return scan;
  const chosenDir = path.dirname(scan.chosen.path);
  const unity = findFileCaseInsensitive(chosenDir, 'UnityPlayer.dll') || findFileCaseInsensitive(root, 'UnityPlayer.dll');
  if (!unity) return scan;
  const markers = markerReader(unity, UNITY_API_MARKERS);
  const hasD3D12 = markers.has('D3D12CreateDevice') || markers.has('D3D12SDKPath') || markers.has('D3D12SDKVersion');
  const hasD3D11 = markers.has('D3D11CreateDevice') || markers.has('CreateDXGIFactory');
  if (!hasD3D12 && !hasD3D11) return scan;
  const apiLabel = hasD3D12 && hasD3D11 ? 'DirectX 11/12' : hasD3D12 ? 'DirectX 12' : 'DirectX 11';
  const normalize = candidate => path.resolve(candidate.path).toLowerCase() === path.resolve(scan.chosen.path).toLowerCase()
    ? { ...candidate, api: 'dxgi', apiLabel, via: 'module:UnityPlayer.dll（动态 DirectX）', dynamic: true, dx12: hasD3D12,
        apiChoices: [{ api: 'dxgi', label: apiLabel }] }
    : candidate;
  return { ...scan, chosen: normalize(scan.chosen), exeCandidates: (scan.exeCandidates || []).map(normalize) };
}

async function mapLimit(items, limit, worker) {
  const result = new Array(items.length);
  let cursor = 0;
  async function run() {
    while (cursor < items.length) {
      const index = cursor++;
      try { result[index] = await worker(items[index], index); }
      catch (error) { result[index] = { error }; }
    }
  }
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, run));
  return result;
}

function createLibraryService(overrides = {}) {
  const library = overrides.library || require('../library');
  const scanModule = overrides.scan || require('../core/scan');

  function samePath(left, right) {
    return path.resolve(left).toLowerCase() === path.resolve(right).toLowerCase();
  }

  function isInside(file, root) {
    const candidate = path.resolve(file).toLowerCase();
    const parent = path.resolve(root).toLowerCase();
    return candidate === parent || candidate.startsWith(`${parent}${path.sep}`);
  }

  function isExcludedGame(game, state) {
    return (state.excludedGames || []).some(row => {
      if (row.dir && (samePath(game.dir, row.dir) || (row.executable && isInside(row.executable, game.dir)))) return true;
      if (row.executable && samePath(game.dir, path.dirname(row.executable))) return true;
      const sameLauncher = !row.launcher || !game.launcher || row.launcher === game.launcher;
      const gameId = game.id == null ? null : String(game.id);
      if (sameLauncher && gameId && ((row.id && row.id === gameId) || (row.appid && row.appid === gameId))) return true;
      return false;
    });
  }

  function choosePreferred(scan, preferredExecutable) {
    if (!scan || !preferredExecutable || !Array.isArray(scan.exeCandidates)) return scan;
    const chosen = scan.exeCandidates.find(candidate => samePath(candidate.path, preferredExecutable));
    if (!chosen) return scan;
    return {
      ...scan,
      chosen,
      primaryDlss: typeof scanModule.selectPrimaryDlss === 'function'
        ? scanModule.selectPrimaryDlss(scan.dlssFiles || [], chosen)
        : scan.primaryDlss
    };
  }

  async function findGameRoot(executable) {
    let current = path.dirname(path.resolve(executable));
    let fallback = current;
    let fallbackScan = null;
    for (let depth = 0; depth < 7; depth += 1) {
      let scan = null;
      try { scan = await scanModule.scanGame(current); } catch {}
      // A previously installed manager runtime (`nvngx_dlssnr.dll`) is not
      // the game's native DLSS SR. Only stop at a directory that has the
      // actual `nvngx_dlss.dll`; otherwise keep walking toward UE's Engine /
      // Plugins / Nvidia / DLSS tree.
      const hasNativeDlss = scan && Array.isArray(scan.dlssFiles) &&
        scan.dlssFiles.some(file => /^nvngx_dlss\.dll$/i.test(file && file.name || ''));
      if (hasNativeDlss && scan.primaryDlss && /^nvngx_dlss\.dll$/i.test(scan.primaryDlss.name || '')) return { root: current, scan };
      fallback = current;
      fallbackScan = scan;
      const parent = path.dirname(current);
      if (parent === current) break;
      // Walk out of known binary layouts only. A missing native DLSS DLL
      // must never turn one EXE selection into a scan of Games/AppData/a drive.
      const technical = /^(?:binaries|bin|win(?:32|64)(?:[._ -].*)?|x64|x86|client|windowsnoeditor|shipping|build|dist|release)$/i;
      const unrealProjectParent = /^windowsnoeditor$/i.test(path.basename(parent)) ||
        fs.existsSync(path.join(parent, 'Engine', 'Binaries')) || fs.existsSync(path.join(parent, 'Engine', 'Plugins'));
      if (!technical.test(path.basename(current)) && !unrealProjectParent) break;
      if (parent === path.parse(parent).root) break;
      current = parent;
    }
    return { root: fallback, scan: fallbackScan };
  }

  function likelyHelper(candidate) {
    return /(?:browser|webbooster|err(?:or)?rep|crash|reporter|launcher|setup|install|unins|helper|service|platformprocess|qtweb|cefview|updater|patcher|bootstrap|anti.?cheat|ace[-_])/i.test(candidate.name || '');
  }

  function isGameDirectory(rel) {
    return /(?:^|[\\/])(?:games?|client|game|binaries|win(?:32|64)|x6game)(?:[\\/]|$)/i.test(String(rel || ''));
  }

  function isLikelyHelper(candidate, root) {
    if (likelyHelper(candidate)) return true;
    const rootPath = String(root || '').replace(/\\/g, '/');
    const stem = path.basename(candidate.name || candidate.path || '', '.exe').toLowerCase();
    // Bannerlord ships both a small bootstrapper and a Native/Story tool in
    // the same Win64 directory. Keep Native selectable, but do not let it
    // outrank Bannerlord.exe when the user scans the folder.
    if (/mount\s*&\s*blade\s*ii\s*bannerlord|bannerlord/i.test(rootPath) && /^bannerlord\.native$/i.test(stem)) return true;
    const rootName = path.basename(root || '');
    // Launcher roots commonly contain several small launcher processes next
    // to a `games\\...` tree. Keep them visible, but lower their priority.
    return /launcher/i.test(rootName) && !isGameDirectory(candidate.rel);
  }

  function apiEvidence(file) {
    let imports = [];
    try { imports = pe.getImports(file); } catch {}
    const names = new Set(imports.map(name => path.basename(name).toLowerCase()));
    if (names.has('d3d12.dll')) return { api: 'dxgi', apiLabel: 'DirectX 12', via: 'imports' };
    if (names.has('d3d11.dll')) return { api: 'dxgi', apiLabel: 'DirectX 11', via: 'imports' };
    if (names.has('dxgi.dll')) return { api: 'dxgi', apiLabel: 'DirectX (DXGI)', via: 'imports' };
    try {
      const markers = pe.findMarkers(file, ['D3D12CreateDevice', 'D3D11CreateDevice', 'CreateDXGIFactory']);
      if (markers.has('D3D12CreateDevice')) return { api: 'dxgi', apiLabel: 'DirectX 12', via: 'strings' };
      if (markers.has('D3D11CreateDevice')) return { api: 'dxgi', apiLabel: 'DirectX 11', via: 'strings' };
      if (markers.has('CreateDXGIFactory')) return { api: 'dxgi', apiLabel: 'DirectX (DXGI)', via: 'strings' };
    } catch {}
    return null;
  }

  function candidateScore(candidate, root) {
    const rel = String(candidate.rel || '').replace(/\\/g, '/').toLowerCase();
    const stem = path.basename(candidate.name || candidate.path || '', '.exe').toLowerCase();
    const rootStem = path.basename(root).replace(/[.\- _]+/g, '').toLowerCase();
    let score = 0;
    if (isLikelyHelper(candidate, root)) score -= 10000;
    if (/(?:^|[\\/])games?(?:[\\/]|$)/i.test(rel)) score += 240;
    if (/(?:^|[\\/])(?:client|game)(?:[\\/]|$)/i.test(rel)) score += 45;
    if (/binaries\/win64(?:\/|$)/.test(rel)) score += 80;
    if (/(?:^|[^a-z])(x6game|htgame|endfield)(?:$|[^a-z])/.test(stem)) score += 70;
    if (/mount\s*&\s*blade\s*ii\s*bannerlord|bannerlord/i.test(path.resolve(root || '')) && stem === 'bannerlord') score += 180;
    if (/mount\s*&\s*blade\s*ii\s*bannerlord|bannerlord/i.test(path.resolve(root || '')) && stem === 'bannerlord.native') score -= 180;
    if (rootStem && stem.replace(/[.\- _]+/g, '') === rootStem) score += 100;
    if (/launcher/i.test(path.basename(root)) && !isGameDirectory(rel)) score -= 80;
    if (candidate.apiLabel && !/unknown/i.test(candidate.apiLabel)) score += 20;
    if (candidate.via && !/^fallback$/i.test(candidate.via)) score += 10;
    return score;
  }

  function applyChosen(scan, chosen) {
    if (!scan || !chosen) return scan;
    return {
      ...scan,
      chosen,
      primaryDlss: typeof scanModule.selectPrimaryDlss === 'function'
        ? scanModule.selectPrimaryDlss(scan.dlssFiles || [], chosen)
        : scan.primaryDlss
    };
  }

  function chooseBest(scan, root, candidates, preferredExecutable = null) {
    const rows = Array.isArray(candidates) ? candidates : (scan && scan.exeCandidates) || [];
    const preferred = preferredExecutable && rows.find(candidate => samePath(candidate.path, preferredExecutable));
    if (preferred) return applyChosen({ ...scan, exeCandidates: rows }, preferred);
    const best = [...rows]
      .filter(candidate => !isLikelyHelper(candidate, root))
      .sort((left, right) => candidateScore(right, root) - candidateScore(left, root))[0];
    return best ? applyChosen({ ...scan, exeCandidates: rows }, best) : { ...scan, exeCandidates: rows };
  }

  async function collectCandidates(root, scan) {
    const existing = new Map((scan.exeCandidates || []).map(candidate => [path.resolve(candidate.path).toLowerCase(), candidate]));
    const rows = [...(scan.exeCandidates || [])];
    if (typeof scanModule.walk !== 'function') return rows;
    const hasDlss = Boolean(scan.dlssFiles && scan.dlssFiles.length);
    await scanModule.walk(root, async (full, name, depth) => {
      if (!/\.exe$/i.test(name)) return;
      const key = path.resolve(full).toLowerCase();
      if (existing.has(key)) return;
      let bitness = null;
      try { bitness = pe.getBitness(full); } catch {}
      if (!bitness) return;
      const rel = path.relative(root, full);
      const evidence = apiEvidence(full);
      const stem = path.basename(name, '.exe').toLowerCase();
      const relLower = rel.replace(/\\/g, '/').toLowerCase();
      const strongPath = /binaries\/win64(?:\/|$)/.test(relLower) ||
        /(?:^|\/)games?(?:\/|$)/.test(relLower) ||
        stem === path.basename(root).toLowerCase().replace(/[.\- _]+/g, '') ||
        /(?:x6game|htgame|endfield)/i.test(stem);
      const chosenDir = scan && scan.chosen && scan.chosen.path ? path.dirname(scan.chosen.path) : null;
      const sameExeDirectory = chosenDir && path.resolve(chosenDir).toLowerCase() === path.resolve(path.dirname(full)).toLowerCase();
      // Manual selection must also expose sibling EXEs that have no readable
      // DirectX import (launchers/dispatchers often do not). They remain
      // visibly “manual” unless normal API/DLSS evidence recommends them.
      if (!evidence && !(hasDlss && strongPath) && !sameExeDirectory) return;
      let size = 0;
      try { size = fs.statSync(full).size; } catch {}
      rows.push({
        path: full, rel, name, size, depth, bitness,
        api: evidence ? evidence.api : 'unknown',
        apiLabel: evidence ? evidence.apiLabel : '图形 API 未知（手动可选）',
        via: evidence ? evidence.via : 'manual-sibling',
        dx12: Boolean(evidence && evidence.apiLabel === 'DirectX 12')
      });
      existing.set(key, rows[rows.length - 1]);
    }, 12, { includeContent: true });
    return rows;
  }

  function defaultGameName(dir) {
    const technical = /^(windowsnoeditor|binaries|win(?:32|64)(?:[._ -].*)?|shipping(?:[._ -].*)?|client|game|content|bin|x(?:64|86)|build|dist|release)$/i;
    let current = path.resolve(dir);
    for (let depth = 0; depth < 6; depth += 1) {
      const name = path.basename(current);
      if (name && !technical.test(name)) return name;
      const parent = path.dirname(current);
      if (parent === current) break;
      current = parent;
    }
    return path.basename(path.resolve(dir));
  }

  async function prepareSelection(source, preferredExecutable = null) {
    if (typeof source !== 'string' || !path.isAbsolute(source)) throw new Error('invalid selection source');
    const isExecutable = /\.exe$/i.test(source);
    const inferred = isExecutable ? await findGameRoot(source) : { root: path.resolve(source), scan: null };
    const root = inferred.root;
    const scan = normalizeUnityDynamicApi(inferred.scan || await scanModule.scanGame(root), root);
    const preferred = preferredExecutable || (isExecutable ? source : null);
    const rawCandidates = await collectCandidates(root, scan);
    const candidateRows = rawCandidates.map(candidate => ({
      path: candidate.path,
      rel: candidate.rel,
      name: candidate.name,
      size: candidate.size,
      api: candidate.api,
      apiLabel: candidate.apiLabel,
      bitness: candidate.bitness,
      dx12: Boolean(candidate.dx12),
      helper: isLikelyHelper(candidate, root),
      score: candidateScore(candidate, root)
    }));
    // The user-selected EXE is authoritative. Some bootstrappers and game
    // dispatchers have no readable DirectX import and are therefore absent
    // from the heuristic scan; they must still appear selected in this
    // confirmation dialog instead of silently falling back to a recommendation.
    if (preferred && !candidateRows.some(candidate => samePath(candidate.path, preferred))) {
      const preferredPath = path.resolve(preferred);
      const relative = path.relative(path.resolve(root), preferredPath);
      if (!relative.startsWith('..') && !path.isAbsolute(relative) && fs.existsSync(preferredPath)) {
        let bitness = null;
        let size = 0;
        try { bitness = pe.getBitness(preferredPath); } catch {}
        try { size = fs.statSync(preferredPath).size; } catch {}
        const name = path.basename(preferredPath);
        const evidence = apiEvidence(preferredPath);
        const manual = {
          path: preferredPath,
          rel: relative,
          name,
          size,
          api: evidence ? evidence.api : 'unknown',
          apiLabel: evidence ? evidence.apiLabel : '图形 API 未知（已选择）',
          bitness: bitness || null,
          dx12: Boolean(evidence && evidence.apiLabel === 'DirectX 12'),
          helper: isLikelyHelper({ path: preferredPath, name }, root),
          score: candidateScore({ path: preferredPath, name, rel: relative, apiLabel: evidence && evidence.apiLabel, via: evidence ? evidence.via : 'manual-selection' }, root)
        };
        candidateRows.push(manual);
      }
    }
    const candidates = candidateRows.map(candidate => ({
      ...candidate,
      recommended: candidate.score > 0 && !candidate.helper
    })).sort((left, right) => right.score - left.score || left.rel.localeCompare(right.rel, 'zh-CN', { numeric: true }));
    const recommended = !preferred
      ? [...candidates].filter(candidate => candidate.recommended).sort((left, right) => right.score - left.score)[0]
      : null;
    const preferredCandidate = preferred && candidates.find(candidate => samePath(candidate.path, preferred));
    const selectedPath = (preferredCandidate && preferredCandidate.path) || (recommended && recommended.path) || null;
    const selectedCandidate = candidates.find(candidate => candidate.path === selectedPath) || null;
    let gameRoot = root;
    if (!isExecutable && selectedCandidate) {
      const inferredGameRoot = await findGameRoot(selectedCandidate.path);
      if (inferredGameRoot.scan) gameRoot = inferredGameRoot.root;
    }
    return {
      root: gameRoot,
      name: defaultGameName(gameRoot),
      chosen: selectedCandidate ? {
        path: selectedCandidate.path,
        rel: selectedCandidate.rel,
        name: selectedCandidate.name,
        api: selectedCandidate.api,
        apiLabel: selectedCandidate.apiLabel,
        bitness: selectedCandidate.bitness,
        size: selectedCandidate.size
      } : null,
      candidates
    };
  }

  async function scanAll(state) {
    const launchContext = createLaunchContext();
    const discovered = library.discover(
      state.scanFolders || [],
      state.scanDrives === true,
      state.excludedRoots || []
    );
    const manualByRoot = new Map((state.manualGames || []).map(dir => [path.resolve(dir).toLowerCase(), {
      launcher: '手动添加', id: null, name: defaultGameName(dir), dir, poster: null, preferredExecutable: null
    }]));
    for (const row of state.manualExecutables || []) {
      const key = path.resolve(row.root).toLowerCase();
      const existing = manualByRoot.get(key) || {
        launcher: '手动添加', id: null, name: defaultGameName(row.root), dir: row.root, poster: null, preferredExecutable: null
      };
      existing.preferredExecutable = row.file;
      manualByRoot.set(key, existing);
    }
    const manual = [...manualByRoot.values()];
    const artworkDirectories = new Map();
    const discoveredGames = (discovered.games || []).map(game => steamArtworkFor(game, artworkDirectories));
    const candidates = library.dedupe([...discoveredGames, ...manual]).filter(game => !isExcludedGame(game, state));
    const rows = await mapLimit(candidates, 2, async game => {
      let scan;
      try {
        const baseScan = normalizeUnityDynamicApi(await scanModule.scanGame(game.dir), game.dir);
        if (game.launcher === '手动添加') {
          // Manual folders may be launcher roots. Broaden only these scans so
          // a nested real executable (for example Endfield.exe) can outrank
          // CefView/QtWebEngine helpers without slowing every library refresh.
          const rawCandidates = await collectCandidates(game.dir, baseScan);
          scan = chooseBest(baseScan, game.dir, rawCandidates, game.preferredExecutable);
        } else {
          scan = choosePreferred(baseScan, game.preferredExecutable);
        }
        // A saved root is explicit. Refresh must not infer a broader root
        // when this game lacks native DLSS or its files are temporarily absent.
      } catch (error) {
        return {
          id: idFor(game.dir),
          name: game.name || path.basename(game.dir),
          launcher: game.launcher || '本地游戏',
          dir: game.dir,
          poster: posterUrl(game.poster),
          chosen: null,
          supported: false,
          supportCode: 'ERR_INTERNAL',
          supportText: '扫描失败，请检查目录权限后重试。',
          installed: false,
          scan: null,
          scanError: error && error.message ? error.message : 'scan failed'
        };
      }
      const override = state.gameOverrides && state.gameOverrides[path.resolve(game.dir).toLowerCase()];
      const apiOverride = override && override.apiExecutable && scan.chosen &&
        samePath(override.apiExecutable, scan.chosen.path) ? override.api : 'auto';
      scan = annotateApi(scan, game.dir, { ...launchContext(game), apiOverride });
      let manifest = null;
      try { manifest = readManifest(game.dir); } catch {}
      const carrierOverride = override && override.carrierExecutable && scan.chosen &&
        samePath(override.carrierExecutable, scan.chosen.path) && typeof override.carrierEnabled === 'boolean'
        ? override.carrierEnabled : null;
      const sameInstalledExe = manifest && scan.chosen && samePath(path.resolve(game.dir, manifest.game.exe), scan.chosen.path);
      const carrierEnabled = isDx11Only(scan.chosen) && (carrierOverride !== null
        ? carrierOverride : !(sameInstalledExe && manifest.carrierDisabledByUser === true));
      scan.componentSelection = { dx11Carrier: carrierEnabled };
      // Pure DX11 uses the unified 0.4.5 compatibility payload. A remembered
      // historical selection must not hide an otherwise eligible DX11 game;
      // the actual payload choice is normalized per game in app-service.
      const allowDx11 = isDx11Only(scan.chosen) ||
        !state.addonVersion || state.addonVersion === DX11_COMPAT_VERSION;
      const support = assess(scan, { allowDx11 });
      return {
        id: idFor(game.dir),
        name: (override && override.name) || game.name || scan.gameName || path.basename(game.dir),
        launcher: game.launcher || '本地游戏',
        appid: game.id || null,
        dir: game.dir,
        poster: posterUrl(game.poster),
        icon: override && override.icon ? override.icon : (game.steamIcon || null),
        chosen: scan.chosen ? {
          path: scan.chosen.path,
          rel: scan.chosen.rel,
          api: scan.chosen.api,
          apiLabel: scan.chosen.apiLabel,
          apiResolution: scan.chosen.apiResolution,
          via: scan.chosen.via || null,
          bitness: scan.chosen.bitness
        } : null,
        supported: support.supported,
        supportCode: support.code,
        supportText: support.code ? MESSAGES[support.code] : '支持安装',
        installed: Boolean(manifest),
        apiOverride: ['dx11', 'dx12'].includes(apiOverride) ? apiOverride : 'auto',
        components: { dx11Carrier: carrierEnabled },
        addonVersion: manifest && manifest.payloadVersion ? manifest.payloadVersion : null,
        recommendedAddonVersion: !manifest && !state.addonVersion && isDx11Only(scan.chosen) ? DX11_COMPAT_VERSION : null,
        d3d12Route: Boolean(manifest && manifest.reshadeRoute === 'd3d12'),
        scan
      };
    });

    return rows.filter(Boolean).sort((a, b) =>
      Number(b.installed) - Number(a.installed) ||
      Number(b.supported) - Number(a.supported) ||
      a.name.localeCompare(b.name, 'zh-CN', { numeric: true })
    );
  }

  return { scanAll, idFor, prepareSelection, findGameRoot, choosePreferred };
}

module.exports = { createLibraryService, idFor, mapLimit, steamArtworkFor, normalizeUnityDynamicApi };
