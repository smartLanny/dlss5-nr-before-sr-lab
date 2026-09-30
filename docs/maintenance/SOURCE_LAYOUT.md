# Direct Core source / 正式源码构建

`scripts/build-unified-candidate.ps1` remains the canonical Windows entry. Its
exporter, `scripts/prepare-core-source.py`, copies committed Core C++, HLSL,
configuration and build inputs without modifying their contents. Changes belong
in `src/` and the explicitly listed private-SR module sources under
`experiments/nr-edit/`; the former 16 Python transformations are archived.

`scripts/core-source-layout.json` records product identity, the extra build inputs
and manifest contracts. Per-build `source-manifest.json` hashes identify the actual
committed inputs. They are checked against the staged compiler inputs, rather
than against hardcoded hashes of a previous Core revision. A dirty checkout is
rejected; explicit development snapshots remain marked dirty and cannot serve
as release acceptance evidence.

The external carrier stays pinned at `28aed4099b0fe1c207b20b5fee5364c0773c25c2`.
Only the integration diff for `bridge.inc` and `dlss5-bridge.cpp` and six existing
project headers are applied to its temporary export. The upstream MIT license is
retained. ReShade, DirectXMath, NGX and FFX remain external pinned dependencies;
their source is not imported into this repository. Existing RenoDX and MinHook
notices continue to apply.

## Migration proof

The original Beta 13 canonical run exited 0. Its archived manifest SHA256 is
`6d15f8ee05ebee620c7d418f1566e9db6670e5918e1f46f293206f8d26a7f6a0`.
All 327 entries were checked against the retained build. The frozen inventory in
[`321_BETA13_SOURCE_BASELINE.json`](321_BETA13_SOURCE_BASELINE.json) is historical
migration evidence, not a gate that later source changes must repin.

For this refactor only, `scripts/validate-folded-source.py` compares the staged
Core and carrier with that inventory. Product source bytes, configuration and
module inputs are compared independently from exporter/build infrastructure.
The new manifest is expected to differ because its source commit and layout are
new; it must not be presented as the original manifest. Binary identity can also
differ due to embedded build provenance, linker timestamps and paths. Preserve
the original Beta 13 binary identities and report the new build separately.

## 中文使用说明

继续使用统一构建入口。Core 现在直接编译仓库里的正式源码，修复功能时直接修改
`src/`；私有 SR 模块仍使用清单列出的 `experiments/nr-edit/` 文件。无需再改 Python
补丁锚点，也无需给每次源码修改重新填写固定哈希。

本次只整理构建结构，保留 Beta 13 算法、简洁 UI、默认值和 DLL ABI。运行成功、CPU、
WARP、NGX 和游戏验收仍分别记录。新版构建回执不是旧版回执，旧二进制不能改名。
源码对照清单只证明本次折回；未来合法源码修改不受这个历史清单阻断。
