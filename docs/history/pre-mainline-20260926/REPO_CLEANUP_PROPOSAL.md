# 倉庫整理清單與待討論問題（2026-09-25）

> **給誰看：** 負責維護本倉庫的 agent，以及維護者本人。
> **誰寫的：** 雲端 Claude Code session（Linux 容器，沒有 Windows 構建、GPU 或遊戲能力）。
> **可信度：** 文中的 SHA、數字和清單都是 2026-09-25 從遠端實際讀取或實際跑出來的。動手前請重新 `git fetch` 核對，遠端可能已經變了。
> **這份文件不是命令。** 第 1 節是維護者已拍板的決定，第 2 節是已經做完的事；第 4 節起全是**提案**，要討論後再分工執行。

## 0. 一句話現況

0.5.1 才是真正的主線，但它還掛在 PR #300 的分支 `codex/d21-unified-next-20260920` 上。`main` 仍是 0.4.7beta，`next/0.5` 停在 9/14。倉庫裡還有 177 個遠端分支、7 個 open PR、97 個 open issue，而且 AGENTS.md 內容自相矛盾。

## 1. 維護者已拍板的決定（2026-09-25）

1. **0.5.1 要成為 `main`。** 具體怎麼升，待本清單討論後再執行。
2. **0.4.x 完全凍結。** 不再出 0.4 版本，只保留 tag 和 Release 供查閱。
3. **Manager 已遷到獨立倉庫。** 本倉庫裡的 Manager 分支凍結並清理；Manager 另開 session 維護。
4. **UI1 包（#307，`21db404`）遊戲實測通過。**

## 2. 本 session 已完成的事

| 動作 | 結果 |
|---|---|
| #307 合入 #306 的分支 | merge commit `8ea14844` |
| #306 合入 #300 的分支 | merge commit `5364a37b` |
| 驗證 | #300 分支 HEAD 的 tree（`3891ebfd`）與維護者實測的 `21db404` **完全相同**，沒有新增內容 |
| 紀錄 | 已在 #300 留言說明 |

**沒有動的：** `main`、`next/0.5`、#308、所有分支刪除、tag、Release、issue。

## 3. 目前快照（2026-09-25）

| 項目 | 狀態 |
|---|---|
| `main` | `dae86e9`，0.4.7beta corefix.8 加上 bug-inbox 證據。**所有程式碼都已包含在 0.5.1 裡。** main 獨有的只有三樣：116 個 bug-inbox 檔案、`docs/compat/COMPAT-PASS-MATRIX.md`（遊戲過測列表，15 行），以及另一個版本的 `bug-inbox/issue-177/KEY.md` |
| `next/0.5` | `cbfb339`（9/14），是 #300 的祖先 |
| #300 分支 | `5364a37b` = 0.5.1 Beta（unified10）+ Inbox1（#306）+ Reconstruction1-UI1（#307） |
| #308 | 開啟中，head `1d1e6d33`，base `codex/051-guest-comparison-20260925`（即已合入的 `21db404`） |
| 托管 CI | 最近 100 次運行（9/9–9/23）**0 次成功**：73 次失敗、27 次取消。失敗的 job 約 3 秒就結束且沒有日誌，符合帳單或額度被擋的情況。堆疊 PR 的 base 不在 `pull-request-validation.yml` 的清單內，所以 #306、#307、#308 從未觸發 CI |
| Release | 最新已發佈的是 `beta0.4.6`（9/07）和 `beta0.5-dev8`（9/08）。**0.5.1 系列沒有 tag，也沒有 Release**，OTA 包只存在本機。另有 2 個 draft Release |
| tag | 28 個，其中 3 個屬於 Manager |

## 4. 待辦提案（按建議順序）

### 4.1 重寫交接文件（建議最先做）

**問題：**
- AGENTS.md（#300 分支上）有 228 行，一層層疊加「current successor」段落。第 170 行說「唯一研究目標是 `next/0.5`」，第 172 行又說「唯一持續開發分支是 `main`」。每個 agent 讀完都會照舊規則繼續開分支、疊 PR，這很可能是分支膨脹到 177 個的主要原因。
- `docs/interop/CURRENT_STATUS.md` 有 989 行、83 個章節；`docs/interop/` 有 183 個檔案；`docs/` 頂層還放著大量 0.3、0.4 的 release notes。

**提案：**
- AGENTS.md 壓到約 60 行，只寫：主線就是 `main`；分支與 PR 規則（見 4.4）；證據規則。原有「Protect private evidence」「Use CI deliberately」「Report evidence accurately」三節保留。最後指向 CURRENT_STATUS。
- CURRENT_STATUS 只保留目前候選、驗收狀態和已知問題。歷史原文搬到 `docs/history/`，**只搬不刪**。
- 在 #300 分支上改，隨升主線一起進 `main`。

