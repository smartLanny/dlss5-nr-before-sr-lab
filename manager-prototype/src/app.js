'use strict';

const state = {
  bootstrap: null,
  view: 'games',
  settings: {
    mode: 'performance',
    intensity: 1.2,
    style: 'natural',
    faceProtection: true
  },
  diagnosis: null
};

const $ = id => document.getElementById(id);
const qa = selector => [...document.querySelectorAll(selector)];

const titles = {
  games: ['我的游戏', '自动识别游戏，推荐最适合你的 DLSS 5 AI 超分设置'],
  install: ['一键安装', '只选择你更在意画质还是帧数，其余交给我们'],
  repair: ['问题修复', '一键体检常见组件、配置和 Mod 冲突'],
  settings: ['设置', '默认保持简单，需要时再打开高级模式']
};

function showView(name) {
  state.view = name;
  qa('.view').forEach(v => v.classList.toggle('active', v.id === `view-${name}`));
  qa('.nav-item').forEach(b => b.classList.toggle('active', b.dataset.view === name));
  $('pageTitle').textContent = titles[name][0];
  $('pageSubtitle').textContent = titles[name][1];
}

function styleLabel(value) {
  return { natural: '自然', default: '默认', cinematic: '电影感' }[value] || value;
}

function updateSummary() {
  $('summaryMode').textContent = state.settings.mode === 'performance' ? '性能优先' : '画质优先';
  $('summaryIntensity').textContent = state.settings.intensity.toFixed(2);
  $('summaryStyle').textContent = styleLabel(state.settings.style);
  $('summaryFace').textContent = state.settings.faceProtection ? '开启' : '关闭';
  $('intensityValue').textContent = state.settings.intensity.toFixed(2);
  $('intensitySlider').value = String(state.settings.intensity);
  $('styleSelect').value = state.settings.style;
  $('faceProtection').checked = state.settings.faceProtection;

  qa('.mode-card').forEach(b => b.classList.toggle('selected', b.dataset.mode === state.settings.mode));
  qa('#intensityPresets button').forEach(b => {
    b.classList.toggle('selected', Math.abs(Number(b.dataset.value) - state.settings.intensity) < 0.001);
  });
}

function applyRecommended() {
  const rec = state.bootstrap.game.recommendation;
  state.settings = { ...rec };
  updateSummary();
  $('installResult').textContent = '已套用推荐设置。原型阶段尚未写入游戏文件。';
}

function renderDiagnosis(result) {
  state.diagnosis = result;
  $('diagnosisList').innerHTML = result.checks.map(item => {
    const icon = item.state === 'ok' ? '✅' : item.state === 'warn' ? '⚠️' : '❌';
    return `<div class="diagnosis-item"><div class="diagnosis-icon">${icon}</div><strong>${item.label}</strong><span>${item.detail}</span></div>`;
  }).join('');
  $('repairAll').disabled = !result.checks.some(c => c.state !== 'ok');
  $('copyDiagnosis').disabled = false;
}

async function boot() {
  state.bootstrap = await window.managerApi.bootstrap();
  const game = state.bootstrap.game;

  $('versionLabel').textContent = `原型 ${state.bootstrap.version}`;
  $('gameName').textContent = game.name;
  $('installGameName').textContent = game.name;
  $('gameMeta').textContent = `${game.api} · ${game.gpu}`;
  $('inputResolution').textContent = game.dlssInput;
  $('outputResolution').textContent = game.output;
  $('summaryInput').textContent = game.dlssInput;
  $('summaryOutput').textContent = game.output;
  $('overlayProfile').textContent = state.bootstrap.overlayProfile;

  qa('[data-jump]').forEach(button => button.addEventListener('click', () => showView(button.dataset.jump)));
  qa('.nav-item').forEach(button => button.addEventListener('click', () => showView(button.dataset.view)));

  qa('.mode-card').forEach(button => button.addEventListener('click', () => {
    state.settings.mode = button.dataset.mode;
    updateSummary();
  }));

  qa('#intensityPresets button').forEach(button => button.addEventListener('click', () => {
    state.settings.intensity = Number(button.dataset.value);
    updateSummary();
  }));

  $('intensitySlider').addEventListener('input', event => {
    state.settings.intensity = Number(event.target.value);
    updateSummary();
  });

  $('styleSelect').addEventListener('change', event => {
    state.settings.style = event.target.value;
    updateSummary();
  });

  $('faceProtection').addEventListener('change', event => {
    state.settings.faceProtection = event.target.checked;
    updateSummary();
  });

  $('applyRecommended').addEventListener('click', applyRecommended);
  $('quickRecommended').addEventListener('click', () => { showView('install'); applyRecommended(); });

  $('prototypeInstall').addEventListener('click', () => {
    $('installResult').textContent = '原型只验证交互：真实安装/备份/恢复事务尚未接入。';
  });

  $('runDiagnosis').addEventListener('click', async () => renderDiagnosis(await window.managerApi.diagnose()));
  $('repairAll').addEventListener('click', () => {
    $('installResult').textContent = '';
    alert('原型阶段不会改写游戏文件。下一阶段接入受事务保护的一键修复。');
  });

  $('copyDiagnosis').addEventListener('click', async () => {
    const text = await window.managerApi.copyDiagnostics({ game, settings: state.settings, diagnosis: state.diagnosis });
    $('copyDiagnosis').textContent = '✓ 已复制';
    setTimeout(() => { $('copyDiagnosis').textContent = '复制诊断信息'; }, 1500);
    console.info(text);
  });

  $('advancedToggle').addEventListener('change', event => $('advancedPanel').classList.toggle('hidden', !event.target.checked));

  $('communityBtn').addEventListener('click', () => alert('QQ群入口占位：正式版写入你的群号 / 加群链接。'));
  $('announcementBtn').addEventListener('click', () => alert('更新公告入口占位：正式版接入你自己的公告源。'));

  updateSummary();
}

boot().catch(error => {
  console.error(error);
  document.body.innerHTML = `<pre style="padding:24px;color:#ff9b9b">Manager prototype failed to start:\n${String(error)}</pre>`;
});
