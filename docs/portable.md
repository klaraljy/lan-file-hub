# 便携包（免 Docker / 免 Node）与托盘程序

> 五阶段改造 ④（`AGENTS.md` §15）。目标：把项目交给**不懂技术的人**时，
> 拿到一个文件夹、**双击一个图标**就能用 —— 不出现命令行窗口、不需要 Docker、不需要 Node.js。

## 1. 形态

| | 开发环境（现在用的） | 便携包 |
|---|---|---|
| 依赖 | Docker Desktop（PostgreSQL + Redis 容器） | **无**（自带免安装 PostgreSQL + Redis） |
| 启动 | `scripts\menu.ps1` 命令行菜单 | **双击桌面 `lan-file-hub` 图标** → 窗口打印「电脑地址 / 手机地址」→ 自动打开浏览器 → 托盘图标 |
| 前端 | vite 开发服务器（需要 Node） | `app\web\`（构建好的 dist，由后端托管，见 §5） |
| HTTP 端口 | 9080（vite 5173） | **9180** |
| PostgreSQL | 5432（容器） | **55432**（只监听 127.0.0.1） |
| Redis | 6379（容器） | **56379**（只监听 127.0.0.1） |

端口**刻意错开**，所以便携包与开发环境可以同时跑，互不干扰。

## 2. 目录结构

```text
portable/
├── 启动.cmd            # 用户入口：双击 → 打印地址面板 + 自动打开浏览器 + 托盘图标（自动启动服务）
├── 停止.cmd            # 停止（数据保留）
├── 备份.cmd            # 一键备份：数据库 + 文件对象 + 清单（2026-10-03）
├── 恢复.cmd            # 一键恢复：先核对清单、再自动另存当前状态，二次确认后才覆盖（2026-10-03）
├── 开机自启.cmd         # 开机自启开关（启动文件夹快捷方式，不需要管理员，随时可撤销；默认关闭）
├── 自动上传.cmd         # 「放进文件夹就自动上传」：交互问本地文件夹与账号（2026-10-03）
├── 设为管理员.cmd       # 把**已有**账号提升为管理员（不需要口令；用户 2026-10-02 要求）
├── favicon.ico         # 桌面快捷方式与网页标签的图标（源头 frontend\public\favicon.ico）
├── 使用说明.txt         # 大白话说明（GBK 编码，记事本直接可读）
├── README.md           # 面向开发者/维护者
├── backups/            # 运行时：备份产物（每次一个时间戳目录；gitignore，里面是用户数据）
├── app/                # ← portable-setup.ps1 生成：后端 exe、迁移工具、migrations、.env、web/
├── pgsql/              # ← portable-setup.ps1 下载：EDB 免安装 PostgreSQL
├── redis/              # ← portable-setup.ps1 拷贝：Redis for Windows
├── tools/              # ← portable-setup.ps1 拷贝：tray.ps1、_env.ps1、make-admin.ps1、备份/恢复/自启/自动上传脚本
├── data/               # 运行时：data\pg（数据库）、data\redis、data\storage（文件对象）
└── logs/               # 运行时：backend.log/.err、postgres.log、redis.log、tray 相关
```

`.gitignore` 里**逐个列出**了 `app/`、`pgsql/`、`redis/`、`tools/`、`data/`、`logs/`、`backups/`
（不用通配符 —— 全局 `AGENTS.md` §2.2 记录过 `.gitignore` negation 被父目录规则吃掉的坑）。
仓库里只提交骨架：几个 `.cmd`、`使用说明.txt`、`README.md`
（`favicon.ico` 由 `portable-setup.ps1` 从 `frontend\public\` 拷进来）。

> ⛔ 所有 `.cmd` 一律**纯 ASCII、无 BOM**：`cmd.exe` 会把 UTF-8 BOM 当成第一行的一部分而报错；
> 中文写在 `.ps1` 里（那些脚本带 BOM，PowerShell 5.1 才不会乱码）。

## 3. 准备（在开发机上跑一次）

```powershell
.\scripts\build.ps1 -Preset release        # 后端 release（1.8 MB 左右）
cd frontend; npm run build; cd ..          # 前端 dist（1.4 MB 左右）
.\scripts\portable-setup.ps1               # 下载 PG(≈330MB) + initdb + 建库 + 迁移 + 建管理员
```

- 下载会**断点续传**（`curl -C -`）、**失败自动重试**、**代理不通自动改直连**；
  实测过一次 257 MB 处被网络中断，重跑后从断点继续。
- `.env` 里的 **JWT 密钥与数据库口令都是随机生成**的（不复用开发机上的任何口令），
  `.env` 本身被 `.gitignore` 忽略，**不会进仓库**。
- 自动生成的管理员引导口令**不打印到屏幕**，写进 `portable\logs\bootstrap-admin.txt`
  （该目录已忽略），登录后应立即修改并删除该文件。

## 4. 托盘程序（`scripts\tray.ps1`）

**解释器**（用户 2026-10-02 要求"更新了就不要用旧的"）：入口脚本 `启动.cmd` / `停止.cmd` **优先用
`pwsh`（PowerShell 7，本机 7.6.6）**，`where pwsh` 找不到时才退回 `powershell`（5.1）；
面板上会打印**实际**用的版本（`解释器：PowerShell 7.6.6 (Core)` / `5.1…(Desktop)`），
出问题一眼能分清是哪个宿主。实测两个宿主都能跑（含托盘图标 WinForms，pwsh 默认就是 STA）。

一个脚本、两种形态（自动识别）：脚本在 `portable\tools\` 下 = 便携包形态（自己拉起 PG/Redis/后端）；
在 `scripts\` 下 = 开发形态（转调 `start.ps1` / `stop.ps1`）。

| 能力 | 说明 |
|---|---|
| 状态 | 图标提示与状态菜单实时显示后端/数据库/Redis 是否在跑、以及**手机该打开的地址** |
| 双击图标 | 打开网页 |
| 右键菜单 | 启动 / 停止 / 打开网页 / 查看日志 / 打开数据目录 / 使用说明 / 退出（不停止服务） |
| 无窗口启动 | 后端用 `Start-Process -WindowStyle Hidden` 拉起，输出重定向到 `logs\backend.log`，**不弹命令行** |
| 双击即用 | 托盘启动时若后端没在跑，会自动启动一次并在气泡里报地址 |
| **看得见的地址**（用户 2026-10-02 要求） | 启动后窗口里直接打印「电脑打开 / 手机打开 / 各组件状态 / 怎么用 / 怎么停」的面板 —— **不允许"双击之后什么都不显示"**（那正是当时的投诉） |
| **自动开浏览器**（用户 2026-10-02 要求） | 启动完成即用默认浏览器打开登录画面（未登录访问 `/` 会跳 `/login`）；`-NoBrowser` 可关掉，仅供自动化验证 |
| **换网络自动更新地址**（用户 2026-10-02 要求） | 托盘每 5 秒查一次本机局域网地址，**变了就打印**「[网络已变化] 手机现在请用：http://…」，托盘悬停提示同步更新。后端监听 `0.0.0.0`，所以**不用重启**（变的只是"该告诉手机哪个地址"） |
| **显示实际解释器**（用户 2026-10-02 要求） | 面板打印 `解释器：PowerShell 7.6.6 (Core)` / `5.1…(Desktop)` —— 本机同时装着两个版本，出问题要一眼分清 |
| 非交互式 | stdin 被重定向时直接打印状态退出（不会卡在 `Read-Host`，这是项目踩过的坑） |

命令行等价入口（便于脚本化与自动化验证）：

```powershell
.\scripts\tray.ps1 -Action start | stop | status | open
.\scripts\tray.ps1 -Action tray -SelfTestSeconds 5   # 起托盘 5 秒后自动退出（自动化验证用）
.\scripts\tray.ps1 -Action start -NoBrowser          # 只启动 + 打印地址面板，不弹浏览器
```

## 5. 单端口形态（`WEB_ROOT`）—— 已实测可用

便携包**不跑 vite、不需要 Node**：后端直接把 `app\web` 当静态目录托管，前端路由回退到 `index.html`。
配置项是 `WEB_ROOT`（`.env` 里写好 `WEB_ROOT=web`）。

实测（2026-09-21，release 构建 + 便携 PostgreSQL + 便携 Redis）：

| 请求 | 结果 |
|---|---|
| `GET /` | **200**，返回 `index.html`（含 `<div id="root">`） |
| `GET /assets/index-*.js` | **200** |
| `GET /files`（前端路由） | **200**（回退到 index.html） |
| `GET /health/ready` | **200**（PostgreSQL + Redis + 存储都可用） |
| `POST /api/v1/auth/login` → 上传 → 票据下载 | 全部成功，**下载内容与上传一致** |

结论：便携包就是"**双击托盘图标 → 浏览器直接能用**"，不需要 Node、不需要 Docker。

（若哪天后端版本没有这个能力，托盘状态会明确写「网页托管未接入」，不会假装正常。）

## 6. 已知限制（诚实清单）

- 只有 Windows 试过；Linux/macOS 未验证。
- **没在别人的电脑上试过**：首次运行杀软/防火墙可能弹拦截，放行一次即可。
- **Windows 11 默认把新托盘图标收进"隐藏的图标"里**：第一次用可能要去任务栏设置里把它固定出来
  （设置 → 个性化 → 任务栏 → 其他系统托盘图标）。这是 Windows 行为，不是程序问题；
  启动面板会把地址直接打印在窗口里，所以即使图标被折叠也不影响使用。
- PostgreSQL 只监听 `127.0.0.1`（局域网只暴露 HTTP 端口）；Redis 同理。
- Redis 可选：没拷贝到时后端会提示"Redis 未配置"、`/health/ready` 会返回 503，核心文件功能不受影响。
- 便携包里的 PostgreSQL 是 EDB 免安装二进制（**不是**安装程序），升级需手动替换 `pgsql\`。
- 未做：安装器（NSIS 可用但未做）、开机自启、自动更新、代码签名。
- 脚本内部有**四条"踩过坑"的硬约束**（改这块时别踩回去）：
  1. **不能给 `pg_ctl` 的输出套管道、也不能用 `*>` 重定向**：postgres 子进程会继承那个句柄，
     PowerShell 5.1 永远等不到 EOF —— 现象是数据库起来了、脚本却**永久卡在这一行**，后面的
     Redis 与后端根本不启动（2026-10-02 实测复现，用户双击入口就是这个症状）。必须用
     `Start-Process` 重定向到**文件**，再用**轮询端口**判断就绪。
  2. **`Start-Process -ArgumentList` 传数组时不会给含空格的元素加引号**：
     `'-o', ('-p ' + $PgPort + ' -h 127.0.0.1')` 会被拆成两个参数，`pg_ctl` 报
     「无效的参数模式 "55432"」直接拒绝启动 → 要拼成一整条带引号的字符串。
  3. **便携 PostgreSQL 的端口必须写进 `postgresql.conf`**：`pg_ctl -o '-p 55432'` 只对**这一次**
     启动有效，之后任何不带 `-o` 的启动都退回默认 5432，而后端 `.env` 连的是 55432
     → 「数据库明明起来了，后端却 connection refused」（2026-10-02 踩到；`portable-setup.ps1`
     现在会把端口写进配置文件）。
  4. **Redis 7 不能传 `--save ''` 这种空参数**：进程会活着但端口不监听（实测踩到过）。
  5. **入口脚本里不要 `chcp 65001`**（2026-10-02 用户实测）：UTF-8 控制台 + ConPTY 会把**东亚宽字符
     打印两遍** —— 中文、`：`、`✅` 全部变成「已已启启动动」，ASCII 与 `═` 却正常。
     保持系统默认代码页即可（PowerShell 按控制台代码页输出，中文正常）；
     控制台里也**只用 ASCII 装饰**（`=`、`-`、`[OK]`），不要用 `✅/❌/═/─/→`。
  6. **每 5 秒跑一次的状态检查不许发 HTTP、也不许产生任何控制台输出**（2026-10-02 用户投诉
     窗口"一闪一闪"）：PowerShell 5.1 的 `Invoke-WebRequest` 会写进度条，控制台一有输出窗口就闪。
     脚本顶部必须保持 `$ProgressPreference = 'SilentlyContinue'`，`Update-Tray` 只做 TCP 探活；
     HTTP 探测只留在一次性动作（`Show-Panel`、`Open-Web`）里。
     验收方法：把托盘输出重定向到文件，跑 20 秒，比较第 7 秒与第 18 秒的文件大小 —— 必须一样。
  7. **端口在听 ≠ 数据库可以连**（2026-10-02 实测）：PostgreSQL 启动时端口先打开，之后才做恢复，
     这期间连它会报「数据库系统正在启动」，后端**一连就退出**（面板显示"数据库 [OK]、后端 [--]"）。
     所以 `Start-Stack` 在端口通了之后还要用 `pg_isready.exe` 问到 `accepting connections` 才启动后端。
     复现与验收：`pg_ctl stop -m immediate` 模拟崩溃 → 再用托盘启动，后端日志里不应出现任何连接失败。

## 7. 备份、恢复、开机自启、自动上传（2026-10-03）

四个双击入口，都不需要 Docker、不需要管理员权限：

| 入口 | 脚本 | 干什么 |
|---|---|---|
| `备份.cmd` | `tools\portable-backup.ps1` | `pg_dump -Fc` 导出数据库 + `tar` 打包 `data\storage` + 写 `manifest.json`（大小、SHA-256、迁移版本、用户/文件/目录计数）。默认落到 `portable\backups\<时间戳>\`；`-OutDir D:\backups` 可以放到别的盘（**建议**：备份与原始数据别放同一块盘） |
| `恢复.cmd` | `tools\portable-restore.ps1` | ① 核对清单哈希 → ② **先给当前状态做一次备份**（失败就中止，不冒险）→ ③ 停服务 → ④ 重建库 + `pg_restore` → ⑤ 恢复文件对象（先解到临时目录，失败时原对象不动）→ ⑥ 重启服务并探活。不带参数会列出可用备份让你挑；`-Force` 跳过二次确认（自动化用） |
| `开机自启.cmd` | `tools\autostart.ps1` | 在**当前用户的启动文件夹**放一个指向 `启动.cmd` 的快捷方式。参数：无 = 打开，`-Disable` = 关掉，`-Status` = 只看状态。**默认关闭**，脚本不会自己打开 |
| `自动上传.cmd` | `tools\watch-folder.ps1` | 盯住一个本地文件夹，丢进去的文件自动传到服务器（单向：本地 → 服务器）。令牌存 `%LOCALAPPDATA%\lan-file-hub\watch-token.txt`（⛔ 不存口令），已传清单存同级 `watch-state.json`，重复运行不会重复上传 |

**两条踩过的坑（改这些脚本时别再犯）**

1. ⛔ **先备份再停服务**：数据库停了 `pg_dump` 连不上，会产出 **0 字节的空 dump**。
   第一次写恢复脚本时就踩了（脚本先停服务再"恢复前备份"）→ 现在 pre-backup 在停服务之前，
   且失败即中止；恢复入口也会拒绝空 dump。
2. ⛔ **启停服务一律用 `Start-Process`（脱离式）**：写成 `& pwsh -File tray.ps1 *> log` 会**永久卡住**
   —— 被启动的 PG/Redis/后端继承那个重定向句柄，父进程永远等不到结束（AGENTS §11.1 的同一个坑，
   当时的表现是"命令跑了 7 分钟没动静"）。

**实测（2026-10-03，便携实例）**

```text
备份：database.dump 41.5 KB（users=3 files=2 folders=3，迁移 0001…0011）+ storage.tar 0.34 MB + manifest.json
恢复：恢复到该备份 → users=3 files=2 folders=3（与清单一致）→ /health/live = 200，用时 24.5 秒
自动上传：2 个文件（含子目录）→ 服务器上出现 2 个；再跑 = 0 个重传；新增 1 个 = 只传 1 个
开机自启：-Status 显示"未开启"，脚本没有擅自打开（是否开启由用户决定）
```

## 8. 桌面入口（带图标的快捷方式）
**只有快捷方式能带自定义图标**（`.cmd` 只能显示系统默认图标），所以桌面入口是 `lan-file-hub.lnk`：

```powershell
.\scripts\create-desktop-shortcut.ps1          # 便携包形态（指向 portable\启动.cmd）
.\scripts\create-desktop-shortcut.ps1 -Dev     # 开发形态（指向 scripts\start.ps1）
.\scripts\create-desktop-shortcut.ps1 -Remove  # 删掉桌面图标
```

图标取 `portable\favicon.ico`（源头是 `frontend\public\favicon.ico`，网页标签用的也是它）。

实测（2026-10-02）：双击桌面图标 → 弹出标题为 `lan-file-hub` 的窗口并打印地址面板
（电脑 `http://127.0.0.1:9180/`、手机 `http://<局域网IP>:9180/`）→ 默认浏览器自动打开登录画面
→ 右下角托盘图标可用于停止/看日志；关掉窗口不会停服务。
