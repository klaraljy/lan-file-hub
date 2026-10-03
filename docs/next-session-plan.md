# 下一轮计划（2026-10-03）

> **状态：本文件里的 ①～⑦ 已全部完成（2026-10-03）**，实测记录见各节与对应提交。> 提交：`a88ee98`/`a4e054f`（网络跟随）、`6a1a633`（命令行提管理员）、`dab63b5`（pwsh 7 启动器）、
> `2fa32d1`（左上角 logo）、`1c8c69e`+`7eed4ca`（邀请制）、`9e8ccc6`（手机 App 连接器）。
> **仍未验证的只有"真机"那一环**：用户手机装 APK、手机热点下的扫码、5 GB 大文件 —— 需要用户实测。
>
> **2026-10-03 追加的两项与遗留**：
> - 「最近文件」已从占位页做成真列表（`GET /api/v1/recent-files`，提交 `0cede07`）。
>   它**有真实 HTTP 验证**（200 / `total=1` / 根目录 `folder_name` 为空 / 未登录 401）
>   与前端单测 137/137，但 ⛔ **还没有自动化集成测试**（该接口涉及跨目录 SQL 与排序，
>   值得补一个 `integration/recent_files_test.cpp`：排序正确、排除回收站、排除别人的文件、
>   分页边界）。**下次动这块之前先补上这个测试。**
> - 手机端「分享给我的 / 回收站 / 最近文件」已改卡片式；管理员表格改为内部横滑。
>   回归钉在 `frontend/tests/e2e/mobile.spec.ts`（底栏在可视区内 + 无横向溢出）。
> - ⛔ 新增最高指令（`AGENTS.md` §16）：**同一时刻只允许一个后台任务**。

---

## 10. ✅ 已做：网页「我的」里的「配套手机应用下载」入口（2026-10-03，用户要求"改完就更新的"）

**用户口径**：网页「我的」里要有一个**永远指向最新 APK** 的入口 —— 重新构建 App 之后，
刷新网页，卡片上的**大小/时间就是新的**；不许手工拷文件、不许重启后端。

**三段分工（这就是"改完就更新"的机制）**

| 谁 | 做什么 |
|---|---|
| `scripts\build-android.ps1` | 出包后写侧车 `android\lan-file-hub-share.apk.json`（版本/大小/构建时间/SHA-256），并**自动同步**到 `portable\app\web\downloads\lan-file-hub.apk`（旧的手工副本 `web\lan-file-hub.apk` 会被清理）。新增开关：`-PublishOnly`（只同步不构建）、`-SkipPublish` |
| 后端 | `GET /api/v1/app/release`（版本/大小/时间/SHA-256/下载地址）与 `GET /api/v1/app/download`（octet-stream + `Content-Disposition`）。**每次请求现看磁盘**，文件没换才用缓存；位置见新配置 `APP_APK_PATH`（留空 = `<WEB_ROOT>/downloads/lan-file-hub.apk`）。这两个端点**不需要登录**（理由写在 `app_release_controller.h`：装 App 不该先登录，APK 不含用户数据） |
| 前端 | 「我的」页新增卡片（`pages/MyPage.tsx` 的 `PhoneAppCard`）：`版本 · 大小 · 构建时间` + 「下载安装」原生下载按钮；电脑上多一个「用手机扫码安装」（复用 `ResponsiveSheet` + `ShareLinkBox`，二维码里是局域网地址）。地址走 `publicUrl.ts` 的 `buildAppDownloadUrl`（与分享链接同一套规则） |

**测试**：后端单元 +7（`AppReleaseTest` 6 条：缺文件/侧车优先但**大小以真实文件为准**/无侧车退化/
侧车坏掉不崩/**换包后立刻给新值**/空路径；`LoadConfigTest` 1 条：`APP_APK_PATH` 默认跟随 `WEB_ROOT`）。
前端纯函数单测 +9（大小、时间、拼行、兜底、局域网地址替换）。

**这一步之后还剩的活**（按优先级）：
1. ~~「最近文件」接口的**集成测试**~~ ✅ 已补（2026-10-03，见 §11）；
2. 单次上传路径与"回收站恢复同名"的命名策略统一（回收站那条**有意保持拒绝**）；
3. 真机：用户手机装新 APK、手机热点下扫码、5 GB 大文件（**只有用户能验**）。

---

