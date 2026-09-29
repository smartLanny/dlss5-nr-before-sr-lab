# Beta 6：一键画面对比手测

本清单供维护者在安排游戏验收时使用。先核对游戏已安装**最终冻结的 0.5.2-beta.6 候选**及对应回退包；最终 Core、nrchain、运行库和 ZIP 的哈希以交付清单为准。本清单不把软件或受控 NGX 测试当作游戏通过。

在实际游戏场景保持固定视角，打开“实验”页，点击“开始自动截图对比”，然后关闭菜单。程序仍按九组完整顺序运行；**没有单独启动第 4 组的按钮**。若需取消，重新打开菜单点击“停止并恢复”。完成后由用户点击“打开对比页”，不会自动打开浏览器。

| 组 | 自动比较内容 | 本轮关注 |
|---|---|---|
| 1 | NR 关闭，游戏原生路线 | 确认基线截图 |
| 2 | 后置 NR 100%，空间恢复 | 确认正常截图 |
| 3 | 后置 NR 50%，空间恢复 | 确认正常截图 |
| 4 | 后置 NR 50%，私有 SR | **优先核对首次私有创建、后续画面与截图** |
| 5 | 后置 NR 25%，空间恢复 | 组 4 后仍能继续 |
| 6 | 后置 NR 25%，私有 SR | 再次核对私有路线 |
| 7 | 后置 NR 50%，游戏 MV 累积 | 观察实际准入与历史采用 |
| 8 | 后置 NR 50%，AMDOF 普通累积 | 观察实际准入与历史采用 |
| 9 | 后置 NR 50%，AMDOF 低频时域 | 观察实际准入与历史采用 |

每组预热后最多保存三张；不支持、未采用、超时、尺寸或配置变化都会记录原因，缺图不是效果一致。对比页截图属于不同时刻的显示画面，不是同帧模型像素证明；HDR 沿用 ReShade 保存格式，浏览器显示不等于线性 HDR 原始像素。游戏 SR/RR、HDR、FG 和相机保持由维护者控制，程序不自动改这些设置。

Beta 5 异环故障发生在第 4 组首次私有 Feature 1 创建；前三组已保存图片。Beta 6 对描述符堆代理/原生对象域的修正需要**这次游戏实测**确认：第 4 组进入后不崩溃、确有私有 SR 采用和图片回执、后续组能继续。若再次崩溃，保留本次游戏日志、现成崩溃报告和已生成的截图/清单，记录最后完成的组及时间，再使用回退包；不要以已有受控测试替代此判断。

完成、取消、运行时退出或 Present 停止时，核对原插件设置恢复，用户 INI 保持原字节。若只想优先验证私有 SR，仍需让现有一键流程经过前三组；可以在第 4 组完成后按“停止并恢复”缩短后续测试，但这不算九组完整通过。截图过程包含预热和写盘，不用于性能计时；运动中的拖影、遮挡、切镜、地图/Esc 恢复及完整游戏帧 ≤3% 成本应另测。

Core-only OTA 仅更新 Core 与对应 nrchain。退出游戏后安装，保留 INI、NR 模型运行库、原生 SR/RR/FG 和面部资源；20/30 与 40/50 运行库按硬件分组独立核对。旧 D21 辅助层比例迁移到主工作网格后，先确认实际负载。游戏测试由维护者进行，6sol 只部署、核对和准备回退；2077 留到最后。

# Beta 6: manual picture-comparison check

Use this checklist when the maintainer schedules game acceptance. First verify the **final frozen 0.5.2-beta.6 candidate** and its rollback package. The delivery manifest will identify the final Core, nrchain, runtime and ZIP hashes. Software and controlled NGX checks are not game acceptance.

In a live game scene, hold a fixed view, open **Experiments → Start screenshot comparison**, then close the menu. The controller still runs the full nine-group sequence; **there is no button to start group 4 alone**. Reopen the menu and use **Stop and restore** to cancel. Use **Open comparison page** after completion; the browser does not open automatically.

The groups are: (1) NR off; (2) post-route spatial NR 100%; (3) post-route spatial NR 50%; (4) post-route private SR 50%; (5) post-route spatial NR 25%; (6) post-route private SR 25%; (7) post-route native-motion accumulation 50%; (8) post-route ordinary AMDOF 50%; (9) post-route low-frequency AMDOF 50%. The immediate priority is the first private Feature 1 creation in group 4 and then whether later groups continue. Each group can save up to three screenshots after warmup; unsupported, inactive, timed-out or changed routes receive a reason. Missing images are not evidence of matching quality.

Beta 5 saved three images in each of its first three groups before crashing on group 4. Beta 6's proxy/original descriptor-heap correction still needs a game test: group 4 must enter without a crash, show actual private-SR adoption and produce screenshot receipts, with later groups continuing. If it crashes again, preserve the game log, existing crash report and any screenshots/manifest, record the last completed group and time, then use the rollback package. Controlled tests cannot substitute for this check.

The images are separate display times, not paired same-frame model pixels. HDR uses ReShade's saved format; browser display does not establish raw linear HDR equivalence. Game SR/RR, HDR, FG and camera remain under maintainer control. Completion, cancellation, runtime destruction or stopped Present should restore the original plugin settings without changing the saved INI. To focus on the private path, the maintainer may cancel **after** group 4 and verify restoration, but that is not a full nine-group pass. Screenshot runs include warmup and disk I/O, so assess motion artefacts, Esc/map recovery and whole-frame cost separately.

The optional Core-only OTA updates Core and its matching nrchain only. Exit the game before installation; retain the INI, NR model runtime, native SR/RR/FG and face assets. The 20/30 and 40/50 runtime families remain separate. Check workload after migrating old D21 auxiliary scale settings. The maintainer performs all game tests; 6sol only deploys, verifies files and prepares rollback, with Cyberpunk 2077 last.
