'use strict';

const packageJson = require('../package.json');

// Keep the ordinary package configuration authoritative. This variant only
// omits the bundled NR payload and writes to a separate output directory.
const config = structuredClone(packageJson.build);
config.directories = { ...(config.directories || {}), output: 'dist-external' };
config.extraResources = (config.extraResources || []).filter(row => row && row.from !== 'payload');
config.portable = { ...(config.portable || {}), requestExecutionLevel: 'admin', artifactName: 'DLSS5-Manager-${version}-external-portable.exe' };
config.nsis = { ...(config.nsis || {}), artifactName: 'DLSS5-Manager-Setup-${version}-external.exe' };
config.win = { ...(config.win || {}), requestedExecutionLevel: 'requireAdministrator' };

module.exports = config;