## 12. ✅ 已做：用户 2026-10-03 批准的 7 项（一次会话做完，全部本地提交）

用户原话：「**1–7 都可以弄**」（在他去休息前），逐项落地与验证：

| # | 事项 | 关键提交 | 怎么验的 |
|---|---|---|---|
| ① | App **自动找电脑** + 当前网络提示 | `a9dd818` | `ServerDiscoveryTest`/`NetworkStateTest`（电脑 JVM）；真机未验 |
| ② | **登录记忆**（记住我 → localStorage + 30 天令牌） | `a9dd818` | 后端集成 `RememberMeControlsTokenLifetime`（两个方向）；前端 `tokenStore` 6 条；真机 HTTP：勾选 2592000 秒 / 不勾 3600 秒 |
| ③ | App 内浏览器顶栏**并成一行** | `a9dd818` | 出包装机前只能看布局与单测；真机观感待用户确认 |
| ④ | **搜索 + 图片缩略图/预览** | `4bff962` | 集成 `search_test.cpp` 6 条（含 `%` 字面量）；前端 6 条；真机 HTTP 正向/负向都跑过 |
| ⑤ | **相册备份（仅 WiFi）** | `8ec1c00` | `BackupPlannerTest` 6 条；服务器侧那串调用用 `verify-backup-api.ps1` 真跑（建文件夹 0→1、上传、同名 `(1)`）；真机未验 |
| ⑥ | **分享链接加密码 / 有效期 / 下载次数** | `8b457b2` | 集成新增 3 条（未解锁不泄露元数据、密码错 401、越权票据被拒）；真机 HTTP：未解锁 name 为空、错密码 401、直接下载 401、正确密码拿 15 分钟票据、下载上限 3 次后 404 |
| ⑦ | **开机自启 + 一键备份/恢复 + 回收站保留期与用量提醒 + PC 端单向上传** | `e320e80`（脚本）/ 本文件（前端） | 备份+恢复真跑（计数与清单一致，24.5 秒，服务 200）；自动上传真跑三轮（2 → 0 → 1）；用量/保留期前端 6 条单测；开机自启只做开关、**默认关闭** |

**顺带修掉一个真 bug**（提交 `e09c891`）：`/files/{id}/download` 只带会话令牌时返回 **403
「下载票据与文件不匹配」**——Drogon 的 `Attributes::get<T>()` 在键不存在时不抛异常（解引用 `end()`）。
现在先 `find()` 再 `get()`；回归口子是 `scripts\verify-download-paths.ps1`（四条 HTTP 路径）。

**仍然只在真机上才能验的**：App 装到手机（自动找电脑、新顶栏观感、相册权限与备份实际速度）、
手机热点下的扫码、5 GB 大文件、iOS 接收。

**待用户拍板**：要不要真的**打开开机自启**（脚本已备好，默认关闭，我没替他打开）。

---

## 11. ✅ 已做：「最近文件」集成测试 + 同名自动改名的后端钉子（2026-10-03）

**新增 `backend/tests/integration/recent_files_test.cpp`（5 条）**，把开头那条遗留补上：

| 用例 | 钉住什么 |
|---|---|
| `ListsOwnFilesNewestFirstAcrossFolders` | 跨文件夹、按"最后改动时间"倒序；**别人的文件一条都不出现**；total 只算自己的 |
| `TellsTheUserWhichFolderEachFileIsIn` | `folder_id`/`folder_name` 带出来（用户得知道去哪儿找），根目录用根目录名 |
| `HidesTrashedFilesAndShrinksTheTotal` | 软删除后不再出现，**total 一起减**（不能过滤了列表却留着旧总数） |
| `IsEmptyForANewAccount` | 新账号空列表、total=0 |
| `PaginatesWithoutGapsOrDuplicates` | page_size=2 翻三页拼起来**不重不漏**、每页 total 都是全量、越界页空列表、page_size=1 下界可用 |

> 排序用 SQL 摆布 `updated_at`（连续上传的几个文件可能落在同一微秒，否则断言会随机成败）；
> 用例注释里写了原因。

**顺带修掉两条真问题**（跑全量集成测试时暴露的）：

1. ⛔ **`DedupQuotaTest.QuotaIsNotAffectedByFailedUpload` 是红的**（2026-10-03 全量集成 157 条里唯一失败）：
   它断言"同名上传被拒"，而同日早些时候同名已经改成**自动加 (1)**（用户要求）→ 那条路不再失败。
   已改成用**超过单文件上限**这条确定失败的路径，验的性质不变（失败的上传不改变已用容量）。
