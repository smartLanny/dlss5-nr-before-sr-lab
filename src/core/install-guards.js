'use strict';

const path = require('path');
const { execFile } = require('child_process');
const upstream = require('../../vendor/DLSS5-Swapper/src/core/install-guards.js');

function run(file, args) {
  return new Promise((resolve, reject) => execFile(file, args, {
    windowsHide: true,
    timeout: 20000,
    maxBuffer: 4 * 1024 * 1024
  }, (error, stdout) => error ? reject(error) : resolve(stdout)));
}

function normalizedExecutable(value) {
  if (typeof value !== 'string' || !value.trim()) return null;
  try { return path.resolve(value).toLowerCase(); } catch { return null; }
}

function numericPid(value) {
  if (value === null || value === undefined || value === '') return null;
  const pid = Number(value);
  return Number.isSafeInteger(pid) && pid > 0 ? pid : null;
}

function processRows(value) {
  const rows = Array.isArray(value) ? value : [value];
  if (!rows.every(row => row && typeof row === 'object')) throw new TypeError('Invalid process snapshot');
  return rows;
}

async function systemProcesses(runner = run) {
  const powershell = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32/WindowsPowerShell/v1.0/powershell.exe');
  const output = await runner(powershell, [
    '-NoProfile',
    '-NonInteractive',
    '-Command',
    "$ErrorActionPreference='Stop'; @(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,ExecutablePath) | ConvertTo-Json -Compress"
  ]);
  const parsed = JSON.parse(output || '[]');
  return processRows(parsed);
}

function createInstallGuards(options = {}) {
  const processId = numericPid(options.processId === undefined ? process.pid : options.processId);
  const executablePath = normalizedExecutable(options.executablePath === undefined ? process.execPath : options.executablePath);
  const portableExecutablePath = normalizedExecutable(options.portableExecutablePath === undefined
    ? process.env.PORTABLE_EXECUTABLE_FILE
    : options.portableExecutablePath);
  const queryProcesses = options.queryProcesses || (() => systemProcesses());

  function ownedProcessIds(rows, gameExePath) {
    const gamePath = normalizedExecutable(gameExePath);
    const owned = new Set();
    if (processId !== null && executablePath !== gamePath) owned.add(processId);

    // Electron renderer/GPU/utility processes use the same executable as the
    // main process. Require both exact path identity and a descendant relation;
    // a game launched by the Manager remains guarded because its path differs.
    let changed = true;
    while (changed && executablePath && executablePath !== gamePath) {
      changed = false;
      for (const row of rows) {
        const pid = numericPid(row && row.ProcessId);
        const parentPid = numericPid(row && row.ParentProcessId);
        if (pid !== null && parentPid !== null && !owned.has(pid) && owned.has(parentPid) &&
            normalizedExecutable(row.ExecutablePath) === executablePath) {
          owned.add(pid);
          changed = true;
        }
      }
    }

    // electron-builder's portable launcher stays alive in ExecWait while the
    // extracted app runs. Trust its environment path only when the process
    // snapshot also proves that exact executable is our direct parent. If UAC
    // or a stale snapshot breaks that relation, retain the conservative block.
    if (processId !== null && portableExecutablePath && portableExecutablePath !== gamePath) {
      const current = rows.find(row => numericPid(row && row.ProcessId) === processId);
      const parentPid = numericPid(current && current.ParentProcessId);
      const parent = rows.find(row => numericPid(row && row.ProcessId) === parentPid);
      if (parentPid !== null && parent && normalizedExecutable(parent.ExecutablePath) === portableExecutablePath) {
        owned.add(parentPid);
      }
    }
    return owned;
  }

  function matchingProcesses(processes, gameDir, gameExePath) {
    const rows = processRows(processes);
    const owned = ownedProcessIds(rows, gameExePath);
    const candidates = rows.filter(row => !owned.has(numericPid(row && row.ProcessId)));
    const matches = upstream.matchingProcesses(candidates, gameDir, gameExePath);

    // Upstream always excludes process.pid. If the selected game executable is
    // the Manager itself, restore that row so an identity collision fails shut.
    const gamePath = normalizedExecutable(gameExePath);
    if (gamePath && gamePath === executablePath) {
      const current = rows.find(row => numericPid(row && row.ProcessId) === processId &&
        normalizedExecutable(row && row.ExecutablePath) === gamePath);
      if (current && !matches.includes(current)) matches.push(current);
    }
    return matches;
  }

  async function assertGameClosed(gameDir, gameExePath) {
    let rows;
    try {
      rows = processRows(await queryProcesses());
    } catch (cause) {
      throw Object.assign(new Error('errProcessCheck'), { code: 'errProcessCheck', cause });
    }
    const matches = matchingProcesses(rows, gameDir, gameExePath);
    if (matches.length) {
      const processes = matches.map(row => ({
        pid: numericPid(row && row.ProcessId),
        name: typeof (row && row.Name) === 'string' && row.Name ? row.Name : 'unknown'
      }));
      throw Object.assign(new Error(`Close the game and helper first: ${processes.map(row => row.name).join(', ')}`), {
        code: 'errGameRunning',
        details: { processes }
      });
    }
  }

  return { ...upstream, matchingProcesses, assertGameClosed };
}

module.exports = { ...createInstallGuards(), createInstallGuards };
