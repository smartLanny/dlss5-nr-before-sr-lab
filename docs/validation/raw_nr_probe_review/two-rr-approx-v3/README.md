# 私有 two-RR approximate v3：仅纠正 alpha 前提

v2 实际 child/NR/parent 各 32 Eval、34 个真实 recording 退休及 RGB finite 通过，但 composition 100% identity/拒绝，composite 完整 RGBA 等于 raw，最终 O 全等旧 baseline。确定原因：自有 shader 错误限定 raw alpha=1，实际 frozen raw 每像素固定 `.75` (`0x3a00`)。这不是算法或 approximate raw guides 不可行的证据。独立 v2 review MANIFEST `60713c61216d10ab0979e1f5f93719483273681435b913ccd02df538824701d6` 保留结论。

v3 已获 root/Astra 的固定域 CPU 准备授权。唯一 native shader 源码变动为 alpha 编辑前提 `raw.a!=1` → `raw.a!=0.75` 及相应注释；其余 HLSL 完全相同。未知 alpha（包括 1、NaN payload、正负零等）仍整像素退回，identity 也不写。raw→composite 的同 recording CopyResource 底图与实际 RGB/分母/相位/越界拒绝域不变；没有泛化任意 finite alpha、clamp、epsilon、额外保护或改变 NR/NGX 参数。

只 CPU 编译新 `ratio-before-shift.cso`；没有重编 host。原 v2 EXE `52f21828f4eea2ce2496aef71b3321c5fbf72cd7ca43b28f46087f94db5e04ef` 与所有 C++/INL/object/其他 shader 字节不变。EXE 使用相对路径加载新 CSO；新 packet 和新 CSO 身份单列，不把原 v2 actual 称为 v3。仍链接 canonical-r3 Core `45df68dc80138e89778e544d49f15109ec437a4f`，不是当前产品 head。

14 个数学/位型 CPU 反例及 13 个 alpha 源码导出 CPU 反例通过。旧真实 HLSL 提取的 alpha 前提加旧 oracle 对 `.75` 非 identity 必拒绝，新真实 HLSL 提取前提加新 oracle 正确计算 RGB 且保留 `0x3a00`。CPU D3DCompile/disassembly 确认新 predicate 编译成功；这些不是 GPU equivalence。v2 实际 clean/edited bytes 的 v3 预测单独标为旧数据 CPU 预测，不能算作新实际结果。

路线与录制合同不变：raw R → 独立 DLAA RR R→R → clean R → 单层 NR N=R/color jitter0/raw guides approximate → edited R → 逐 tap ratio 后正权 bilinear 搬移 `-j` → 乘 raw composite R → 原始 parent RR R→O。同 original native/device/init domain，不重 Init；child 与 parent 独立 history；原 parent 仅换 Color，所有 guides/scalars/matrices 保持；所有 packet 保留到同实际 queue/fence/value 完成且 native Reset 成功。

新执行规模只一个进程、J32（16 warm + 16 observe），R=N=1280×720，O=1920×1080，child/NR/parent 各 32 Eval，两个 Create + 32 帧=34 个新 recording。仅继承 v1 已完成 64 控制帧及原 EXE 身份，固定 actual `97475bee24b6cf47828b0fc2f88e8074f868b6f6eba9f66c625d1468a5fe492f` 与独立 review `32630ebbb699c0ec938144913529095554202c8b910461ac96c0b1483971348e`。不继承 v2 新路线的有效编辑 PASS。

新 receipt 在既有计数、runtime、原合同、34 实际退休、RGB finite/poison、oracle ≤1 FP16 ULP 之外，还逐像素核原 raw alpha=`0x3a00`、全部 composite alpha u16 不变、identity/拒绝完整 RGBA bits 不变。每观察帧预测 RGB 改变和实际 GPU RGB 改变都必须 >0；所有实际改变只能在预测合法域内，合法域值须匹配 oracle。oracle 合法域计数与实际变化像素分开报告：合法写可能舍入为不变，没有新增 GPU write-mask 仪器，不能声称直接观测每个 UAV 写。预测/实际 changed-mask 差异照实记录，既有 ≤1 ULP 数值容差不扩大。

root 窄核后串行运行：`python run_two_rr.py --root-two-rr-go`。新 sibling 输出固定 `two-rr-approx-v3-r1`，存在即拒绝。没有 flag、进程 guard 不通过或任何首失败都停止，不重试、不叠保护、不改旧 actual。准备阶段 GPU/NGX=0，产品/游戏操作=0；画质/性能/游戏验收 unknown。

3D 只读接缝评估已在 v2 独立收据 `THREED_PORT_ASSESSMENT.md`。现有 SceneFrame 的 raw alpha=1，因此本 v3 `.75` 编辑合同不能默默用于该 3D producer；3D 时需另行明确批准其输入合同。