### 4.2 保存 0.4 的最後狀態

- 打 tag `archive/0.4.7beta-corefix8` 指向 `dae86e9`。
- 注意：`release/0.4` 分支停在 9/06，**不是** 0.4 的最後狀態，0.4 的最後狀態是 main 的 `dae86e9`。

### 4.3 升 main（已模擬驗證可行）

**衝突情況：** 在 #300 分支上試合併 `origin/main` 會出現 **50 個衝突**，分佈在 `.github/workflows`、AGENTS.md、README.md、`config/`、`docs/`、`scripts/`（19 個）、`src/`（14 個）、`tests/`（11 個）。

**為什麼是假衝突：** main 和 0.5.1 有兩個 merge base（`d7a6def` 和 `cd10d96`），是交叉合併造成的。相對較新的 `d7a6def`，main 只多了 bug-inbox 和過測表，其他檔案沒有任何獨有改動。

**已模擬的解法：** 所有衝突檔都取 0.5.1 的版本（在 #300 分支上 merge main 時就是 `git checkout --ours`）。模擬結果：
- 與 0.5.1 相比，bug-inbox 以外只多了 `docs/compat/COMPAT-PASS-MATRIX.md`（+15 行）。
- bug-inbox 新增 116 個檔案，修改 1 個（`issue-177/KEY.md`，取 main 的版本），刪除 1 個（`issue-177/CLOUD-KEY-excerpt.log.txt`，main 當初刪掉了它）。

**提案步驟：**
1. 在 #300 分支上 merge `origin/main`，用上面的解法處理衝突。
2. 驗證：`git diff 5364a37b HEAD -- . ':!bug-inbox' ':!docs/compat'` 必須是空的。
3. 更新 #300 的標題和描述（目前還寫著 unified10），把 base 從 `next/0.5` 改成 `main`，用 merge commit 合併。不 force push，不改寫歷史。
4. `next/0.5` 轉成 tag 後退役。
5. 升完後由 Windows 端在 `main` 上跑一次完整門禁，確認 `main` 能構建。

### 4.4 CI 與 PR 規則

- **帳單或額度：** 由維護者處理。修好之前，任何托管 CI 結果都是「未啟動」，不是通過也不是失敗。
- **路由錯了：** `pull-request-validation.yml` 對 base=main 跑的是 0.4 時代的 job（baseline-and-release-contracts、exposure、single-text、multi-api、generated-picture、picture-controls、recommended-picture、retirement-ownership 等）。升主線後，main 要改跑現在給 `next/0.5` 用的 0.5 檢查（`feedback` → `feedback-integrity.yml`、`research-050` → `beta050-validate.yml`）。
- **過時的 workflow：** 26 個 workflow 裡有不少是 0.4 發版專用，可以刪除或歸檔：`release-beta043r2-ota.yml`、`release-beta043r3-ota.yml`、`release-beta044-test1-ota.yml`、`release-beta045-ota.yml`、`publish-045-fix1.yml`、`beta04-*.yml`、`beta042-validate.yml`、`native-bridge-045.yml`、`compat-post-dev2.yml`。
- **PR 規則提案：** PR 直接以 `main` 為目標，最多堆疊一層。多層堆疊的 PR 永遠不會觸發 CI。
- **要討論：** 完整門禁需要 Windows 環境（mingw clang、ReShade、FFX SDK、GPU），托管 CI 大概只能跑其中一部分。CI 實際該覆蓋哪些，需要先講清楚。

### 4.5 #308（guide/MV 換算校驗）

**它改了什麼（白話）：**
1. **兩道防呆檢查。** 這是唯一會改變執行行為的部分。
   - NR 工作尺寸或遊戲渲染尺寸是 0 時，在讀取資源前就拒絕。
   - 遊戲給的運動向量（MV）縮放值，乘上「NR 尺寸 / 渲染尺寸」後如果變成無限大、NaN 或 0，就拒絕，不把壞數字送給 NVIDIA 的 NR。
   - 被拒絕時：那一幀保留遊戲原畫面（不做 NR），旁路原因記為 `guide-contract-invalid`，日誌前 3 次及之後每 600 次提示按 F8。下一個正常幀會自動恢復。
2. F7 深度採集多記一些尺寸資訊（渲染尺寸、NR 尺寸、MV 來源尺寸、換算倍率），只是診斷資料。
3. 選單新增上述旁路原因的中英文說明。
4. 測試、源碼 pin、構建腳本（新增驗證步驟）和文件。