2. ⛔ **同名自动加 (1) 这条行为此前没有任何后端测试**（前端/手工验过，集成里没有）。
   新增 `UploadFlowTest.SameNameUploadGetsASuffixInsteadOfBeingRejected`：
   同一文件名连传三次 → 会话名与落库名依次是 `同名.txt` / `同名 (1).txt` / `同名 (2).txt`，
   三份并存、互不覆盖。

**跑法（本机没有 Docker 时的替代）**：测试库建在**便携包那个 PostgreSQL** 上（独立库
`lan_file_hub_test`，与用户的 `lanfilehub` 库互不影响），跑完确认用户库里 3 个账号与文件都还在：

```powershell
$env:DATABASE_URL = '<便携库连接串，库名换成 lan_file_hub_test>'
$env:REDIS_URL    = 'redis://127.0.0.1:56379'      # 便携 Redis
$env:LAN_FILE_HUB_REQUIRE_DB = '1'                 # 数据库缺失必须报失败，不许静默跳过
.\scripts\test.ps1 -Preset debug -Label integration   # 串行，158/158
```

---

## 9. ✅ 已修：同名文件自动加 (1)、(2)…（用户 2026-10-03 实测并提出）

**用户原话**：「我刚刚就试着上传了已更新后的 apk，但是显示上传失败，不能有同名文件，
这个问题还没有解决吗，这么简单的文件命名系统」→ 随后明确要求：
**「你为什么就不能用相同名字的文件加一个 (1) 呢，再多一个变成 (2) 呢」**。

**最终行为（按用户口径，不做弹框）**：同一文件夹里已有同名文件时，**自动排下去**：
`portable-test.txt` → `portable-test (1).txt` → `portable-test (2).txt` …
（后缀插在扩展名之前；名字超长时截断基名；排在 (50) 之后才报错并提示整理目录。）

**实现**（`backend/src/application/upload_service.cpp`）：建上传会话时不再返回
`RESOURCE_CONFLICT`，而是循环找一个没被占用的名字。**判重要同时看两处**：
`files` 表里已存在的文件**和** `upload_sessions` 里还没结束的会话
（`UploadSessionRepository::ExistsPendingName`）——
否则连着建多个会话会都拿到 `(1)`（第一版就踩了这个坑，实测三次都是 `(1)`）。

**实测（2026-10-03，便携实例，同名连建 3 次）**：

```text
HTTP 200 / 200 / 200        （不再报"不能有同名文件"）
库里的会话名： portable-test (1).txt / portable-test (2).txt / portable-test (3).txt   ✅
```

**仍未做（诚实清单）**：单次上传（非分块）那条路径与"回收站恢复时同名"仍是拒绝策略
（后者是有意的：恢复不能悄悄改名，必须让用户决定）；这两处还没按新规则统一。

**用户原话**：「我刚刚就试着上传了已更新后的 apk，但是显示上传失败，不能有同名文件，
这个问题还没有解决吗，这么简单的文件命名系统」。

**现状（缺陷）**：同一文件夹内文件名唯一（`ExistsWithName` → `RESOURCE_CONFLICT 同名文件已存在`），
上传同名文件**直接被拒**。用户的心智模型是"文件管理器"：同名就该**替换**或**保留两个**，
而不是弹一个技术味的错误——按 `AGENTS.md` §14.1，这属于**产品缺陷**。

**要做的行为（三步，界面上说清楚）**：上传时发现同文件夹已有同名文件 → 弹一个确认框：

| 选项 | 行为 | 实现 |
|---|---|---|
| **替换**（推荐，默认） | 把新内容作为**该文件的新版本**上传，旧内容留在版本历史里**可恢复** | 走已有的 `POST /api/v1/files/{file_id}/versions`（前端已具备"上传新版本"能力），不需要新接口 |
| **保留两个** | 新文件自动改名 `名字 (2).ext`（已存在就 `(3)`…） | 前端按当前目录列表算出未占用的名字再上传；仍冲突则继续递增（上限 20 次后报错） |
| 取消 | 什么都不做 | —— |

**要改的地方（预计一次改完）**：

1. `frontend/src/features/upload/`（上传入口所在的 hook）+ 触发上传的页面：
   上传前用**当前目录已加载的列表**判断是否同名（不用新接口），命中就弹上面的框；
