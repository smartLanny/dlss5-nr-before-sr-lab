# D6：分层输入调查和模型参数隔离

## 本轮问题与结论边界

维护者新测试确认D5完整亮暗在异环中有效，细节分层的反射调节仍不明显。
异环人脸调节只有恢复默认后较可靠；维护者在2077和黎明行者之血中似乎始终
有效。后两者作为维护者经验记录，不能推出异环缺少遮罩或模型普遍失效。

已核对D5实际配置到模型的路径：UseAutoMask、SkinStructureStrength和
UICorrection均参与模型参数，修改后通过既有实例生命周期应用。不能把
“D合成没有读取遮罩”写成“模型或引擎没有分区能力”。

现有F8资源观察只检查Color、Output、Depth、MotionVectors和ExposureTexture。
因此旧报告未出现UI、法线、粗糙度等，属于未观测，不能证明游戏没有提供。
最新异环报告的Color为R11G11B10_FLOAT，没有alpha通道；Output虽有alpha，
其语义未验证，不能借用为UI或反射遮罩。

## 改动范围

- 对当前NGX参数入口做稀疏、只读的可选资源调查。类型化资源成功时记录描述，
  不明类型的void指针只记录存在性；不解引用，也不扩展通用GetResource白名单。
- 模型基准及撤销只操作七项模型字段，复用配置代次、采集互斥和模型签名更新；
  不主动创建第二个NR实例，不更改D参数、路线、工作比例或颜色。
- 图像菜单开放既有Current中心区域采集，取得同次NR结果参与合成的内部平面。
  不改变F7默认、全屏截图、七组矩阵或GPU采集布局。
- 配置日志补齐D模式、五个系数、预设和光照保护，并与配置代次一同冻结。
  这些是配置值，不称实际GPU选区或模型识别率。

D6不更换D5着色器，不新增深度合成、运动历史或反射识别。本轮先把可用输入
与模型/合成各自的影响变为可核对的证据。后续选区算法应把区域权重与增强
倍率分离，并提供区域预览；不能用放大正亮度变化代替语义反射识别。

## 模型与引擎接口依据

本地匹配的310.8运行库读取DLSSNR.ControlMask、DLSSNR.UI、DLSSNR.UIAlpha
等主机提供的资源句柄。局部消费者证据支持它们作为输入使用；尚未验证全部
GPU通道语义，也未发现自动人脸遮罩的发布接口。不把“未找到”写成无法导出。
运行库二进制和只读分析记录保持私有，没有复制进源码或修改供应商组件。

[NVIDIA的DLSS5研究说明](https://research.nvidia.com/labs/adlr/DLSS5/)描述了
画面、运动、时间状态和艺术控制值参与模型，但没有在该页面提供遮罩导出接口。
[Streamline帧生成指南](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)
描述HUD-less与UI/alpha输入及Present时的有效性要求；它属于另一个调用阶段，
不证明当前SR参数块含有这些资源。
[RR指南](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md)
描述材质和反射导引，仍需在实际入口确认，不能直接把资源名称当作本游戏证据。

## 验证记录

本文件初稿只记录实现范围。冻结源码、完整本地门禁、实际菜单、包哈希与安装
状态须在执行后补充；不能把D5或其他候选的通过结果迁移为D6已经通过。

[测试方法](../testing/DLINE6_CORE_ACCEPTANCE.zh-CN.md)。
