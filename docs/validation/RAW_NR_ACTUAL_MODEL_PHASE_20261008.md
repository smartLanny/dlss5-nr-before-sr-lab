# raw低分NR：实际模型与回写相位门

2026-10-08。固定灰阶 fixture 的有限可行性结果；未改产品路线、运行库或游戏。
实际来源仍为内部组合 `45df68dc80138e89778e544d49f15109ec437a4f` / src
`7fe3819be240c0fad0eb16526e9d4b3ce0a67f87`，不是当前 #342 head 的构建。

## 实际执行与独立复核

冻结后的 Z/Z2/J/J2 四个新进程串行各运行一次，均 exit0。每组16帧预热、
16帧观察；总计 F18 Init4/Create4/Eval128，原生 F13 parent Create4/Eval0，
132组真实 recording 在同队列精确 fence 完成及 Reset 后退休。没有重试或调参。
冷启动 NR、初始化 raw result1、每进程10个 FROM_ADDRESS 模块 owner、实际
owned device、独立 parent domain、recording lifetime、输入与附件身份均已核。

两个独立 CPU 复核检查实际 manifest 全部700文件，并从四组原始 scan 重算
全部64个观察帧；与冻结分析逐项一致。NR inverse-proxy-equivalent 和 editedRaw
分别使用完整误差界判定，不能继承 SceneRR 的 c=0 结果。

| 项目 | 实际结果 |
|---|---|
| NR 与 editedRaw | 各自64观察帧均满足预注册有限目标 c=s |
| 非零 s 的 J/J2 | 每路26例，在 c=0、+s、-s 中唯一选择 +s |
| 零 s | 候选别名合并，满足物理运动基线；不能用来判别 jitter 符号 |
| paired-repeat 角误差 | 两路各三频率均0；对应160附件逐 SHA 相同 |
| NR 目标最坏残差，rad | 0.007570 / 0.007081 / 0.010437 |
| editedRaw 目标最坏残差，rad | 0.004229 / 0.004213 / 0.007328 |
| U 分量最大值，rad | 0.018115 / 0.034207 / 0.068528 |

这只证明固定 ROI/三频率/有限候选的相位相容，不能宣布连续相位精确或任意
内容成立。原 raw、codec、量化区间及 paired-repeat 误差全部保留，只增不减。

## HDR 与范围

固定曝光 stored=.375×scene、NR intensity1.5、既有 curve/codec 未改。必需
HDR patch 的相邻 half/逆映射区间均有限可逆且包含实际点；实际 NR 增益如实
记录，没有要求模型输出 unity。scene .25/1/4 的 NR inverse-equivalent 平均
RGB 增益范围分别约 .981–1.031 / .810–.849 / .521–.557；editedStored 约
.995–1.025 / .923–.939 / .982–.983。inverse-proxy-equivalent 不是物理
scene radiance 声明，也不是复杂 HDR/自动曝光的画质验收。

## 证据身份

- 冻结执行包 manifest（43件）：`f5c262f233b1f57ddf07bf1797866931a27ef3bee3802ce477f79700fbaf1cf1`。
- 实际 manifest（700件）：`fdd7ab9d7c99e281b8a2562258b5298cd22e97ea9f709589ce2f3e039bdf1f0c`。
- 实际 validation：`0298132246bc309ebabc6f5068cae0c7825d271b38f2a6494394bf94f52be38e`。
- 相位/HDR分析：`928531e851448bd28ca9f7d5bde1f8da8655aa016d5687c54403361ba9d2a940`。
- 独立 CPU 收据：`2f941928f1e3ac45498eef150d609c8348e3ca5b1d7f3898bb68bc4d6640577f`。
- 已审最终模型关键 diff：`c300285c0cc68ff4c29293fc7acf8971f64f768e98ee6f6f95426dadd332ae51`。

自有源码审查片段见 [raw_nr_probe_review](raw_nr_probe_review/README.md)。这些
经过路径清理及 LF 规范化的片段不是完整可构建包，不重标封存 EXE 的来源。
模型准备时的 Python wildcard import 曾误指旧目录，截断一份58字节 CPU 日志；
在任何模型运行前已从可信封存件恢复并逐 SHA 核回旧包全部32文件。未保存错误
瞬间的零字节日志副本；事故记录是事后记录。原 manifest 未改，新执行脚本已改
显式 import/自身目录，实际 GPU 前重新核旧件与新冻结包。事故记录 SHA：
`c26de9b650297d1bbeb9240ddb6630a17238abf57759108fbbad815156d9712e`。

## 本批最后一门与交付边界

上述 parent 是 R→R DLAA Create，Eval0；**尚未执行完整 rawNR→nativeRR R→O**。
这是本报告四 NR histories 的截止范围；后续最后切片结果见
[一次原生 RR 端到端功能结果](RAW_NR_ONE_NATIVE_RR_FUNCTIONAL_20261008.md)。
Astra 建议只再做同宿主一个固定端到端切片：R1280×720→O1920×1080，真实
cap/optimal 在唯一 parent Create 前必须精确支持该 R；原生 RR 基线和
rawNR→一次原生 RR 两条独立 J 历史，各16warm/16observe，一帧仅一次 RR。
只替换 Color，raw guides/P0/j=-s/曝光保留，所有中间依赖活到 RR 消费完成及
精确退休。关键 diff 先审；合同拒绝即停止，不扫描参数或扩大测试系统。

即使此切片成功，也只完成受控功能里程碑，仍无产品接入、真实3D或游戏画质/
性能证据。两 RR 路线仍因真实 clean-guide producer 缺失而阻塞，不以一次 RR
替代。官方 NRD-Sample 适配本批 STOP；不复活该测试台。普通恢复仍为基线，
私有 SR 缺净优势不再送画质 A/B，0.4.7 性能差没有同条件因果修复结论。