Shader、畫質算法、模型都沒有動。那兩種異常情況是作者自己構造的極端反例，**沒有真實遊戲因此出錯的回報**，也沒有證據說它修好了黑點或閃爍。

**狀態：**
- 不在已實測的 UI1 包裡。
- 沒有 Windows 雙語完整構建。
- 已在 Linux 驗證：試合併到 `21db404` 沒有衝突；64 個 Python 測試結果與合併前相同（54 個通過，10 個因 Linux 環境缺依賴而失敗，三棵樹一致）；#308 的 guide-domain、guide-history 驗證，以及 #307 的 UI（605 項）和 math（115,423 項）驗證，在合併後的樹上用 clang++ 都通過。

**提案：** 改以 #300 分支為目標（升主線後改成 `main`）。由 Windows 端做完整構建和打包，維護者快速進遊戲確認 NR 照常生效、F8 裡沒有新的 `guide-contract-invalid`，之後再合併。

### 4.6 分支清理（177 個，完整清單見附錄 A）

| 類別 | 數量 | 提案 |
|---|---|---|
| A0 已完整包含在 0.5.1 裡 | 84 | 直接刪除。**例外：** `codex/d21-unified-next-20260920`（#300）要等升主線後才刪，`codex/051-guest-comparison-20260925`（#308 的 base）要等 #308 改目標後才刪。`archive/*`、`release/0.4`、`next/0.5` 先轉成同名 tag 再刪 |
| A1 Manager / 安裝器 | 18 | 維護者已決定清理。刪除前先確認新 Manager 倉庫已有這些歷史；沒有的話先打 `archive/manager/<分支名>` tag |
| A2 0.3.x / 0.4.x / corefix.1–4 / 047beta | 36 | 過時。先打 archive tag 再刪 |
| A3 早期 0.5 實驗（9/5–9/11） | 21 | 過時（只有 0–47% 被採用，看起來做法已被放棄或重寫）。先打 archive tag 再刪 |
| A4 近期 0.5 實驗（9/12–9/22） | 17 | 其中 11 個只改文件或 `experiments/`、≥84% 已被吸收，或只有極少量程式碼（`d21-quality-reference`，11 行），打 tag 後刪除。**另外 6 個有 0.5.1 沒有的程式碼，要逐一決定：** `codex/d16-rr-residual-depth-20260913`（#258，493 行）、`codex/d16-deferred-dlss-20260913`（#259，208 行）、`codex/d16-edit-258-259-runtime-v3-20260913`（#265）、`codex/d22-core-entry-before-create-20260917`（341 行）、`codex/d21-amdof-quality-research-20260915`（160 行）、`codex/d21-ui-energy-gate-v2-20260915`（約一半沒被採用）。這些都基於 9/13–9/17 的舊程式碼，就算還要，也建議在 0.5.1 上重做 |
| A5 進行中 | 1 | #308，見 4.5 |

- **tag 命名提案：** `archive/branch/<原分支名>`。tag 會讓 commit 永久可達，刪分支後隨時找得回來。
- **刪除前必做：** 清點本機 Windows 上的 worktree 有沒有未推送的提交（見 5.1）。
- 本 session 的分支 `claude/funny-goldberg-9xjrco` 不在統計內，用完可以刪。

### 4.7 PR

- **關閉 #250、#256、#258、#259、#265。** 關閉時留言註明 archive tag 名稱。#256 有 96% 已在 0.5.1；#250 的改動全在 `experiments/fullres_guides/`（1,855 行），沒有接入主程式；#258、#259、#265 見 A4。
- **#300：** 升主線時合併（4.3）。
- **#308：** 見 4.5。

### 4.8 tag 與 Release

- **28 個 tag 全部保留。** 它們是歷史版本的錨點。
- **Manager 的 3 個 tag**（`manager-v0.4.5`、`manager-0.4.6-hotfix.7-dev.1`、`install-assistant-beta0.1`）和 2 個 Manager Release 還在這個倉庫。要保留、改描述指向新倉庫，還是搬走？
- **2 個 draft Release**（`rog-local-a2a3380-20260908`、`beta0.4.6-hotfix.1`）：刪除還是發佈？
- **0.5.1 系列沒有 tag。** 提案按實際身份打 tag：`v0.5.1-beta` → `3e94329b`（unified10 打包 HEAD，構建源 `d36dc771`）、`v0.5.1-beta-inbox1` → `fe245aed`、`v0.5.1-beta-ui1` → `21db404d`（構建源 `31cddd07`）。

### 4.9 Issue（97 個 open）

