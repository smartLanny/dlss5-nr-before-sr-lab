'use strict';

const branding = require('../../product.json');

const PRODUCT = Object.freeze({
  id: 'xiaofeng-dlss5-manager',
  ...branding,
  upstreamCommit: 'ccb67f4bc92cb5da1d25416dd6b338e12fa432c5'
});

const DX11_COMPAT_VERSION = '0.4.2-dx11-native-bridge-exp1-r1';

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
  config: PAYLOAD_FILES.config,
  carrier: 'dlss5-native-carrier-exp1.addon64'
});

const DEFAULT_NR_CONFIG = Object.freeze({
  Enabled: 1,
  Mode: 2,
  Intensity: 1.0,
  WorkMode: 0,
  CustomWorkScale: 1.0,
  Style: 0,
  AutoMask: 0,
  ColorStrength: 0.75,
  SkinStructureStrength: -1,
  LocalToneStrength: 1,
  LocalStructureStrength: 1,
  TransferStrength: 1,
  UICorrection: 0
});

const PUBLIC_NR_KEYS = Object.freeze([
  'Enabled', 'Intensity', 'WorkMode', 'CustomWorkScale', 'Style', 'AutoMask', 'ColorStrength', 'SkinStructureStrength'
]);

module.exports = {
  PRODUCT,
  DX11_COMPAT_VERSION,
  PAYLOAD_FILES,
  INSTALLED_NAMES,
  DEFAULT_NR_CONFIG,
  PUBLIC_NR_KEYS
};