2. `frontend/src/services/versionsApi.ts`：确认"上传新版本"这条路能被复用（已有则直接用）；
3. 后端**不改**：替换走版本接口、保留两个走改名后的普通上传，两条路都已在；
   但要在 `docs/frontend.md` 写清"同名三选一"的交互，并在 `docs/guide.md` 补一句大白话说明。
4. **测试**：前端单测覆盖"同名 → 三个选项各自的行为"（替换调用版本接口、保留两个改名、取消不发请求）；
   e2e 可选。

**验收**：上传一个与已有文件同名的文件 →
① 选「替换」→ 列表里仍是同一个名字、内容更新、版本历史里能看到旧版本并可恢复；
② 选「保留两个」→ 出现 `xxx (2).apk`，原文件不动；
③ 选「取消」→ 什么都没发生（无网络请求）。

**当前的临时办法（已告知用户）**：先把文件名改一下（例如加个日期）再上传，或先在列表里删掉旧文件再传。

### 9.1 ✅ 已修：网页目录里的 `.apk` 打不开（2026-10-03 修完并验证）

把新的 APK 放进 `portable\app\web\lan-file-hub.apk`（338.8 KB）后，
`GET /lan-file-hub.apk` 曾返回 **`text/html`（SPA 回退页）而不是 APK** -—
根因：**Drogon 的静态托管只处理它认识的 MIME 类型**，`.apk` 不在其中，请求落到 default handler，
那里一律回退 index.html。后果：用户**没法从自己的服务器把 APK 下到手机**。

**修法（`backend/src/api/web_hosting.cpp`）**：default handler 里先判断"路径像不像带扩展名的文件"，
像就**安全地**映射到 WEB_ROOT 下的真实文件按 `application/octet-stream` 发；否则才回退前端路由。
安全上做了两道：路径规范化后必须落在 WEB_ROOT 之内（防 `../`），响应头仍带 `nosniff`。

**实测（2026-10-03，便携实例）**：

```text
GET /lan-file-hub.apk              → 200  application/octet-stream  338.8 KB   ✅
GET /files（前端路由）              → 200  含 <div id="root">（仍回退 index.html） ✅
GET /../backend/CMakeLists.txt     → 200  0.9 KB 网页，未泄露源码 ✅
GET /logo.png                      → 200  image/png（正常静态资源未受影响） ✅
```

> 用户 2026-10-02 收工前提出的四件事，逐条对应下面四节。**执行顺序：① 启动器 → ② 网页图标 → ③ 手机 App → ④ 收尾。**
> 每件事做完都要给**可验证的证据**（命令输出 / 截图 / 真机结果），跑通再提交；一次提交只放一个逻辑变化。

## 0. 已确认的事实（不用再查）

| 事实 | 值 |
|---|---|
| 本机有新版 PowerShell | **pwsh 7.6.6**：`C:\Program Files\WindowsApps\Microsoft.PowerShell_7.6.6.0_x64__8wekyb3d8bbwe\pwsh.exe`，PATH 里有 `pwsh` 别名；默认线程模型 **STA**（WinForms 托盘可直接用），支持 `-STA` |
| 旧版 | Windows PowerShell **5.1**.22621.3880（保留作兜底） |
| 图标素材 | `F:\夸克发图\下载\9ad235a1d16a27b17bf570ff856b8cd7.jpg`：**1600×1600 正方形**、JPEG 25 KB、绿底 + 白色文件夹 + 绿色箭头 |
| 手机 App | **没更新**：`android/lan-file-hub-share.apk`（310.8 KB）仍是 2026-09-23 的"手机当服务器"快传版（提交 `7b9bb01`） |
| 便携库账号 | `admin`（ADMIN，09-23 建）、**`klaraljy`（USER，10-02 23:52 用户自建）**；文件数 1 |
| 当前服务 | 便携实例正常：`/health/ready` 200、首页登录页正常渲染；托盘实例 1 个、终端窗口 1 个、整机 CPU ~3% |

## 1. 启动器改用新版 PowerShell（用户要求 1）

**依据**：用户记得 PowerShell 更新过，"更新了就不要用旧的" —— 本机确实有 pwsh 7.6.6。

**做什么**