- **約 39 個標題標著 0.4.x 或更早：** #7、#64、#65、#68、#82、#93、#98、#99、#101、#110、#121、#122、#123、#124、#126、#127、#139、#140、#148、#152、#153、#159、#176、#177、#187、#188、#202、#205、#223、#224、#225、#229、#233、#247、#248、#249、#254、#269、#271。大多是遊戲相容性問題，**不能因為版本舊就直接關**。提案：加 `needs-retest-0.5.1` 標籤並請回報者複測，多少天沒回應就關閉，天數待定。
- **跟 Manager 有關的**（#271、#224，#268 可能也是）：轉移到 Manager 倉庫（同一擁有者的倉庫之間可以直接 transfer issue）。
- **9/4–9/9 的 RFC 和總綱類 issue**（#4、#7、#8、#9、#10、#11、#12、#14、#15、#94、#95、#96、#105、#106、#107、#108、#113 等）：檢查哪些已完成或已被 0.5.1 取代。
- **`fix-in-pr` / `partial-fix` 標籤**指向的 PR 可能已合併或廢棄，需要刷新。
- **#294**（D21 統一後繼協調）：升主線後關閉，或改成 0.5.x 追蹤 issue。

### 4.10 bug-inbox

- `main` 上有 412 個檔案、約 29 MiB，其中包含二進位檔：`bug-inbox/issue-224/` 的兩個 `.addon64` 橋接器，以及多個 `feedback.zip`。這違反倉庫自己的 `docs/REPOSITORY_AUDIT.md`（二進位掃描必須零命中）。
- 裡面是玩家日誌。倉庫是私有的，所以沒有外洩；但如果以後要公開，歷史裡都還在，屆時必須按 AGENTS.md 用 allowlist 匯出。
- 升主線會把 main 上這 116 個 bug-inbox 檔案帶進 0.5.1 的 main。
- **要決定：** bug-inbox 繼續放在 git 裡（agent 讀取方便），還是改放 git 以外的私有存儲？至少要停止再提交二進位檔。

## 5. 你可能沒印象、需要一起確認的問題

1. **本機未推送的工作：** Windows 上的 worktree 有沒有未推送的提交？刪遠端分支前必須先清點，否則可能被 agent 重新推上來，或者工作被遺忘。
2. **CI 帳單：** 9/9 起沒有一次 CI 真正跑過。要修好，還是正式改成「只靠本地門禁」？
3. **分支保護 / rulesets：** `main` 有沒有設保護？本 session 查不到。升主線和刪分支前要先確認。
4. **簽名私鑰：** 倉庫裡只有 `signing/release-public-key.json`。私鑰在哪台機器？有備份嗎？升主線後的發版流程還用它嗎？
5. **OTA 包的正本：** 0.5.1 Beta、Inbox1、UI1 的 ZIP 只存在本機，正本在哪？要不要備份（例如當作 draft Release 的附件）？
6. **玩家從哪裡下載：** 倉庫是私有的，玩家看不到這裡的 Release。下載通道是 Manager 還是別的地方？這會影響要不要在這裡建 0.5.1 Release。
7. **版本命名：** 0.5.1-Beta-Reconstruction1-UI1 很長。成為主線後，下一版叫什麼（0.5.2？）？
8. **`experiments/` 和 `docs/research/archive/`：** 保留還是歸檔？
9. **過測列表：** `docs/compat/COMPAT-PASS-MATRIX.md` 只在 main 上。升主線時帶過去後，還要繼續維護嗎？
10. **RR 編輯層和 Deferred 方向（#258、#259）還要嗎？** #300 說私有 SR/RR 還沒接通。
11. **多個 agent 同時寫這個倉庫：** 分支前綴有 `codex/`、`cursor/`、`claude/` 等，要不要統一命名規則和「誰負責什麼」？
12. **0.4 凍結點：** tag 打在 main 的 `dae86e9`（建議），不是 `release/0.4`，確認嗎？
13. **Manager 要同步的「最新 Core」是哪一份？** 見第 6 節。#308 合入後 Core 會再變一次。

## 6. 給 Manager session 的 Core 交接資訊

目前遊戲實測通過的 Core 是 **0.5.1-Beta-Reconstruction1-UI1**，身份資訊摘自 #307 的描述，**Manager session 必須從實際包裡的檔案重新核對哈希**：

