# lan-file-hub 便携包

**给不懂技术的人看的说明在 [`使用说明.txt`](使用说明.txt)**（双击就能看）。

这个目录是"免 Docker、免 Node"的分发包骨架：二进制与大文件都在 `.gitignore` 里，
由 `scripts\portable-setup.ps1` 一键准备。

## 目录

```text
portable/
├── 启动.cmd            # 用户入口：双击 → 托盘图标（自动启动服务，不弹命令行窗口）
├── 停止.cmd            # 停止（数据保留）
├── 使用说明.txt         # 大白话说明（给用户）
├── app/                # 后端 exe + 迁移工具 + migrations + .env + web/（前端 dist）  ← 准备脚本生成
├── pgsql/              # 免安装 PostgreSQL（EDB binaries zip 解压）                  ← 准备脚本下载
├── redis/              # Redis for Windows（从 D:\Codex Tools\Redis 拷贝）           ← 准备脚本拷贝
├── tools/              # tray.ps1 与 _env.ps1 的副本（让便携包自包含）                ← 准备脚本拷贝
├── data/               # 运行时数据：PostgreSQL 数据目录、Redis 数据、对象存储        ← 运行时生成
└── logs/               # 后端/数据库/Redis/托盘日志                                  ← 运行时生成
```

## 准备（在开发机上跑一次）

```powershell
.\scripts\build.ps1 -Preset release                 # 后端 release
cd frontend; npm run build; cd ..                   # 前端 dist
.\scripts\portable-setup.ps1                        # 下载便携 PostgreSQL(≈330MB) + initdb + 迁移 + 建管理员
```

端口**刻意避开开发环境**，两者可以同时跑：

| | HTTP | PostgreSQL | Redis |
|---|---|---|---|
| 便携包 | 9180 | 55432 | 56379 |
| 开发环境 | 9080 | 5432（Docker） | 6379（Docker） |

## 已知限制（诚实清单）

- **网页托管（`WEB_ROOT`）已实测可用**：`GET /`、`GET /assets/*.js`、前端路由 `/files` 全部 200，
  `/health/ready` 200。也就是"双击托盘 → 浏览器直接能用"，不需要 Node、不需要 Docker。
- Linux/macOS 没试过（只做了 Windows）。
- 没在**别人的电脑**上试过：杀软/防火墙首次运行可能弹拦截（放行一次即可）。
- Windows 11 默认把新托盘图标收进"隐藏的图标"里，可能要去任务栏设置固定出来（系统行为）。
- PostgreSQL 用的是 EDB 免安装二进制，**只监听 127.0.0.1**；局域网只暴露 HTTP 端口。
- Redis 可选：没有它后端会提示"Redis 未配置"且 `/health/ready` 返回 503，核心文件功能不受影响。
