'use strict';

const { app, BrowserWindow } = require('electron');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const source = path.join(root, 'src', 'renderer', 'app-icon.svg');
const outDir = path.join(root, 'build');
const sizes = [16, 24, 32, 48, 64, 128, 256];

function pngData(image, size) {
  return image.resize({ width: size, height: size, quality: 'best' }).toPNG();
}

function buildIco(images) {
  const header = Buffer.alloc(6);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(images.length, 4);
  const directory = Buffer.alloc(images.length * 16);
  let offset = 6 + directory.length;
  images.forEach((image, index) => {
    const at = index * 16;
    directory[at] = image.size === 256 ? 0 : image.size;
    directory[at + 1] = image.size === 256 ? 0 : image.size;
    directory.writeUInt16LE(1, at + 4);
    directory.writeUInt16LE(32, at + 6);
    directory.writeUInt32LE(image.png.length, at + 8);
    directory.writeUInt32LE(offset, at + 12);
    offset += image.png.length;
  });
  return Buffer.concat([header, directory, ...images.map(image => image.png)]);
}

app.whenReady().then(async () => {
  const svg = fs.readFileSync(source, 'utf8');
  const window = new BrowserWindow({
    show: false,
    width: 1024,
    height: 1024,
    useContentSize: true,
    transparent: true,
    backgroundColor: '#00000000',
    webPreferences: { offscreen: true }
  });
  try {
    const html = `<!doctype html><html><body style="margin:0;width:1024px;height:1024px;background:transparent;overflow:hidden">${svg}</body></html>`;
    await window.loadURL(`data:text/html;charset=utf-8,${encodeURIComponent(html)}`);
    const image = await window.webContents.capturePage({ x: 0, y: 0, width: 1024, height: 1024 });
    fs.mkdirSync(outDir, { recursive: true });
    const images = sizes.map(size => ({ size, png: pngData(image, size) }));
    fs.writeFileSync(path.join(outDir, 'icon.ico'), buildIco(images));
    fs.writeFileSync(path.join(outDir, 'icon.png'), pngData(image, 1024));
    console.log(`Built build/icon.ico and build/icon.png from ${path.relative(root, source)}`);
  } finally {
    window.destroy();
    app.quit();
  }
});
