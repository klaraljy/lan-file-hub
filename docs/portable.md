# 便携包（免 Docker / 免 Node）与托盘程序

> 五阶段改造 ④（`AGENTS.md` §15）。目标：把项目交给**不懂技术的人**时，
> 拿到一个文件夹、**双击一个图标**就能用 —— 不出现命令行窗口、不需要 Docker、不需要 Node.js。

## 1. 形态

| | 开发环境（现在用的） | 便携包 |
|---|---|---|
| 依赖 | Docker Desktop（PostgreSQL + Redis 容器） | **无**（自带免安装 PostgreSQL + Redis） |
| 启动 | `scripts\menu.ps1` 命令行菜单 | **`portable\启动.cmd` → 托盘图标** |
| 前端 | vite 开发服务器（需要 Node） | `app\web\`（构建好的 dist，由后端托管，见 §5） |
| HTTP 端口 | 9080（vite 5173） | **9180** |
| PostgreSQL | 5432（容器） | **55432**（只监听 127.0.0.1） |
| Redis | 6379（容器） | **56379**（只监听 127.0.0.1） |

端口**刻意错开**，所以便携包与开发环境可以同时跑，互不干扰。

## 2. 目录结构

```text
portable/
├── 启动.cmd            # 用户入口：双击 → 托盘图标（自动启动服务）
├── 停止.cmd            # 停止（数据保留）
├── 使用说明.txt         # 大白话说明（GBK 编码，记事本直接可读）
├── README.md           # 面向开发者/维护者
├── app/                # ← portable-setup.ps1 生成：后端 exe、迁移工具、migrations、.env、web/
├── pgsql/              # ← portable-setup.ps1 下载：EDB 免安装 PostgreSQL
├── redis/              # ← portable-setup.ps1 拷贝：Redis for Windows
├── tools/              # ← portable-setup.ps1 拷贝：tray.ps1、_env.ps1（让便携包自包含）
├── data/               # 运行时：data\pg（数据库）、data\redis、data\storage（文件对象）
└── logs/               # 运行时：backend.log/.err、postgres.log、redis.log、tray 相关
```

`.gitignore` 里**逐个列出**了 `app/`、`pgsql/`、`redis/`、`tools/`、`data/`、`logs/`
（不用通配符 —— 全局 `AGENTS.md` §2.2 记录过 `.gitignore` negation 被父目录规则吃掉的坑）。
仓库里只提交骨架：两个 `.cmd`、`使用说明.txt`、`README.md`。

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

一个脚本、两种形态（自动识别）：脚本在 `portable\tools\` 下 = 便携包形态（自己拉起 PG/Redis/后端）；
在 `scripts\` 下 = 开发形态（转调 `start.ps1` / `stop.ps1`）。

| 能力 | 说明 |
|---|---|
| 状态 | 图标提示与状态菜单实时显示后端/数据库/Redis 是否在跑、以及**手机该打开的地址** |
| 双击图标 | 打开网页 |
| 右键菜单 | 启动 / 停止 / 打开网页 / 查看日志 / 打开数据目录 / 使用说明 / 退出（不停止服务） |
| 无窗口启动 | 后端用 `Start-Process -WindowStyle Hidden` 拉起，输出重定向到 `logs\backend.log`，**不弹命令行** |
| 双击即用 | 托盘启动时若后端没在跑，会自动启动一次并在气泡里报地址 |
| 非交互式 | stdin 被重定向时直接打印状态退出（不会卡在 `Read-Host`，这是项目踩过的坑） |

命令行等价入口（便于脚本化与自动化验证）：

```powershell
.\scripts\tray.ps1 -Action start | stop | status | open
.\scripts\tray.ps1 -Action tray -SelfTestSeconds 5   # 起托盘 5 秒后自动退出（自动化验证用）
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
  （设置 → 个性化 → 任务栏 → 其他系统托盘图标）。这是 Windows 行为，不是程序问题。
- PostgreSQL 只监听 `127.0.0.1`（局域网只暴露 HTTP 端口）；Redis 同理。
- Redis 可选：没拷贝到时后端会提示"Redis 未配置"、`/health/ready` 会返回 503，核心文件功能不受影响。
- 便携包里的 PostgreSQL 是 EDB 免安装二进制（**不是**安装程序），升级需手动替换 `pgsql\`。
- 未做：安装器（NSIS 可用但未做）、开机自启、自动更新、代码签名。
- 脚本内部有两条"踩过坑"的硬约束（改这块时别踩回去）：
  1. **不能给 `pg_ctl` 的输出套管道**（`| Out-Null`）：postgres 子进程会继承管道句柄导致永久阻塞；
     要重定向到文件。
  2. **Redis 7 不能传 `--save ''` 这种空参数**：进程会活着但端口不监听（实测踩到过）。
