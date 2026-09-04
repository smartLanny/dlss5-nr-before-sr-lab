'use strict';

const branding = require('../../product.json');

const PRODUCT = Object.freeze({
  id: 'xiaofeng-dlss5-manager',
  ...branding,
  upstreamCommit: 'ccb67f4bc92cb5da1d25416dd6b338e12fa432c5'
});

const PAYLOAD_FILES = Object.freeze({
  reshade: 'ReShade64.dll',
  addon: 'nr-before-sr.zh-CN.addon64',
  bridge: 'nrchain_nvngx.dll',
  runtime: 'nvngx_dlssnr.dll',
  config: 'nr_before_sr.ini'
});

const INSTALLED_NAMES = Object.freeze({
  reshade: 'dxgi.dll',
  addon: PAYLOAD_FILES.addon,
  bridge: PAYLOAD_FILES.bridge,
  runtime: PAYLOAD_FILES.runtime,
  config: PAYLOAD_FILES.config
});

const DEFAULT_NR_CONFIG = Object.freeze({
  Enabled: 1,
  Mode: 2,
  Intensity: 1.2,
  Style: 1,
  AutoMask: 1,
  SkinStructureStrength: -1,
  LocalToneStrength: 1,
  LocalStructureStrength: 1,
  TransferStrength: 1,
  ColorStrength: 0,
  UICorrection: 0
});

const PUBLIC_NR_KEYS = Object.freeze([
  'Enabled', 'Mode', 'Intensity', 'Style', 'AutoMask'
]);

module.exports = {
  PRODUCT,
  PAYLOAD_FILES,
  INSTALLED_NAMES,
  DEFAULT_NR_CONFIG,
  PUBLIC_NR_KEYS
};
