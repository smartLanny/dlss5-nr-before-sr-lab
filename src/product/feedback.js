'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');

const MAX_LOG_BYTES = 256 * 1024;
const MAX_OPERATIONS = 80;

function trimText(value, max = MAX_LOG_BYTES) {
  const text = String(value || '').replace(/\u0000/g, '');
  if (Buffer.byteLength(text, 'utf8') <= max) return text;
  const bytes = Buffer.from(text, 'utf8');
  return `...[前面的内容已截断，保留最后 ${Math.round(max / 1024)} KB]...\n${bytes.subarray(-max).toString('utf8')}`;
}

function redactPath(value) {
  if (typeof value !== 'string') return value;
  const home = os.homedir();
  return value
    .replace(new RegExp(home.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi'), '%USERPROFILE%')
    .replace(/([A-Za-z]:[\\/]+Users[\\/])[^\\/]+/gi, '$1%USERNAME%');
}

function safeJson(value, includePaths = false) {
  if (typeof value === 'string') return includePaths ? value : redactPath(value);
  if (Array.isArray(value)) return value.map(item => safeJson(item, includePaths));
  if (!value || typeof value !== 'object') return value;
  return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, safeJson(item, includePaths)]));
}

function createFeedbackCollector({ userData, productVersion = '0.0.0' }) {
  const feedbackDir = path.join(userData, 'feedback');
  const operationsFile = path.join(feedbackDir, 'operations.jsonl');

  async function record(entry) {
    try {
      await fs.promises.mkdir(feedbackDir, { recursive: true });
      const row = {
        time: new Date().toISOString(),
        ...safeJson(entry, false)
      };
      await fs.promises.appendFile(operationsFile, `${JSON.stringify(row)}\n`, 'utf8');
      const lines = (await fs.promises.readFile(operationsFile, 'utf8')).split(/\r?\n/).filter(Boolean);
      if (lines.length > MAX_OPERATIONS) {
        await fs.promises.writeFile(operationsFile, `${lines.slice(-MAX_OPERATIONS).join('\n')}\n`, 'utf8');
      }
    } catch {
      // A diagnostic logger must never turn a successful install into a failure.
    }
  }

  async function readOperations(gameId = null) {
    try {
      const lines = (await fs.promises.readFile(operationsFile, 'utf8')).split(/\r?\n/).filter(Boolean);
      const rows = lines.slice(-MAX_OPERATIONS).map(line => {
        try { return JSON.parse(line); } catch { return null; }
      }).filter(Boolean);
      const related = gameId ? rows.filter(row => row.gameId === gameId) : [];
      return (related.length ? related : rows).slice(-30);
    } catch {
      return [];
    }
  }

  async function readLogs(gameDir, executableDir) {
    const names = new Set([
      'ReShade.log',
      'nr-before-sr.log',
      'nr-before-sr.previous.log',
      'nr_before_sr.log',
      'dlss5.log'
    ]);
    const files = [];
    for (const dir of [executableDir, gameDir]) {
      if (!dir || !fs.existsSync(dir)) continue;
      let entries = [];
      try { entries = await fs.promises.readdir(dir, { withFileTypes: true }); } catch { continue; }
      for (const entry of entries) {
        if (!entry.isFile() || !names.has(entry.name)) continue;
        const file = path.join(dir, entry.name);
        if (files.some(row => row.file.toLowerCase() === file.toLowerCase())) continue;
        try {
          files.push({ name: entry.name, file, text: trimText(await fs.promises.readFile(file, 'utf8')) });
        } catch {}
      }
    }
    return files;
  }

  function formatOperations(rows, includePaths) {
    if (!rows.length) return '暂无管理器操作记录。';
    return rows.map(row => {
      const status = row.outcome || (row.ok ? '成功' : '失败');
      const code = row.errorCode ? ` [${row.errorCode}]` : '';
      const detail = row.errorMessage ? `：${row.errorMessage}` : '';
      return `${row.time || '-'} | ${row.action || '-'} | ${status}${code}${detail}`;
    }).join('\n');
  }

  async function buildReport({ game, diagnostic = null, hardware = null, payload = null, settings = null, gameId = null, includePaths = false }) {
    const chosen = game && game.chosen ? game.chosen : null;
    const executable = chosen && chosen.path ? chosen.path : null;
    const gameDir = game && game.dir ? game.dir : null;
    const executableDir = executable ? path.dirname(executable) : gameDir;
    const [operations, logs] = await Promise.all([
      readOperations(gameId),
      readLogs(gameDir, executableDir)
    ]);
    const payloadSummary = payload ? {
      selectedVersion: payload.selectedVersion || null,
      ready: Boolean(payload.ready),
      missing: payload.missing || [],
      invalid: payload.invalid || []
    } : null;
    const report = [
      'DLSS 5 AI 超分管理器 · 问题反馈日志',
      '请把这个 TXT 文件完整发给开发者；管理器不会自动上传。',
      `生成时间：${new Date().toISOString()}`,
      `管理器版本：${productVersion}`,
      `系统：${process.platform} ${process.arch} ${os.release()}`,
      '',
      '[游戏]',
      `名称：${game && game.name ? game.name : '未知'}`,
      `来源：${game && game.launcher ? game.launcher : '未知'}`,
      `游戏目录：${includePaths ? gameDir || '未知' : redactPath(gameDir || '未知')}`,
      `运行程序：${includePaths ? executable || '未知' : redactPath(executable || '未知')}`,
      `图形 API：${chosen && chosen.apiLabel ? chosen.apiLabel : '未知'}`,
      `位数：${chosen && chosen.bitness ? chosen.bitness : '未知'}`,
      '',
      '[硬件与组件]',
      `显卡匹配：${hardware ? JSON.stringify(safeJson(hardware, includePaths)) : '未知'}`,
      `组件版本：${payloadSummary ? JSON.stringify(payloadSummary) : '未知'}`,
      `游戏设置：${settings ? JSON.stringify(safeJson(settings, includePaths)) : '未读取'}`,
      '',
      '[当前诊断]',
      diagnostic ? `完整：${diagnostic.complete ? '是' : '否'}\n${(diagnostic.components || []).map(row => `${row.label}：${row.detail || (row.ok ? '完整' : '异常')}`).join('\n')}` : '诊断未生成。',
      '',
      '[最近管理器操作]',
      formatOperations(operations, includePaths),
      '',
      '[相关运行日志]'
    ];
    if (!logs.length) report.push('未找到 ReShade.log 或 nr-before-sr.log。');
    for (const log of logs) report.push(`--- ${log.name} ---\n${includePaths ? log.text : redactPath(log.text)}`);
    return {
      text: `${report.join('\n')}\n`,
      suggestedName: `DLSS5-反馈-${String(game && game.name || '未知游戏').replace(/[<>:"/\\|?*\x00-\x1F]/g, '_').slice(0, 40)}-${new Date().toISOString().replace(/[.:]/g, '-')}.txt`
    };
  }

  return { record, buildReport, readOperations, operationsFile };
}

module.exports = { createFeedbackCollector, redactPath, trimText };
