# #208 肤色补充：独立标量实现与离线范围工具

本切片新增 `src/nr_skin_protection.h`，同一份标量实现可编译为 C++ 和 SM5 HLSL。
它没有接入运行时、菜单、INI、F7/F8 或资源生命周期；没有改模型 AutoMask/
SkinStructure，也没有修复或恢复调查 #162。基础工作树为 #200 `2e5bf87`，只说明
本切片祖先，不表示该 Core 已通过当前菜单验收。

## 可直接接续的接口

| 入口 | 合同 |
|---|---|
| `NrSkinDefaultRequest()` | enabled=false，明暗/颜色保护均0。0–1对应0–100% |
| `NrSkinClassify(reference, admitted)` | 原始显示参考RGB → `{confidence, valid}` |
| `NrSkinProtect(original, candidate, mask, request)` | 独立削弱本合成点的亮度/色度修改，返回RGB、applied和reason |
| `AdmitReference` / `SameSource` / `AdmitMask` | CPU上的输入、源帧/源代次/视图/曝光/颜色合同代次、rect/相位及校准代次准入 |
| `WeightedMask` | 可选CPU参考；非负权重、有效性与confidence分开；有贡献的无效tap使结果无效 |

C++ 命名空间是 `nr_skin_protection`。HLSL包含/拼接同一头文件前定义
`NR_SKIN_HLSL=1`，函数名保留 `NrSkin` 前缀。CPU结构含bool和平台布局，**不能直接
上传为shader常量/纹理结构**；接线端显式转换为已有配置快照中的标量。

`applied` 仅表示此次合法请求通过计算，不表示识别成功，结果也可能与候选完全相同。
reason区分关闭、无效mask、有效零选区、无效请求、零保护、非法合成输入、黑位色度
未定义、输出超域和已应用。不能将补充没有选中与NR整体旁路混为一谈。

## 输入和颜色语义

分类只接受**未经NR改动、曝光依据一致的sRGB编码BT.709显示参考**，RGB范围[0,1]。
该输入可以由当前已验证的原始display-reference Proxy提供；格式、纹理地址或
“HDR勾选”不能证明其语义。未知HDR/PQ/scRGB、raw linear、NR输出、负值、NaN或
大于1的编码数值不做猜测、不裁成SDR。若现有路由不能提供这份参考，本层不生效。

分类采用BT.709的Y'差色坐标：`Cb=(B-Y')/1.8556`、`Cr=(R-Y')/1.5748`。
可预览的保守椭圆种子为中心(-.075,.105)、半径(.090,.115)，平方半径.30→1软退出；
色度长度.025→.060、Y'.030→.100分别作低饱和/近黑软门。
这些数值是独立实验初值，**不是已验证肤色分布或推荐阈值**。

合法灰色/低饱和/近黑像素返回valid=true、confidence=0；它们与缺失/不支持参考不同。
暖木头、皮革等可以与皮肤拥有同一颜色而误选；彩光、低饱和动漫皮肤可以漏选。
本切片没有位置记忆、mask膨胀、脸/头发/衣物分割或额外AI；不能处理所有人物外圈
辉光。辉光仍沿#86的宽正编辑单独控制。

## 独立保护端点

合成输入original与candidate必须是**同一合成点、同一曝光、非负finite线性BT.709**
RGB。分类编码域和合成线性域是两个明确不同的接口，调用端不能混用。

令 `Ys=Y(original)`，`Yc=Y(candidate)`，`a=M*preserveLuma`，`c=M*preserveColour`：

```
Yout = (1-a)*Yc + a*Ys
Qout = (1-c)*(candidate/Yc) + c*(original/Ys)
output = Yout*Qout
```

- 关闭、无效mask、M=0或两个保护都0：位级返回candidate。
- M=1且两个保护都1：直接返回original RGB，包括合法黑色端点。
- 仅保护颜色：保持候选亮度；仅保护明暗：保持候选归一化色度。
- original==candidate：直接保留原值，不制造新残差。
- 部分保护遇到任一亮度≤1/65536时，独立色度无定义，保守返回candidate；不造颜色。
- 非法请求、负值/非有限输入、超FP16有限范围输出：返回candidate而不作隐式裁剪。

亮度及归一化色度各自在原始与候选端点间作凸组合。它们各自有界并不保证任意
重组都能落入存储/色域上限，因此超域明确回退，最终HDR/headroom/ratio/gamut预算
仍由既有合成链共用。RGB接口不接触alpha；运行接线必须保留原生产alpha路径。
这只是该合成点的端点，不能声称前置NR之后原生SR/后处理每个像素都锁回原图。

首轮运行接线应在当前已验证合成位置独立执行一次，共用现有最终安全预算。不能再
建第二套HDR转换/肤色预算，也不因本层开关改模型参数、强度、N/R/O或模型history。
不将低网格前移当作等价优化。跨尺寸/源帧/源代次/视图/曝光/颜色合同/rect/相位的mask拒用；
不以Present计数或复用纹理地址证明配对。纯合成请求和F7冻结沿现有配置所有权。

## 本地验证和工具

```
python scripts/validate-skin-protection.py --compiler <clang++-compatible-compiler>
build/skin-protection/skin-protection-fixture.exe --samples
build/skin-protection/skin-protection-fixture.exe --original-srgb709-ppm original.ppm --mask-pgm new-mask.pgm
```

工具只做离线颜色范围数据分析：P6 PPM→P5 PGM confidence；输入合同由显式参数声明，
不是工具自动证明。仅使用已授权且确实属于该颜色合同的原始画面。工具不读取游戏、
不截图、无网络/模型调用、拒绝覆盖输出，并限制为单帧/32M像素/8192单边。
PGM是保护范围数据，不是修图。当前只用了自建合成样本，没有选取或外发私人图像。

验证直接编译生产头文件，覆盖合成肤色/非肤色/彩光/低饱和/近黑/非法数值、端点、
纯灰、输入身份拒绝、非负mask插值、2万组HDR量级随机输入的双精度独立公式对照。
错误地把颜色旋钮绑定明暗旋钮的负控必须失败。共享HLSL使用位模式finite判断，
避免SM5优化将`isfinite`判断省略；初轮isfinite的warning-as-error失败据此修正。

CPU及SM5编译、离线PPM读写结果和源码SHA256保存在本地
`build/skin-protection/validation.json`；逐条命令历史在同目录，均不提交源码仓库。
SM5是**编译检查**，未做WARP dispatch、真实NGX、运行接线、游戏或性能验收。
alpha/实际subrect映射/资源退休及配置冻结须随接线另验；不能以CPU元数据测试冒充。

## 后续独立验收

按#208在实际授权静帧/短序列比较写实/风格化脸、肤色、彩光、木头/皮革/火光、
发丝/衣物/字幕的误选和漏选；先展示实际范围再考虑阈值校准。已有2077模型人脸
路径保持，异环只作自愿后续样本，#162持续暂停。新功能仍默认关，不进入首批三档
光影口味的自动写集；完成接线、WARP和游戏验证前不称“补充保护已交付可玩”。

来源：https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/208
