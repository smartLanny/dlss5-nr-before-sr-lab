# D21：258／259 接入增量与多层编辑历史修正

## 当前状态

这是源码候选，不是已经合并或构建通过的 D21 addon。

目标：cbfb33919696a2840ed5154c91abaea56228b661（D21 saved-route/FG 分支）。
复用的已有执行后端：04eda2e4601ac6c57a2be85149d9c817d8dbf8a0 中 experiments/nr-edit/。

Win-Devspace 读取和写入可用，命令执行返回 No bash shell found。因此本轮没有运行真实 D21 产品生成、Windows 编译、HLSL、WARP、NVIDIA 模型或游戏。没有提交新 Git commit，没有合并目标分支，没有清理远端 PR，没有修改游戏文件或原有 INI。旧 D19 的 Windows/WARP 结果不外推到本候选。

新增实际 C++ 历史策略和适配器在 Linux 的 GCC 与 Clang ASan/UBSan 测试通过；Core 类型是明确夹具，不是完整产品编译。FP16数值测试4/4、迁移夹具测试7/7、三个错误变体被检出。完整备份包包含测试和状态清单。

## 本轮代码

- nr_edit_chain_history.hpp：记录实际各层 instance/history/尺寸与当帧 Reset；拒绝重复源，核验实际运动参考。
- nr_edit_d21_chain.inl：读取 D21 dual_selected/dual_owner、full_instance/full_adopted 及上下文池；区分真正采用1层、2层和完整最多5层。
- upgrade_v4.py：在导出的旧实验副本中接入全链历史。P1回退、全链恢复、中间层重建/Reset和参考帧变化，均会重置新增编辑历史。保留原模型的Reset/暖机机制。
- Deferred carrier增加1/64、1/8、1、4、16的独立A/B控件。保留旧默认4；小范围提高小残差精度，但更多大修改进入空间尾差，不是降噪强度。
- 构建使用目标的build include path，避免profile构建误取普通build下的头。
- GPU曝光纹理不能靠一个scalar Reset正确归一化；该实验在无法处理这个合同时保留旧路线，不删除D21原产品曝光支持。

D21的第2层缩到N2，之后各层继续N2，并非层层减半。所有NR层处理图像；残差是模型后的重建形式。

旧实验只比较主层instance/history，不足以表示D21的全链身份。这是明确的接入缺口，不是已经证明现有游戏全部拖影都由它造成。

## 执行

普通PowerShell可以运行本脚本，无需Git Bash。使用已经打开的干净D21隔离工作树，不要覆盖根目录的Manager checkout。

```powershell
$W = '<D21隔离工作树绝对路径>'
powershell -NoProfile -ExecutionPolicy Bypass -File "$W\experiments\nr-edit-d21\Prepare-D21.ps1" -CoreRoot $W
```

脚本从本地Git导出不可变04eda2e4的实验目录，修改新副本，再运行D21自己的原生成链、实验接入、双语编译和WARP测试。它不切换、reset、stash、提交、合并、推送、关闭PR或安装游戏。

支持明确指定 -LabRoot、-ToolchainRoot、-ReShadeSourceRoot、-UpstreamRoot、-CodecCacheRoot、-FaceRuntimeRoot。依赖默认从实际.git位置的兄弟dlss5-lab推导，不使用旧PC用户名。

-PrepareOnly仅导出和升级副本，明确标为未构建；-StageOnly仅生成候选，明确标为未编译。默认固定D21，后续已提交版本可以用-AllowDescendant，但依然必须通过祖先与源锚点检查。

dirty、pin失败、锚点变化、旧输出目录均拒绝，不得删检查强套旧代码。实际执行状态在D21-RESULT.json，日志在runner.log和validation/logs。导出的旧README/VERIFIED_BASELINES只是历史材料。

## 画质、光流与PR

残差SR可能降低一些时域闪烁，也可能保留已有拖影、抹掉细节或引入网格。AMDOF只估计运动，仍需历史拒绝/滤波；不默认叠加。

新版Opti的Enlargement -> Matched residual + DLSS还提供“先RR得到干净图，再低分NR，再对修改层跑私有SR”的C+路线。它不同于本轮#258前RR carry，C+在本候选中尚未实现，不能混淆。

固定R/O、实际N1/N2、各层模型参数，先关FG，比较现有NRSR、完整后置、同N低分后置、Deferred和#258；观测暗部噪点、人物运动、遮挡/反射、切镜/曝光及整帧P95/P99、显存。NR计时不包含全部额外SR/合成成本。

本轮未清理PR。必须先获取258/259当前最终HEAD和完整diff，确认独有代码被新的D21候选替代并验证后再标记superseded。不能只按年龄关闭，也不触碰已暂停的257或其他安装器任务。
