'use strict';

const fs = require('fs');
const path = require('path');

// Read only launcher-owned local configuration. Keep no account identifiers
// or unrelated launch strings in the public game/feedback record.
function parseVdf(text) {
  const tokens = text.match(/"(?:\\.|[^"\\])*"|[{}]/g) || [];
  const root = Object.create(null);
  const stack = [root];
  let key = null;
  for (const token of tokens) {
    if (token === '{') {
      if (key === null || stack.length > 32) return null;
      const next = Object.create(null);
      stack[stack.length - 1][key] = next;
      stack.push(next); key = null;
    } else if (token === '}') {
      if (stack.length < 2 || key !== null) return null;
      stack.pop();
    } else {
      const value = token.slice(1, -1).replace(/\\(["\\])/g, '$1');
      if (key === null) key = value.toLowerCase();
      else { stack[stack.length - 1][key] = value; key = null; }
    }
  }
  return stack.length === 1 && key === null ? root : null;
}

function createLaunchContext() {
  const cache = new Map();
  return game => {
    if (!game || game.launcher !== 'Steam' || !game.steamRoot || !/^\d+$/.test(String(game.id))) return {};
    const root = path.resolve(game.steamRoot);
    if (!cache.has(root)) {
      const apps = [];
      const userdata = path.join(root, 'userdata');
      let users = [];
      try { users = fs.readdirSync(userdata, { withFileTypes: true }).filter(row => row.isDirectory() && /^\d+$/.test(row.name)).slice(0, 8); } catch {}
      for (const user of users) {
        const file = path.join(userdata, user.name, 'config', 'localconfig.vdf');
        try {
          const stat = fs.lstatSync(file);
          if (!stat.isFile() || stat.size > 2 * 1024 * 1024) continue;
          const data = parseVdf(fs.readFileSync(file, 'utf8'));
          const values = data?.userlocalconfigstore?.software?.valve?.steam?.apps;
          if (values && typeof values === 'object') apps.push(values);
        } catch {}
      }
      cache.set(root, apps);
    }
    const args = cache.get(root).map(apps => apps[String(game.id)]?.launchoptions).filter(value => typeof value === 'string').map(value => value.slice(0, 4096));
    return { launchArguments: [...new Set(args)], launchMode: 'direct-executable' };
  };
}

module.exports = { createLaunchContext };