- `portable\启动.cmd`、`portable\停止.cmd`：先找 `pwsh.exe`（PATH → 常见安装路径），找到就用，**找不到退回 `powershell.exe`**。
- `portable\tools\tray.ps1`（源头 `scripts\tray.ps1`）：保持两种宿主都能跑（现用 API 在 7 上都兼容）；
  面板上**打印实际使用的解释器与版本**（以后排查一眼就知道跑的是 7 还是 5.1）。
- 桌面快捷方式仍指向 `启动.cmd`，不用改；`scripts\create-desktop-shortcut.ps1` 不用改。
- 同步更新 `docs/portable.md`：说明两种宿主、兜底规则、以及"面板会显示实际版本"。

**验收（必须真跑，不能只看代码）**

1. `pwsh -File tray.ps1 -Action status` 与 `powershell -File tray.ps1 -Action status` 各跑一次，都能打印面板；
2. 双击桌面图标：托盘图标出现、右键菜单能弹出（pwsh 是 STA，理论可行但要实测）、面板中文正常；
3. **安静性**沿用昨天的测法：输出重定向到文件，跑 20 秒，第 7 秒与第 18 秒文件大小必须相同；
4. 服务状态与 5.1 下一致（PG 55432 / Redis 56379 / 后端 9180 全在听）。

**风险**：pwsh 是 MSIX（应用商店）安装，卸载或更新时 `pwsh.exe` 别名可能短暂失效 → 所以**必须保留 5.1 兜底分支**，且面板显示实际版本，出问题一眼能看出来。

## 2. 网页左上角换成用户给的图标（用户要求 3）

- 处理素材：1600×1600 → 生成 **256px 与 128px PNG**（透明/圆角保留），放 `frontend/public/`；
  同一张图同时用作**浏览器标签图标**（替换现有 `favicon.ico`）与**桌面快捷方式图标**。
- 改 `frontend/src/app/AppLayout.tsx`：侧边栏顶部由纯文字改为 `图标 + lan-file-hub`；移动端顶部栏同样处理。
- 验收：`npm run build` 通过；便携实例刷新后左上角能看到图标；手机浏览器同样能看到；`/logo.png` 返回 200 且 ≤ 100 KB。
- 文档：`docs/frontend.md` 记一句"logo 源头 = `frontend/public/logo.png`，图标源头同文件"。

## 3. 手机 App 改成"连电脑"的连接器（用户要求 4，方案已确认）

**先明确回答用户**：App **还没更新**，现在装的那个是 09-23 的快传版；改完会重新出 APK。

**做什么**（按用户选的简单方案：在 App 内打开电脑的网页）

- 新增「连接」页并设为默认页：
  - 服务器列表 = 历史记录（`SharedPreferences` 存 `ip:port` + 最后使用时间）；
  - 每条实时显示**在线/离线**（`GET /health/live`，超时 1.5 秒；下拉刷新 + 页面可见时每 10 秒自动刷）；
  - 点一条 → 内置 `WebView` 打开 `http://<ip>:<port>/`（开 JS、DOM storage、cookie 持久化 → 登录态可保持）；
  - 「+ 添加」手输 IP（允许只输 `192.168.1.5`，自动补 `http://` 与 `:9180`）；长按删除；显示"最后使用"；
  - 打开 App 自动带出上次用的服务器。
