'use strict';
const path = require('path');
const { readMfgUnlockResources } = require('../src/product/fg-mfgunlock-resources');
const manifest = readMfgUnlockResources(path.resolve(__dirname, '../resources/fg-mfgunlock'));
console.log(`MFG Unlock resources verified: ${manifest.id}; binary was not executed.`);
