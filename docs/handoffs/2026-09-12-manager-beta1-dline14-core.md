# Manager 0.4.9-beta.1：D14 Core 替换

本分支基于 Manager beta.7，版本为 `0.4.9-beta.1`，Windows 文件版本为 `0.4.9.1`。

内置 `0.5-dline13` 测试槽替换为 `0.5-dline14`。D14 Core 来自
`codex/050-corefix8-integration-20260911` 的 `1cabf797a24256f1b40f5653a0dcdda9f3f97c14`，
来源清单为 `da0dd185085e0f3992fa23d5808fe9ddd79fab73511327c84dfc0f45c4ab89eb`。

| 文件 | SHA-256 | 字节数 |
|---|---|---:|
| 中文 D14 Core | `d96726c3f74f90c1309994e6f4e7c3825907fc35b348ea53fb188b4373810290` | 9,167,872 |
| `nrchain_nvngx.dll` | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` | 8,192 |
| `nr_before_sr.ini` | `84cb54bea2d2fcdd0ede005741d413a80f082d960752be1ae8912c28cd0204a8` | 6,943 |

该候选继续标记为 `coreUpdateOnly`、`D3D12-x64`，不包含 DX11 carrier。现有 0.4.7beta
的 DX11 carrier 兼容关系保持不变；本轮没有把未实测 carrier 自动配给 D14。

仓库按项目边界不提交运行时二进制。发布前的本地 payload 由上述摘要校验；切换到本分支后，
需要在本机准备 `payload/nr-before-sr/versions/0.5-dline14` 槽位才能进行本地打包。