- 保留原「快传」页（两机互传），两个 Tab 切换。
- 构建：`.\scripts\build-android.ps1` → 新 APK 落到 `android\`（按项目规则提交产物），文档 `docs/android.md` 同步更新。

**验收（真机）**

1. 用户手机装上新 APK → 添加电脑 IP → 显示"在线" → 点开能在 App 内登录并看到文件列表；
2. 电脑停止服务后 → 该条目变"离线"，App 不卡死、不崩溃；
3. App 内下载一个小文件成功（走浏览器原生下载路径）。

**风险**：WebView 内下载大文件（目标 ≤5 GB）行为需实测；Android 9+ 默认禁明文 HTTP（现有 manifest 已处理 `usesCleartextTraffic`，要复核）；手机与电脑必须同一 WiFi/热点（校园网客户端隔离时用手机热点）。

## 4. 账号与其他待办

- 便携库里已有用户自建的 `klaraljy`（USER）✅；`admin`（ADMIN）仍在，口令在 `portable\logs\bootstrap-admin.txt`（已实测可登录）。
- **待用户决定**：要不要把 `klaraljy` 提升为 ADMIN？（自己用不需要；要管理用户/看审计才需要）
- 待用户决定（不急）：关掉自助注册 + 管理员建号（公司场景）；开机自启；**隐藏命令行窗口、全部在浏览器里操作**（用户说的 2_）。

## 5. 收尾

- 每件事：跑定向验证 → 提交（一个逻辑变化一个提交）→ 更新对应 `docs/`。
- 全部做完：把这几项作为阶段 ⑥/⑦ 写进 `AGENTS.md` §15 表；`docs/guide.md`（大白话说明）补"手机 App 怎么连电脑"。
- 仍未验证的老账（不要写成已完成）：真机 APK、手机热点下的扫码路径、5 GB 大文件、iOS 端接收、TSan/UBSan、生产 HTTPS、局域网长时间压测。

---

## 6. 邀请制注册（用户 2026-10-02 明确要求：不是所有人都能注册，要通过管理员邀请）

**目标形态**：默认仍允许自助注册（个人自用方便）；**关掉自助注册后**，只有拿到管理员邀请链接的人才能注册。

**数据（迁移 `0010_invites.sql`）**

```sql
CREATE TABLE invites (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  token_hash   TEXT NOT NULL UNIQUE,      -- 只存哈希（与分享链接同口径，明文只在创建时返回一次）
  note         TEXT NOT NULL DEFAULT '',  -- 备注：这条邀请是给谁的
  created_by   UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  expires_at   TIMESTAMPTZ,               -- NULL = 不过期
  max_uses     INT NOT NULL DEFAULT 1,
  used_count   INT NOT NULL DEFAULT 0,
  revoked_at   TIMESTAMPTZ,
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

**配置**：`ALLOW_SELF_REGISTRATION`（默认 `true`；便携包/公司场景设 `false`）。关闭后
`POST /api/v1/auth/register` **必须**带有效邀请，否则 `403 INVITE_REQUIRED`。

**接口**

| 方法 | 路径 | 谁 | 说明 |
|---|---|---|---|
| POST | `/api/v1/admin/invites` | ADMIN | 建邀请（`note` / `expires_in_seconds` / `max_uses`），**明文 token 只在这里返回一次** |
| GET | `/api/v1/admin/invites` | ADMIN | 列表：状态、已用次数、过期时间（**不含 token**） |
| DELETE | `/api/v1/admin/invites/{id}` | ADMIN | 撤销（幂等） |
| GET | `/api/v1/public/invites/{token}` | 公开 | 校验：有效 → 返回备注与剩余次数；无效/过期/撤销 → 404 / 410 |
| POST | `/api/v1/auth/register` | 公开 | 新增可选 `invite_token`；自助注册关闭时必须提供；**在同一事务里**消耗一次使用次数 |

**安全**：token 只存哈希；注册接口已有速率限制（沿用）；建邀请与用邀请都写审计日志；
`note` 与 token 不得进日志正文。

**前端**：管理员页面新增「邀请」区（生成 → 弹窗展示**链接 + 二维码**，复用 `ShareLinkBox`，
不绕过它）；注册页支持 `?invite=<token>`（显示"你是被邀请来的"，提交时带上 token）；
自助注册关闭且无邀请时，注册页明确写"本实例需要邀请才能注册，请找管理员要链接"。

**测试**：单元（过期 / 次数用尽 / 撤销 / 哈希不落明文）、集成（开关开与关、带与不带 token、
重复使用同一 token 的行为）、前端单测（注册页三种状态）、e2e 可选。

---

## 7. 手机 App（用户 2026-10-02 选定的简单方案）

**已明确回答用户**：现在那个 APK 还是 09-23 的"手机当服务器"快传版，**没有**连电脑的能力。

**做什么**（§3 有更细的验收）：新增「连接」页（IP 历史 + 在线状态 + App 内 WebView 打开电脑网页），
保留「快传」页。要点：`SharedPreferences` 存 `host:port` 与最后使用时间；
`GET /health/live` 1.5 秒超时探活（页面可见时每 10 秒刷新 + 下拉刷新）；
输入允许省略协议与端口（自动补 `http://` 与 `:9180`）；WebView 开 JS / DOM storage / cookie 持久化。

**构建与交付**：`.\scripts\build-android.ps1` → 新 APK 落到 `android\`，按项目规则提交产物，
`docs/android.md` 同步更新；请用户真机安装验证（添加 IP → 显示在线 → App 内登录 → 看到文件列表）。