- 版本：`beta0.5-dline21-unified10-reconstruction1-ui1`
- 構建源 `31cddd07`，包裝和文件 HEAD `21db404d`
- 中文 Core SHA256：`213338900bfcbada149dc89e7fcea0b12f8843f0252a4cb81ce16e847d3b607d`
- 英文 Core SHA256：`54f998be0c7d1c293fcb9a6b231f4ca917a826e6e9c40afa699903534cb97929`
- nrchain SHA256：`1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2`
- `DLSS5-0.5.1-Beta-Reconstruction1-UI1-zh-CN-OTA.zip`：`7b9dcb58260edc18e2dfd55111213cfb589d2bf958127bedde8e869cfc7f4e14`
- `DLSS5-0.5.1-Beta-Reconstruction1-UI1-en-OTA.zip`：`6f37b47bd1d3be8d48094f0624362ba4fcce5824841ba5f52ddd62cd3dec505b`
- 每個包含一個語言的 Core、配套 nrchain、可選的人臉資源和許可證、可選的 DX11 carrier（D3D12 遊戲不用裝 DX11 carrier）。包裡不含 INI、ReShade 或 NVIDIA 模型。
- 操作與驗證說明：`docs/interop/051_RECONSTRUCTION1_20260925.md`（在 #300 分支上）。
- 新增的兩項功能「低分辨率細節增強」（均衡/精細）和「去除暗噪」都**預設關閉**，INI 鍵名與舊版相容。

## 7. 分工草案（待討論後確定）

| 角色 | 能做 | 不能做 | 建議負責 |
|---|---|---|---|
| 雲端 Claude Code session（本 session，Linux） | git 和 GitHub 操作（PR 合併與關閉、tag、刪分支、issue 標籤與轉移）、改寫文件、升 main 的衝突處理、Linux 上能跑的 Python/clang 測試 | Windows 雙語完整構建、WARP 或 5090 GPU 測試、打包 OTA、遊戲 | 4.1（可協助）、4.2、4.3、4.6、4.7、4.8、4.9 |
| Windows 本地 agent | 完整構建、打包、WARP/GPU 測試、清點本機 worktree | — | #308 的構建和打包、升主線後在 main 跑完整門禁、5.1 |
| Manager session | Manager 倉庫 | — | 確認 Manager 分支歷史已在新倉庫、接收轉移過去的 Manager issue |
| 維護者 | 做決定、遊戲測試、帳單 | — | 4.4 的帳單、第 5 節的問題、#308 的遊戲快速確認 |

## 附錄 A：分支清單

量測方法：以 `21db404`（與目前 #300 分支 HEAD 同一棵樹）為基準。「新增程式碼已在 0.5.1 的比例」是指：分支相對 merge base 新增的、長度 ≥20 字元的程式碼行（限 `src`、`scripts`、`tests`、`config`、`.github` 等目錄），在 0.5.1 同名檔案中仍然存在的比例。這是判斷過時程度的**粗略指標**，不代表語義上等價。

### A0. 已完整包含在 0.5.1 裡（84 個）

刪除不會丟失任何提交。`archive/*`、`release/0.4`、`next/0.5` 建議先轉成同名 tag 再刪。

