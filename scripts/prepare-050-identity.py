# Executed by the isolated preparation wrapper before its validation/publish step.
# read/save/changes are supplied by prepare-050-source.py. Not in product tree.
validator = changes['scripts/validate-beta04-structure.py']
validator = validator.replace('0,4,3,3', '0,5,0,1').replace('0.4.3r3 beta', '0.5.0 dev1 beta')
validator = validator.replace('beta0.5-dev1 community maintenance release', 'beta0.5-dev1 experimental candidate, not an OTA release')
save('scripts/validate-beta04-structure.py', validator)
save('docs/UPDATE.zh-CN.txt', '''DLSS 5 NR Before SR beta0.5-dev1 实验候选\n\n这不是正式 Release/OTA。基于完整 beta0.4.3r3，0.4 继续维护兼容性。\nMode2 新增默认开启的“高强度保护”：只限制大于1倍的额外增强；关闭可恢复旧算法。\nMode1、0-1倍、原图旁路及 HDR 原始高光合同不变。\n不能修复1倍已剪裁的模型画面，不代表8倍画质稳定或游戏问题已实测解决。\n安装前备份原 addon/INI；不要同时加载新旧两个NR插件。\n详细开发边界、回退、0.4.4合入规则与测试见 docs/BETA_0_5_DEVELOPMENT.md。\n''')
save('docs/UPDATE.en.txt', '''DLSS 5 NR Before SR beta0.5-dev1 experimental candidate\n\nNot a Release or OTA. Based on the complete beta0.4.3r3 maintenance head.\nMode2 adds opt-out HighStrengthProtection for extra amplification above 1x.\nMode1, 0-1x, native identity and the source HDR-headroom contract are unchanged.\nThis does not recover clipping already present at 1x or establish 8x/game stability.\nBack up the existing addon/INI; never load both old and new NR addons together.\nSee docs/BETA_0_5_DEVELOPMENT.md for scope, rollback and future 0.4.4 merges.\n''')
readme = read('README.md')
save('README.md', '''# beta0.5-dev1 experimental development branch\n\n当前分支是基于完整 **beta0.4.3r3** 的 **0.5 开发候选**，不是新的正式 Release。\n首批加入可关闭的高强度保护；低分辨率 EditField / Lanczos2 / Catmull 尚在后续分阶段任务中。\n开发、验证、回退和持续合入 0.4.4 的规则：[BETA_0_5_DEVELOPMENT.md](docs/BETA_0_5_DEVELOPMENT.md)。\n当前验证状态请查看该提交的 Actions，不将 WARP 通过等同于游戏实测。\n\n---\n\n## 继承的 0.4 稳定线说明（历史基线，版本链接不代表本候选）\n\n''' + readme)
for path in ['docs/INSTALL.zh-CN.txt', 'docs/INSTALL.en.txt', 'docs/INSTALL.zh-CN.md', 'docs/INSTALL.en.md']:
    content = read(path)
    banner = ('beta0.5-dev1 实验候选，不是正式 Release。先备份旧版，运行时/画质验收以PR为准。详见 docs/BETA_0_5_DEVELOPMENT.md。\n\n'
              if '.zh-CN.' in path else
              'beta0.5-dev1 experimental candidate, not a Release. Back up the old version. See docs/BETA_0_5_DEVELOPMENT.md and the PR for validation limits.\n\n')
    # These are current installation instructions, not historical release notes.
    content = content.replace('beta0.4.3r3', 'beta0.5-dev1').replace('beta0.4.3', 'beta0.5-dev1')
    save(path, banner + content)
validator = changes['scripts/validate-beta04-structure.py']
for label in ['Chinese install', 'English install', 'Chinese install markdown', 'English install markdown']:
    validator = validator.replace('"' + label + '": ["beta0.4.3"]', '"' + label + '": ["beta0.5-dev1"]')
save('scripts/validate-beta04-structure.py', validator)
