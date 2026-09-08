'use strict';

function createLaunchCoordinator({ service, settings, legacySrModel, guards }) {
  let queue = Promise.resolve();
  function serialize(work) {
    const current = queue.then(work, work);
    queue = current.catch(() => {});
    return current;
  }
  const closed = id => guards.assertGameClosed(service.gameDirectory(id), service.gameExecutable(id));
  async function launch(id) {
    await settings.assertReady(id);
    await service.validateLaunch(id);
    await closed(id);
    const launchSettings = await settings.beforeLaunch(id);
    const failure = launchSettings.find(row => row.applied !== true && row.skipped !== true);
    if (failure) throw Object.assign(new Error(failure.reason || '启动设置应用失败，未启动游戏。'), { code: failure.code || 'SETTINGS_APPLY_FAILED' });
    let srModel = null;
    if (!await settings.hasSrRequest(id)) {
      srModel = await legacySrModel.applyBeforeLaunch(id);
      if (srModel?.apply?.ok !== true) throw Object.assign(new Error(srModel?.apply?.error || 'SR 模型应用失败，未启动游戏。'), { code: 'SETTINGS_APPLY_FAILED' });
    }
    return { launched: await service.launch(id), srModel, launchSettings };
  }
  async function restoreForUninstall(id) {
    await settings.assertReady(id);
    await closed(id);
    for (const domain of ['fg', 'sr']) await settings.restore(id, domain);
    await legacySrModel.prepareMigration(id);
  }
  return { serialize, launch, restoreForUninstall,
    assertMutationReady: id => settings.assertReady(id),
    async writeLegacySr(id, selection) {
      if (await settings.hasSrRequest(id)) throw Object.assign(new Error('此游戏已使用新的 SR 设置，请在 SR 超分区域修改。'), { code: 'SETTINGS_OWNERSHIP' });
      return legacySrModel.write(id, selection);
    }
  };
}

module.exports = { createLaunchCoordinator };
