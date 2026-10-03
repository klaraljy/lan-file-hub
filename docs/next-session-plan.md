# 下一轮计划（2026-10-03）

> **状态：本文件里的 ①～⑦ 已全部完成（2026-10-03）**，实测记录见各节与对应提交。
> 提交：`a88ee98`/`a4e054f`（网络跟随）、`6a1a633`（命令行提管理员）、`dab63b5`（pwsh 7 启动器）、
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

## 9. ⛔ 下一件事（优先级最高）：同名文件上传不该被"报错挡住"（用户 2026-10-03 实测）

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

### 9.1 顺带发现的第二个缺陷：网页目录里的 `.apk` 打不开（2026-10-03）

把新的 APK 放进 `portable\app\web\lan-file-hub.apk`（338.8 KB）后，
`GET /lan-file-hub.apk` 返回的是 **`text/html`（SPA 回退页），不是 APK** ——
说明后端的静态托管**只认白名单里的扩展名**，`.apk` 不在其中，于是走了前端路由回退。
后果：用户**没法从自己的服务器把 APK 下到手机**（这恰恰是他要干的事）。

**要做的**：`backend/src/api/web_hosting.cpp` 里给静态响应补一个兜底 ——
白名单之外的扩展名用 `application/octet-stream` 返回（并保留 `X-Content-Type-Options: nosniff`），
而不是回退到 index.html；只有**看起来像前端路由**的路径（无扩展名 / `.html`）才回退。
验收：`GET /lan-file-hub.apk` → 200 + `application/octet-stream` + 正确大小；
`GET /files`（前端路由）→ 仍是 index.html。

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