| 最後提交 | 分支 |
|---|---|
| 2026-09-26 | `codex/051-guest-comparison-20260925` |
| 2026-09-24 | `codex/051-inbox-recovery-20260924` |
| 2026-09-23 | `codex/d21-unified-next-20260920` |
| 2026-09-23 | `codex/unified6-temporal-memory-opt-20260921` |
| 2026-09-22 | `codex/d21-unified8-consolidated-20260922` |
| 2026-09-20 | `codex/d21-colour4-edge-recovery-20260919` |
| 2026-09-20 | `codex/nr-aux-memory-retirement-20260920` |
| 2026-09-20 | `codex/nr-aux-history-reuse-20260919` |
| 2026-09-19 | `codex/d21-recording-traffic-perf10-20260919` |
| 2026-09-19 | `codex/d21-observer-pending-perf9-20260919` |
| 2026-09-19 | `codex/d21-paired-lifetime-perf8-20260919` |
| 2026-09-19 | `codex/d21-colour4-stage-isolation-20260919` |
| 2026-09-18 | `codex/d21-tracking-batch-perf7-20260918` |
| 2026-09-18 | `codex/d21-slot-lookup-perf6-20260918` |
| 2026-09-18 | `codex/colour-source-validation-gates-20260918` |
| 2026-09-18 | `codex/d21-copy-range-perf5-20260918` |
| 2026-09-18 | `codex/d21-range-saturation-perf4-20260918` |
| 2026-09-18 | `codex/d21-colour-texture-lab-20260917` |
| 2026-09-18 | `codex/d21-descriptor-lineage-perf3-20260918` |
| 2026-09-17 | `cursor/d21-lockperf2-win-044-gate-20260917` |
| 2026-09-17 | `codex/d21-command-state-hotpath-20260917` |
| 2026-09-17 | `codex/d21-shadow-lockperf-buildfix-20260916` |
| 2026-09-16 | `codex/d21-277-command-state-lockperf1-20260916` |
| 2026-09-16 | `codex/d21-uniform-chain-update-20260916` |
| 2026-09-14 | `codex/d21-fg-ui-energy-gate-20260915` |
| 2026-09-14 | `codex/d21-saved-route-fg-boundary-20260914` |
| 2026-09-14 | `next/0.5` |
| 2026-09-13 | `codex/d20-fg-stability-perf-20260913` |
| 2026-09-13 | `codex/d19-auto-present-stable-perf-20260913` |
| 2026-09-13 | `codex/d18-native-guide-snapshot-20260913` |
| 2026-09-13 | `codex/d17-late-present-receipts-20260913` |
| 2026-09-13 | `codex/d16-temporal-edit-v2-20260913` |
| 2026-09-13 | `codex/dx12-compat-multipass-20260912` |
| 2026-09-12 | `codex/compatibility-post-present-20260912` |
| 2026-09-12 | `codex/three-point-processing-ui-20260912` |
| 2026-09-12 | `codex/sf985-route-receipts-20260912` |
| 2026-09-11 | `codex/050-corefix8-integration-20260911` |
| 2026-09-11 | `codex/corefix8-copy-observation-20260911` |
| 2026-09-11 | `codex/corefix7-followup-20260911` |
| 2026-09-11 | `codex/corefix6-followup-20260911` |
| 2026-09-11 | `codex/corefix5-followup-20260910` |
| 2026-09-10 | `codex/050-dline-independent-controls-20260910` |
| 2026-09-09 | `codex/050-dline-face-diagnostic-20260909` |
| 2026-09-09 | `codex/050-dline-layer-probe-20260909` |
| 2026-09-09 | `codex/050-dline-directional-20260909` |
| 2026-09-09 | `codex/050-magpie-research-20260909` |
| 2026-09-09 | `codex/050-dline-047-merge-20260909` |
| 2026-09-09 | `codex/dev16-route-blocks-lighting-20260909` |
| 2026-09-09 | `codex/050-dline-feedback-20260909` |
| 2026-09-09 | `codex/ds1-first-recording-20260909` |
| 2026-09-09 | `codex/050-dline-post-20260909` |
| 2026-09-09 | `codex/dev14-nte-menu-depth-20260909` |
| 2026-09-09 | `codex/dev13-picture-defaults-20260909` |
| 2026-09-09 | `codex/multigame-compat-routing-20260908` |
| 2026-09-08 | `codex/dev11-style-blocks-20260908` |
| 2026-09-08 | `codex/dev10-face-capture-20260908` |
| 2026-09-08 | `codex/dev9-owner-feedback-20260908` |
| 2026-09-08 | `codex/050-safety-config-20260908-cloud` |
| 2026-09-08 | `codex/050-color-contract-20260908` |
| 2026-09-08 | `codex/050-catchup-20260908` |
| 2026-09-08 | `codex/ui-rr-refresh-20260908` |
| 2026-09-08 | `codex/priority-merge-rehearsal-20260908` |
| 2026-09-08 | `codex/reshade-carrier-exclusion-20260908` |
| 2026-09-08 | `codex/carrier-lifecycle-153-20260907` |
| 2026-09-08 | `cursor/dx11-matching-carrier-e1c3` |
| 2026-09-07 | `fix/rr-post-boundary-diagnostics-20260907` |
| 2026-09-07 | `fix/045-null-guard-reasons` |
| 2026-09-07 | `fix/045-lighting-edge-weights` |
| 2026-09-07 | `cursor/runtime-load-126-e1c3` |
| 2026-09-07 | `cursor/nullguard-finite-blend-e1c3` |
| 2026-09-07 | `fix/priority-finite-blend-windows-20260907` |
| 2026-09-07 | `codex/open-source-readiness-ci-20260907` |
| 2026-09-06 | `fix/050-maintenance-stability` |
| 2026-09-06 | `feature/050-layered-composite` |
| 2026-09-06 | `codex/050-high-strength-guard` |
| 2026-09-06 | `release/0.4` |
| 2026-09-06 | `archive/mainline-normalization-20260906` |
| 2026-09-06 | `archive/normalized-main-49dd9d4` |
| 2026-09-06 | `archive/beta042-final` |
| 2026-09-06 | `archive/pre-normalization-marker` |
| 2026-09-06 | `archive/pre-mainline-beta043r3` |
| 2026-09-06 | `archive/r3-release-line` |
| 2026-09-06 | `codex/beta0.5` |
| 2026-09-02 | `tmp/cross-repo-blob-test` |

### A1. Manager / 安裝器（已遷到獨立倉庫）（18 個）

