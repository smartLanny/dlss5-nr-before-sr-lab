'use strict';
// Thin, pinned adapter. Product policy stays under src/product; upstream local
// library discovery stays auditable and replaceable as one dependency.
module.exports = require('../vendor/DLSS5-Swapper/src/library.js');
