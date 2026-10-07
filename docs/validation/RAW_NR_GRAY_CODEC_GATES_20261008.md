# raw低分NR→一次RR：真实灰阶输入与无模型编解码门

2026-10-08。独立可行性测量，未改产品路线或运行游戏。Core来源为内部组合
`45df68dc80138e89778e544d49f15109ec437a4f`，生产src树
`7fe3819be240c0fad0eb16526e9d4b3ce0a67f87`；337个来源文件及24个链接对象核对。
这不等于#342当前head的二进制。

旧RGB控制图经完整量化/逆proxy区间得到9/9 unknown，原件保留且没有运行模型。
之后仅登记一次有导数功效依据的新灰阶控制，频率、振幅、ROI、NR strength及
完整误差界不变。旧fullhost输入路径错误的冻结件未运行，独立路径变体指向新数据。

## 实际栅格输入

Z/J各32个独立真实D3D12栅格进程，共64次，均一次exit0。Windows复用了一个
PID数字，按PID及进程开始时间计64个身份。实际现场编译shader和系统compiler
一致，RTX5090D同LUID；debug layer不可用如实保留。

176,947,200个同PS32→RTZ16组件、灰阶/alpha/HDR/SV精确门通过。
理想几何残差只作diagnostic unknown，不凭阈值宣布零误差。所有64帧的实测
raw相位项完整加入原U，9个CPU typed控制仍能唯一判别；这只是测量设计的功效。

输入manifest SHA `38a52409a5ffb4a59fdbcc1feeeeece743a09abafb2cc105b1ead8223a0d5245`。
dataset SHA `90096017fea681fdff79e13e19510712e48091ac412b20e329b371c147577d5b`。
analysis manifest SHA `0b940737e3252f747332204e9af624a91b96181f2b105d93e30779d8f6adb1b9`。

## 实际无模型编解码

单次新进程exit0，真实F13 parent Create1/Eval0；没有NR DLL或F18 Init/Create/Eval。
原310.5 native tuple及bridge1acf不变。实际读取38个输入文件/上传32个MV，记录
immutable roots及两个独立recording token，以实际同队列fence值1/2完成后退休。
parent release guard通过，最终PASS在源码检查过的release/destroy之后；这些
清理调用没有逐项返回日志，不能称完整原生故障路径测试。

原图combine恢复RGBA整面逐bit一致，proxy与neural encoded相同，实际生产
FP32 Sample decode结果相同。HDR .25/1/4的完整邻接half与逆映射区间包含原值；
曝光关系stored=.375×scene逐组件一致。Z0实际codec误差与64帧raw误差都加入
原误差界，只增不减，9个typed控制仍唯一。单帧无模型结果不证明模型值域、
历史、所有相位、画质或HDR保真；模型repeat误差仍待实际测量。

运行manifest SHA `cb3653015dc184db4ea44714043e309f1ca78f393e1c2b81a4fa4e298a084a18`。
analysis manifest SHA `2bffa35fe1db47f9fbd784873dbff3c2b46d8cf50c6e467b27a655324f67fffc`。

## 有限后续门

首次F18前只在新probe变体修已证ClearUAV的非shader-visible CPU descriptor，
并持有至精确退休。所有可达失败出口统一落盘自有日志后立即 `_Exit`；用实际
CPU子进程证明不执行atexit/静态析构，再用同host一次无F18部分录制错误控制
验证保留依赖并停止提交/恢复/释放。厂商真实partial-handle、device-loss和
failed-Reset仍unknown，不新增完整故障矩阵。

关键diff及上述小门过后才允许固定四个独立history，16warm/16observe一次；
完整U和paired-repeat保留。NR decoded与editedRaw各自判定；不唯一/不informative
保持unknown，不继承SceneRR c0。当前未运行模型，不宣称新RR可用。

两RR的真实clean-guide producer缺失边界不变；本任务不能替代SceneRR→NR(clean)
→rephase→nativeRR，也不放松生产Before/Feature1门。普通恢复仍为基线。