| 最後提交 | 分支 | 新增程式碼已在 0.5.1 的比例 | 新增程式碼行數 |
|---|---|---|---|
| 2026-09-12 | `codex/manager-049-beta1-20260912` | 0% | 59781 |
| 2026-09-12 | `codex/manager-049-mgr27-28-20260912` | 0% | 59781 |
| 2026-09-11 | `codex/manager-048-compatibility-ux-20260911` | 0% | 57604 |
| 2026-09-11 | `codex/manager-048-hoyo-unified-20260911` | 0% | 56145 |
| 2026-09-10 | `codex/manager-sm75-sm86-235-cloud-20260910` | 0% | 24372 |
| 2026-09-09 | `codex/manager-maintenance-20260908` | 0% | 23696 |
| 2026-09-08 | `codex/manager-recovery-20260908-cloud` | 0% | 10769 |
| 2026-09-08 | `codex/xiaofeng-sr-fg-20260908` | 0% | 10629 |
| 2026-09-08 | `codex/xiaofeng-bridge-ui-20260908` | 0% | 7090 |
| 2026-09-08 | `codex/manager-046-hotfix1-20260908` | 0% | 7042 |
| 2026-09-08 | `product/xiaofeng-manager-foundation` | 0% | 6791 |
| 2026-09-08 | `codex/manager-api-routing-verify-20260908` | 0% | 6130 |
| 2026-09-06 | `codex/manager-sr-model-policy` | 0% | 4451 |
| 2026-09-05 | `codex/manager-official-link-ux` | 0% | 991 |
| 2026-09-05 | `codex/install-assistant` | 0% | 232 |
| 2026-09-05 | `product/glass-quickbar-v0.2` | 0% | 991 |
| 2026-09-05 | `ui/quickbar-control-v1` | 0% | 68 |
| 2026-09-05 | `product/china-manager-prototype` | 0% | 15 |

### A2. 0.3.x / 0.4.x / corefix.1–4 / 047beta 舊版本線（36 個）

| 最後提交 | 分支 | 新增程式碼已在 0.5.1 的比例 | 新增程式碼行數 |
|---|---|---|---|
| 2026-09-10 | `codex/corefix4-observation-20260910` | 96% | 17915 |
| 2026-09-10 | `codex/corefix3-followup-20260910` | 95% | 16032 |
| 2026-09-10 | `codex/rr-fence-handoff-234-cloud-20260910` | 95% | 13482 |
| 2026-09-10 | `codex/corefix2-integration-20260910` | 96% | 13322 |
| 2026-09-10 | `codex/core-contract-fixes-047beta-20260909` | 97% | 10709 |
| 2026-09-09 | `codex/post-output-boundary-047beta-20260909` | 88% | 1202 |
| 2026-09-09 | `codex/post-output-evidence-047beta-20260909` | 89% | 994 |
| 2026-09-09 | `codex/submitted-retirement-047beta-20260909` | 90% | 845 |
| 2026-09-09 | `codex/queue-activity-047beta-20260909` | 88% | 507 |
| 2026-09-09 | `codex/ds1-queue-affinity-047beta-20260909` | 89% | 400 |
| 2026-09-09 | `codex/core-047beta-name-20260909` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-08 | `codex/registry-admission-luna-20260908` | 38% | 9400 |
| 2026-09-08 | `codex/bl3-owner-test-20260908` | 38% | 9379 |
| 2026-09-08 | `codex/carrier-production-verify-20260908` | 38% | 9326 |
| 2026-09-08 | `codex/priority-validation-stack-20260907` | 96% | 861 |
| 2026-09-07 | `cursor/diag-151-lighting-edge-93b9` | 0% | 2 |
| 2026-09-07 | `cursor/diag-149-150-nullguard-rr-93b9` | 93% | 210 |
| 2026-09-07 | `codex/045-feeder-recovery` | 75% | 9056 |
| 2026-09-07 | `release/0.4.5-test` | 89% | 1707 |
| 2026-09-07 | `codex/dx11-045-preparation` | 87% | 881 |
| 2026-09-07 | `fix/f8-two-file-delivery` | 71% | 1085 |
| 2026-09-07 | `prep/045-followup-sweep` | 70% | 400 |
| 2026-09-06 | `prep/045-followup-audit` | 84% | 332 |
| 2026-09-06 | `cursor/fix-118-dx11-waiting-feature1-e3f9` | 1% | 552 |
| 2026-09-06 | `prep/045-integration` | 45% | 1123 |
| 2026-09-06 | `prep/sep06-review-fixes` | 42% | 434 |
| 2026-09-06 | `codex/dx11-r3-render-control` | 78% | 1025 |
| 2026-09-05 | `codex/043r2-finalize-preparation` | 0% | 419 |
| 2026-09-05 | `codex/043-source-preparation` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-05 | `codex/beta0.4.2-native-bridge-exp1` | 86% | 588 |
| 2026-09-05 | `codex/beta042-dx11-native-carrier` | 91% | 560 |
| 2026-09-05 | `codex/beta0.3.8-helldivers2` | 63% | 2997 |
| 2026-09-05 | `codex/beta0.3.8` | 85% | 1802 |
| 2026-09-05 | `codex/beta0.3.7-helldivers2-r2` | 64% | 2756 |
| 2026-09-05 | `codex/beta0.3.7` | 83% | 1627 |
| 2026-09-05 | `codex/beta0.3.7-helldivers2` | 78% | 1133 |

