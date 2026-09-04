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
  config: 'nr_before_sr.ini',
  quickbar: 'zjz-quickbar.addon64',
  quickbarConfig: 'nr_quickbar.ini'
});

const INSTALLED_NAMES = Object.freeze({
  reshade: 'dxgi.dll',
  addon: PAYLOAD_FILES.addon,
  bridge: PAYLOAD_FILES.bridge,
  runtime: PAYLOAD_FILES.runtime,
  config: PAYLOAD_FILES.config,
  quickbar: PAYLOAD_FILES.quickbar,
  quickbarConfig: PAYLOAD_FILES.quickbarConfig
});

module.exports = { PRODUCT, PAYLOAD_FILES, INSTALLED_NAMES };
