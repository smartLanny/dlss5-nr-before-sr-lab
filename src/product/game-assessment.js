'use strict';

const { inspectNativeEnhancementCapabilities } = require('./game-enhancement-capabilities');
const { emptyVerification } = require('./runtime-verification');
const { classifyApi } = require('./game-support');

function createGameAssessment({ service, settings, coordinator, environment, operations, launches, verification, launchMode, helper, records, components, hardware = () => null }) {
  async function assess(id) {
    const games = await service.listGames(), game = games.find(row => row.id === id);
    if (!game) throw Object.assign(new Error('游戏不在当前库中。'), { code: 'ASSESSMENT_GAME_MISSING' });
    const failures = [];
    const optional = async (name, fn, fallback) => {
      try { return await fn(); } catch (error) { failures.push({ section: name, code: error.code || 'UNAVAILABLE', message: error.message }); return fallback; }
    };
    const [deployment, enhancements, maintenance, operation, session, launch, nr, hotkeys] = await Promise.all([
      optional('deployment', () => service.inspectDeployment(id), { mode: 'unknown', verified: false }),
      optional('enhancements', () => coordinator.inspect(id), { unavailable: true, featureStates: {} }),
      optional('maintenance', () => environment.inspect(id), { unavailable: true }),
      optional('operation', () => operations.inspect(id), { pending: true, unavailable: true }),
      optional('session', () => launches.inspect(id), null), optional('launch', () => launchMode(id), {}),
      optional('nr', () => service.readNrSettings(id), null), optional('hotkeys', () => service.readGameHotkeys(id), null)
    ]);
    const scan = service.gameScan(id), api = scan.chosen?.apiAssessment || game.chosen?.apiAssessment || {
      capabilities: [], effectiveApi: classifyApi(scan.chosen), confidence: 'none', conflicts: [], evidence: [], observedApi: null,
      bridgeStatus: { status: 'unknown', verified: false }
    };
    const runtime = await optional('verification', () => verification.assess(id, session), emptyVerification(session?.helper, session));
    const visual = records ? await optional('visualRecord', () => records.inspect(id, session), null) : null;
    if (visual) runtime.visual = visual;
    const componentState = components ? await optional('components', () => components.inspect(id), { files: [], conflicts: [], warnings: [], unavailable: true }) : null;
    return { schema: 1, gameId: id, assessedAt: new Date().toISOString(), game, api, deployment,
      layout: await optional('layout', () => service.getLayout(id), null), hardware: hardware(),
      nativeIntegration: inspectNativeEnhancementCapabilities(scan), enhancements, nr, hotkeys,
      components: componentState,
      conflicts: { api: api.conflicts || [], files: componentState?.conflicts || [], source: 'current-inspection' },
      maintenance, operation, launch: { ...launch, session }, verification: runtime,
      coreVersions: await optional('coreVersions', () => service.listAddonVersions(), []), failures,
      helperModules: helper ? await optional('helperModules', () => helper.inspect(id), { ready: false, modules: [] }) : { ready: false, modules: [] },
      antiCheat: { detected: await optional('antiCheat', () => require('../core/install-guards').antiCheatPresent(service.gameDirectory(id)), false),
        message: '反作弊或游戏保护可能阻止加载，当前路线也可能暂时无法启用；使用模组有账号处罚风险，请自行决定。',
        officialUrl: 'https://help.steampowered.com/zh-cn/faqs/view/571A-97DA-70E9-FF74',
        searchQuery: `${game.name || '游戏'} ReShade 反作弊 兼容性` }, runtimeVerified: false };
  }
  return { assess };
}
module.exports = { createGameAssessment };
