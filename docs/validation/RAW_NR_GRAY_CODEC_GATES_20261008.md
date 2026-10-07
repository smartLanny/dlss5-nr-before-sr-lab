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

v3的唯一实际无NR控制在正向首段提前拒绝，未到预定第二段错误：真实Clear
成功录制后，`ObserveSlot` 首查proxy无状态记录，按原门退出42并保留三份lease/
descriptor闭包。此分支绕过encode，只对neural转换；新proxy虽由factory以
COMMON创建，但当前recording从未录制其transition。Reset后同目标neural
转换也会no-op，不能当新状态证据。因此控制未通过，不把退出42单独算PASS。

最小新v4仅补control的真实状态来源与barrier：首次绑定实际fresh factory
COMMON，后继绑定相同资源的前次实际观测及精确退休；同目标经合法中间状态
再回目标，每次重新StateOf核验。原ObserveSlot/生产guard不改、不直接填表。
旧v3保持失败封存，模型仍禁用，下一次执行须独立窄核和GPU窗口。

v3实际manifest SHA `9dee0735b3c950496c10912f7dc1519e8cff1d00e10c614daffa7ab004371c03`。
独立定位 SHA `854eb7507f46a72323e5322ac3b79e36d4896c835096792175a68379330984e3`。

v4经独立窄核后只执行一次，注册纪律门通过。正向首段两资源实际transition/
StateOf成立，neural整面FP16 Clear读回d2e4逐bit一致，Scope恢复及同recording
精确fence/Reset退休完成。第二记录真实命令后受控BAD00006，实际token3三份
lease/CPUheap闭包仍持有并quarantine，提交/恢复/combine/release/retry增量为0，
日志flush后实际退出42。此时只有F13 Create1/Eval0，NR Init/Create/Eval0。
原厂partial-handle/device-loss/failedReset仍未测试，不能外推。

v4实际manifest SHA `e3c61bb691ae041cab5d304325cb292c35425be6aec0f3607c46b1de8ddab888`。
外层known-process监督 SHA `01a1dc101cfdaee57e5e9c0937384ab4b3a2b17e97e331887209f6f43381be37`。
独立窄核 SHA `97facac911e1507ad939d4d17b44ed0918fc403f489ed9a17dee4fd3f04a6c82`。

关键diff及上述小门过后才允许固定四个独立history，16warm/16observe一次；
完整U和paired-repeat保留。NR decoded与editedRaw各自判定；不唯一/不informative
保持unknown，不继承SceneRR c0。当前未运行模型，不宣称新RR可用。

两RR的真实clean-guide producer缺失边界不变；本任务不能替代SceneRR→NR(clean)
→rephase→nativeRR，也不放松生产Before/Feature1门。普通恢复仍为基线。
