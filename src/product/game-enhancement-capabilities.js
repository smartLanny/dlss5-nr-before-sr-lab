'use strict';
const fs = require('node:fs');
const path = require('node:path');

// Static DLL evidence is not a runtime capability or a generated-frame result.
// Manager-owned external NR assets can never upgrade a game's SR/FG eligibility.
function x64Header(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const stat = fs.fstatSync(fd), dos = Buffer.alloc(64);
    if (fs.readSync(fd, dos, 0, 64, 0) !== 64 || dos.readUInt16LE(0) !== 0x5a4d) return false;
    const offset = dos.readUInt32LE(0x3c), header = Buffer.alloc(26);
    return offset >= 64 && offset <= 1024 * 1024 && offset + 26 <= stat.size &&
      fs.readSync(fd, header, 0, 26, offset) === 26 && header.readUInt32LE(0) === 0x4550 &&
      header.readUInt16LE(4) === 0x8664 && header.readUInt16LE(24) === 0x20b;
  } finally { fs.closeSync(fd); }
}
function inspectNativeEnhancementCapabilities(scan, options = {}) {
  function contains(rows, expected) {
    return Array.isArray(rows) && rows.some(row => {
      const file = row?.path || row?.file;
      if (typeof file !== 'string' || !path.isAbsolute(file) || path.basename(file).toLowerCase() !== expected ||
          /(?:^|[\\/])(?:_DLSS5_Feeder|_DLSS5_Backup|feeder-runtime|vulkan-runtime)(?:[\\/]|$)/i.test(file)) return false;
      try {
        const stat = fs.lstatSync(file);
        return stat.isFile() && !stat.isSymbolicLink() && stat.nlink === 1 &&
          (options.pe ? options.pe.getBitness(file) === 64 : x64Header(file));
      } catch { return false; }
    });
  }
  const nativeDlssAvailable = contains(scan?.dlssFiles, 'nvngx_dlss.dll') || contains([scan?.primaryDlss], 'nvngx_dlss.dll');
  const nativeFgAvailable = contains(scan?.dlssFiles, 'nvngx_dlssg.dll') && contains(scan?.streamlineFiles, 'sl.dlss_g.dll') &&
    (contains(scan?.streamlineFiles, 'sl.interposer.dll') || contains(scan?.streamlineFiles, 'sl.common.dll'));
  return { nativeDlssAvailable, nativeFgAvailable, staticOnly: true };
}
module.exports = { inspectNativeEnhancementCapabilities };
