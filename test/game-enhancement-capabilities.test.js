'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { assessEnhancementState } = require('../src/product/game-enhancement-capabilities');
const { enhancementEvidence } = require('./helpers/enhancement-evidence');
const exe = 'C:/Games/Example/Game.exe', identity = 'a'.repeat(64);
function input(overrides = {}) {
  const evidence = enhancementEvidence();
  return { domain: 'fg', request: { backend: 'nvidia', mode: 'fixed', multiplier: 4 },
    game: { ...evidence, exe, exeIdentity: identity }, hardware: { series: ['RTX50'] }, driver: evidence.driver, ...overrides };
}

test('enumerated driver settings cannot supply missing per-game integration evidence', () => {
  const value = input(); value.game.support = { status: 'unknown' };
  const result = assessEnhancementState(value);
  assert.equal(result.eligible, false); assert.ok(result.blockers.some(row => row.code === 'SETTINGS_GAME_SUPPORT_UNKNOWN'));
  assert.equal(result.evidence.driver.perGameSupport, false);
});
test('known native integration does not grant NVIDIA MFG multipliers or Dynamic', () => {
  const value = input(); value.game.support = { status: 'supported', source: 'native-integration', staticOnly: true, evidence: ['native-files'] };
  const result = assessEnhancementState(value);
  assert.equal(result.eligible, false); assert.deepEqual(result.availableMultipliers, [2]);
  assert.equal(result.availableModes.includes('dynamic'), false); assert.equal(result.officialOverrideCertified, false);
});
test('MFG Unlock never offers Dynamic and refuses unverified 5/6x capacity', () => {
  const value = input({ hardware: { series: ['RTX40'] }, request: { backend: 'mfgunlock', mode: 'dynamic', targetFps: 120 } });
  value.game.support = { status: 'supported', source: 'native-integration', staticOnly: true, evidence: ['native-files'],
    capabilities: { mfgUnlock: { available: true, api: 'dx12', multipliers: [2, 3, 4] } } };
  const result = assessEnhancementState(value);
  assert.equal(result.eligible, false); assert.deepEqual(result.availableModes, ['follow', 'fixed']);
  assert.deepEqual(result.availableMultipliers, [2, 3, 4]);
  value.request = { backend: 'mfgunlock', mode: 'fixed', multiplier: 6 };
  assert.ok(assessEnhancementState(value).blockers.some(row => row.code === 'SETTINGS_MULTIPLIER_UNCONFIRMED'));
});
test('MFG support without verified backend capacity cannot unlock default multipliers', () => {
  const value = input({ hardware: { series: ['RTX40'] }, request: { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 } });
  value.game.support = { status: 'supported', source: 'native-integration', capabilities: { multipliers: [2] } };
  const result = assessEnhancementState(value);
  assert.equal(result.eligible, false); assert.deepEqual(result.availableModes, []); assert.deepEqual(result.availableMultipliers, []);
  assert.ok(result.blockers.some(row => row.code === 'SETTINGS_MFG_RUNTIME_UNCONFIRMED'));
});
test('unreadable activation is advisory and user statements cannot create support', () => {
  const value = input(); value.game.gameSetting = { state: 'unknown' };
  assert.equal(assessEnhancementState(value).state, 'configurable');
  assert.equal(assessEnhancementState(value).canConfirm, false);
  value.confirmation = { enabled: true, exe, exeIdentity: identity, domain: 'fg' };
  const confirmed = assessEnhancementState(value); assert.equal(confirmed.eligible, true); assert.equal(confirmed.runtimeVerified, false);
  assert.equal(confirmed.state, 'configurable'); assert.equal(confirmed.canConfirm, false);
  assert.deepEqual(confirmed.actual, { state: 'unknown', source: null });
  assert.equal(confirmed.evidence.confirmation, null); assert.equal(confirmed.activation.state, 'unknown');
  value.game.support = { status: 'unknown' };
  assert.equal(assessEnhancementState(value).eligible, false);
});
test('known-off is activation advice while missing required support evidence remains blocked', () => {
  const value = input({ confirmation: { enabled: true, exe, exeIdentity: identity, domain: 'fg' } });
  value.game.gameSetting = { state: 'off', source: 'game-config' };
  assert.equal(assessEnhancementState(value).state, 'configurable');
  assert.equal(assessEnhancementState(value).activation.state, 'off');
  assert.equal(assessEnhancementState(value).canConfirm, false);
  value.game.gameSetting = { state: 'missing', requiresFirstRun: true, source: 'game-config' };
  assert.equal(assessEnhancementState(value).state, 'configurable');
  value.game.gameSetting.requiredForSupport = true;
  assert.equal(assessEnhancementState(value).state, 'waiting-first-run');
  assert.equal(assessEnhancementState(value).canConfirm, false);
  value.game.gameSetting = { state: 'on', source: 'game-config' };
  assert.equal(assessEnhancementState(value).state, 'configurable');
  assert.equal(assessEnhancementState(value).canConfirm, false);
});
test('GPU and driver read failures remain missing evidence without masquerading as game rejection', () => {
  const value = input({ driver: { available: false }, hardware: { series: ['RTX40', 'RTX50'], family: 'mixed' } });
  const result = assessEnhancementState(value);
  assert.equal(result.eligible, false); assert.equal(result.canConfirm, false);
  assert.equal(result.blockers.some(row => row.code === 'SETTINGS_GAME_UNSUPPORTED'), false);
});
