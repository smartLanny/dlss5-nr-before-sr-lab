# ReShade 预加载 carrier 排除工具

`scripts/configure-carrier-exclusion.py` 为调用方已经明确识别的图形 API
准备 ReShade `DisabledAddons` 配置。它不根据已加载的 `d3d11.dll`、辅助
device、可执行文件名或游戏名猜测 API，也不实现动态 Present 激活。

## 生效边界

固定 ReShade 源码在普通 add-on 扫描阶段读取 `[ADDON] DisabledAddons`。
只有包含 `@实际文件名` 的条目会在 `LoadLibraryExW` 之前按文件名跳过，
所以工具扫描 `AddonPath` 的第一项（相对 `ReShade.ini` 所在目录），只为
该普通目录中的以下现存普通文件生成大小写精确的 `@` 条目：

- `dlss5-native-carrier-*.addon64`
- `dlss5-bridge.addon64`

核心 addon、`nrchain_nvngx.dll`、其它 add-on 和 DLL 均不修改或删除。
已有禁用项逐字节保留；已有精确 `name@file` 条目不会重复，只有名称而无
`@file` 的项仍会补一个 `@实际文件名`，使下一次普通目录扫描能在映射前
排除。重复 section/key 会像 ReShade 一样累加；值中的 `,,` 按一个字面
逗号解析，新增文件名中的逗号也会写成 `,,`。

`LoadFromDllMain` 在 ReShade 的 DllMain 中直接调用 `LoadLibraryExW`，绕过
上述 `DisabledAddons` 过滤。若它显式包含 carrier 或指向疑似 carrier，
工具返回 `unsupported-direct-load` 并拒绝 apply；它不会修改这些显式加载
项，也不会声称排除已经生效。此工具只覆盖 ReShade 的普通目录扫描，
不承诺阻止其它插件、脚本或第三方组件自行调用加载 API。

只有调用方显式传入 `d3d12` 或 `vulkan` 时才计划排除。`d3d11` 与
`unknown` 永远不写，也不会自动重新启用用户已经禁用的条目。配置在下一次
ReShade 普通 add-on 扫描时生效，不是对当前进程的动态卸载。

修改前必须完全退出游戏和所有使用该 INI 的 ReShade 进程。OTA 更换或新增
carrier 文件名后，需要重新 preview/apply，让新的实际文件名也进入排除项。
从 D3D12/Vulkan 改回 D3D11 时，工具不会自动重新启用用户配置；如需撤销
本次追加，应使用对应 receipt 执行 restore。

## 使用

默认只输出 JSON 预览：

```powershell
python scripts/configure-carrier-exclusion.py --ini '<ReShade.ini>' --api d3d12
```

确认调用方 profile 已明确 API 后执行写入：

```powershell
python scripts/configure-carrier-exclusion.py --ini '<ReShade.ini>' --api vulkan --apply
```

每次独立 CLI apply 会在该次调用内先建立 plan，再在写入前重新校验 plan
读取的 SHA256。独立 preview 与后续 apply 之间没有共享 token，后续 apply
会重新读取当前文件。工具在 INI 同目录以排他创建方式保存原始字节备份和
JSON receipt，再通过同目录临时文件原子替换 INI。工具只在末尾
追加一个独立 `[ADDON]` / `DisabledAddons` section，保留 UTF-8 BOM、换行、
原内容与现有条目。超过4 MiB、非 UTF-8、符号链接或非普通文件会被拒绝。
为避免 Windows 文本模式在 ReShade 之前截断配置，`^Z` 等非制表符/换行的
ASCII 控制字符也会被拒绝。

恢复时 `--restore` 只接受 receipt 的 basename：

```powershell
python scripts/configure-carrier-exclusion.py --ini '<ReShade.ini>' `
  --restore '<ReShade.ini.carrier-exclusion.YYYYMMDDTHHMMSSZ.token.receipt.json>'
```

只有当前 INI SHA256 等于 receipt 的 `afterSha256`，且同目录备份 SHA256
等于 `beforeSha256` 时才恢复。期间用户修改、receipt/backup 路径异常、哈希
变化或符号链接都会拒绝覆盖；成功恢复后备份与 receipt 继续保留为证据。

这是稳妥的下次启动预加载配置工具，不证明 BG3、2077 或 #153 已修复。
真实 ReShade parser、映射前跳过和 DllMain 未执行由
`scripts/validate-reshade-carrier-exclusion.py` 的独立 Windows fixture验证；
NVIDIA、目标游戏、启动/退出生命周期与多 ReShade 场景仍需 ROG 验收。