### A3. 早期 0.5 實驗（9/5–9/11）（21 個）

| 最後提交 | 分支 | 新增程式碼已在 0.5.1 的比例 | 新增程式碼行數 |
|---|---|---|---|
| 2026-09-09 | `codex/050-dline-beta-20260909` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-09 | `codex/050-manual-dual-nr-20260910-cloud` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-09 | `codex/nvfp4-ffn-optimization-lab` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-08 | `codex/owner-decisions-plan-20260908` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-08 | `codex/fg-sample-20260908` | 44% | 8151 |
| 2026-09-08 | `codex/work-percent-config-20260908` | 44% | 8238 |
| 2026-09-08 | `codex/nr-size-admission-20260908` | 42% | 8473 |
| 2026-09-08 | `codex/carrier-two-stage-prototype-20260908` | 46% | 7685 |
| 2026-09-08 | `codex/synthetic-mask-20260908` | 43% | 8223 |
| 2026-09-08 | `codex/cascade-consumer-review-20260908` | 43% | 8285 |
| 2026-09-08 | `codex/core-sr-combined-20260908` | 47% | 7502 |
| 2026-09-08 | `codex/direct-sr-provider-20260908` | 0% | 598 |
| 2026-09-08 | `codex/provider-transport-20260908` | 15% | 2603 |
| 2026-09-07 | `codex/mc1-feeder-20260907` | 86% | 4662 |
| 2026-09-06 | `codex/050-stability-preparation` | 26% | 399 |
| 2026-09-06 | `codex/050-layered-source-preparation` | 23% | 593 |
| 2026-09-06 | `docs/r3-followup-test-matrix` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-06 | `codex/050-high-strength-preparation` | 35% | 358 |
| 2026-09-05 | `codex/research-unified-controls-v1` | 0% | 338 |
| 2026-09-05 | `codex/research-fg-mfg-control-v1` | 0% | 185 |
| 2026-09-05 | `codex/research-temporal-provider-v1` | 0% | 152 |

### A4. 近期 0.5 實驗（9/12–9/22）（17 個）

| 最後提交 | 分支 | 新增程式碼已在 0.5.1 的比例 | 新增程式碼行數 |
|---|---|---|---|
| 2026-09-22 | `codex/d21-post-residual-sr-20260921` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-20 | `codex/d21-quality-reference-20260920` | 45% | 11 |
| 2026-09-19 | `codex/perf10-integration-handoff-20260919` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-17 | `codex/d22-core-entry-before-create-20260917` | 2% | 341 |
| 2026-09-16 | `codex/d21-amdof-quality-research-20260915` | 0% | 160 |
| 2026-09-15 | `codex/d21-ui-energy-gate-v2-20260915` | 57% | 377 |
| 2026-09-13 | `codex/d16-edit-258-259-runtime-v3-20260913` | 0% | 31 |
| 2026-09-12 | `cursor/typeless-current-gen-native-view-cc6f` | 84% | 184 |
| 2026-09-12 | `cursor/upstream-nr-reuse-inventory-15f7` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-13 | `codex/d16-rr-residual-depth-20260913` | 0% | 493 |
| 2026-09-12 | `cursor/compat-corpus-placement-rr-b094` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-12 | `cursor/compat-corpus-format-8701` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-12 | `cursor/d16-compat-next-slices-8916` | 量測範圍外（只改文件或 experiments/） | 0 |
| 2026-09-13 | `codex/d16-deferred-dlss-20260913` | 0% | 208 |
| 2026-09-13 | `codex/d16-present-route-recovery-20260913` | 99% | 172 |
| 2026-09-13 | `codex/provider-of-integration-20260912` | 96% | 63173 |
| 2026-09-12 | `codex/050-fullres-optical-inputs-20260912` | 量測範圍外（只改文件或 experiments/） | 0 |

### A5. 進行中（1 個）

| 最後提交 | 分支 | 新增程式碼已在 0.5.1 的比例 | 新增程式碼行數 |
|---|---|---|---|
| 2026-09-25 | `codex/051-guide-domain-audit-20260925` | 1% | 245 |
