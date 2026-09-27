# Beta5 异环一键对比崩溃（2026-09-27）

状态：实际游戏验收失败，原 Beta5 候选暂停交付。源码 de8d5ab851e1f635e4bdb14b60fe8a5cc8aa84c7；实装中文 Core SHA256 3c7d306d5b901a5daa64a99e199ba99eecafd1ca48dcd01cd243036ac07fd3f5，与冻结候选一致。用户操作游戏，代理仅取证，未重试游戏。

23:39:25 开始一键对比，组0关闭NR、组1普通后置NR100%、组2普通后置NR50%各成功保存三张。23:39:36.561切到组3私有SR50%，约23:39:39崩溃。未完成对比页导出，空会话目录不等于没有保存截图；九张原图在ReShade配置的游戏目录中，均已保全。

异常为访问冲突读取0xC。最终map与模块身份对应的Core+139293位于私有Feature1 Create调用，Core+ead1c位于StateHookSetDescriptorHeaps最开始的原始转发调用。D3D12Core+2425be读取第一个descriptor heap内部+0xD8所指结构的+0xC，而该内部指针为0。原调用count=2；this的0x110差值有合法调整thunk对应，不据此认定this错误。

初始转储阶段尚未证明是谁造成对象内部指针无效（后续受控复现见下文）；minidump缺少heap/vtable与原转发槽页。重点鉴别NGX初始化域和实际命令列表的ReShade包装层是否匹配，不能把问题直接归给截图保存API、混版、显存不足或已证明的游戏/驱动缺陷。

证据仅在本地：`D:/ChatGPT/DLSS5/evidence/beta5-nte-comparison-crash-20260927/capture-manifest.json`（17份原字节文件：CrashContext、minidump、日志、配置与9张图）；`D:/ChatGPT/DLSS5/evidence/beta5-nte-comparison-diagnosis-20260927/`（符号、反汇编、寄存器/栈与边界说明）。原始用户ID、转储和截图不提交Git或发布。

游戏已退出。当前INI的PrivateResidualSR=0；用户游戏会话后的INI与安装前不同，因此保留当前文件，不用部署旧备份覆盖用户此后设置。Beta4回退包仍可用，尚未实际回退。Beta5 OTA保留原哈希作为故障证据，未在线发布。

## 历史：受控创建复现与已撤下的heap-only尝试（2026-09-28）

使用游戏同一 ReShade 6.8 DLL 的独立 D3D12 宿主复现：把包装后的 descriptor heap 传给原生命令列表，发生同一 D3D12Core+2425be 读取 0xC；原生 NGX 创建也实际传入两个包装 heap。在既有私有调用范围内将它们转换为原始接口后，首次 Create 与两次提交/fence 通过。单独宿主 Evaluate 的参数对照两边均返回 BAD00005，因此该实验只证明创建故障机制，不算产品完整推理通过。

删除 Create 参数中的资源绑定未能消除故障，未采用该猜测修改。仅按物理设备判同不足以证明命令列表与 descriptor heap 的接口域兼容。修正保留原初始化、录制、提交与退休合同；仅私有 NGX 调用转换 heap，普通调用不查询或改写。QI 返回引用持续到转发及状态记录结束后释放。

后继版本为 Beta6，完整 Core + ReShade 创建、推理和回收及统一构建仍需完成后才能交付。Beta5 的所有二进制和 OTA 哈希保留，不覆盖。窄回归证据：`D:/ChatGPT/DLSS5/evidence/private-descriptor-heaps-exact-hook-r2/validation.json`；受控机制证据：`D:/ChatGPT/DLSS5/evidence/beta5-proxy-domain-host-r4/validation.json`。这些本地测试均非游戏验收。

### 完整推理对照后的修正

完整 Core + 同版 ReShade 的受控检查证明：只解包 descriptor heap 后，首次 Create 成功，但下一帧私有 Evaluate 仍访问冲突；因此该中间改法没有进入交付。ReShade 同时拥有包装 heap 和虚拟 CPU descriptor 句柄，不能只转换一部分接口协议。

仅将私有原始 NGX Create/Evaluate 的命令列表改回当前已验证的原始入口接口，其余 Core 资源、采集、合成、raw 录制与提交/退休不变，受控对照完成1次创建、5次推理、6次提交和1次退休，689650项检查、退休资源0、原始Shutdown成功。该对照替换过测试转发器，属于方案证据；产品完成后仍需用未替换出口的真实生成 Core 复验。证据 `D:/ChatGPT/DLSS5/evidence/beta6-reshade-core-proxyforward-r1/result.json`。

最终接线复用已有原始调用 AliasScope：核对当前epoch、native地址、token、Reset序号和device身份，且原始接口的QI必须指向同一个raw列表；只在当前调用内借用，不跨帧缓存。Create/Evaluate每次调用前重验，无有效alias时无论raw或proxy均拒绝并保留Z，不使用GetDevice兜底。首次仅heap归一化代码撤下，历史提交和失败记录保留。

扩展测试宿主把整个NR链切到proxy导致反向混用，并在Feature18创建前崩溃，此用例不能作为产品NR失败或通过；本轮ReShade专项使用人工残差隔离私有Feature1，真实Feature18多层继续使用原来的完整Core独立验证。所有边界在最终清单分开记录。

### Beta6最终软件验证与交付

最终编译源码 `e7407a39d12a6d43bf4e557f8c041e8361296831` 的统一构建及实际生成Core复验通过。未替换NGX出口的同版ReShade SR/RR-only专项各完成Create1/Evaluate5/submit6/retire1，缺alias反例拒绝且不分配；均使用0层NR人工B/Z。原生接口真实Feature18另验SR两层25%奇数尺寸及RR-only五层50%，不能合并成ReShade多层已通过。最终证据、组合夹具失败、包身份、部署/回退和待游戏范围统一见[Beta6验收](052_BETA6_ACCEPTANCE.md)。6sol只换文件未启动游戏，Beta5真实崩溃记录不因软件通过而撤销。
