# 配置、部署与发布

## 配置管理

配置通过环境变量或配置文件提供，但普通用户不需要修改配置才能使用系统。

### 配置项清单

```text
APP_ENV
HTTP_PORT
DATABASE_URL
REDIS_URL
STORAGE_ROOT
MAX_FILE_SIZE
DEFAULT_CHUNK_SIZE
MAX_CONCURRENT_UPLOADS
MAX_CONCURRENT_UPLOADS_PER_USER
MAX_USER_STORAGE_BYTES
UPLOAD_SESSION_TTL
JWT_SECRET
JWT_EXPIRE_SECONDS
JWT_REMEMBER_EXPIRE_SECONDS
DOWNLOAD_RATE_LIMIT
SHARE_UNLOCK_RATE_LIMIT_PER_MINUTE
LOG_LEVEL
CORS_ALLOWED_ORIGINS
WEB_ROOT
APP_APK_PATH
```

> `WEB_ROOT`（五阶段改造 ④）：留空 = 后端只提供 API（开发用 vite、生产用 nginx）；
> 填成前端 dist 目录（例如 `web`）则**由后端直接托管前端**：静态资源走 documentRoot，
> 前端路由（`/files`、`/shared`、`/s/<token>` 等）在找不到文件时回退 `index.html`，
> 而 `/api/`、`/health`、`/metrics` 永不回退（保证前端拿到的是 JSON 而不是 HTML）。
> 便携包就是靠它做到**单端口、单进程、不需要 Node.js**。

> `APP_APK_PATH`（2026-10-03）：配套安卓安装包的位置，供 `GET /api/v1/app/release`
> 与 `GET /api/v1/app/download` 使用。**留空**时跟着 `WEB_ROOT` 走
> （`<WEB_ROOT>/downloads/lan-file-hub.apk`，便携包默认就是这样）；两个都没配时下载入口
> 如实显示"还没有准备好安装包"，不影响其他功能。
> 接口每次请求现看这个文件，所以 `scripts\build-android.ps1` 重新出包（它会把新包与
> `lan-file-hub.apk.json` 侧车一起同步过去）之后**不需要重启后端**，刷新网页即可。

> `JWT_REMEMBER_EXPIRE_SECONDS`（2026-10-03）：登录页勾了「记住我」时签发的令牌有效期，
> 默认 `2592000`（30 天），允许 1 小时 ~ 365 天。不勾时仍用 `JWT_EXPIRE_SECONDS`（默认 1 小时）。
> 用户真机反馈"每次打开 App 都要重新登录"，这是配套改动（另一半在前端 `tokenStore`：
> 勾了才把令牌放进 `localStorage`）。风险与生产替代方案见 `docs/api.md`「认证方案」。

> `SHARE_UNLOCK_RATE_LIMIT_PER_MINUTE`（2026-10-03）：分享密码解锁接口的限流，
> 按 **IP + 分享 Token** 计数，默认 `10`（允许 1~600）。分享链接是公开的，
> 不限流就等于让人随便猜密码。见 `docs/api.md`「分享密码」。

### 配置要求

- 提供 `.env.example`。
- 禁止把真实密码、JWT 密钥和数据库凭据提交到 Git。
- 生产环境启动时检查必需配置。
- 配置错误时服务必须启动失败并输出明确原因。
- 运行期间允许热加载的配置必须明确列出。
- 修改配置时必须校验新配置，校验失败不能覆盖旧配置。
- 配置读取逻辑集中在 `Config` 模块中。
- 业务代码不能直接读取环境变量。
- 配置中涉及路径时，启动阶段必须转换为规范化绝对路径。
- 对文件大小、分块大小、并发数等数值配置进行上下限校验。

### 热加载

可以在运行期间热加载的配置必须显式列在白名单里，未列入的配置一律要求重启生效。白名单必须写在文档和 `Config` 模块中，两处保持一致。

### 生产启动检查

生产环境启动时必须检查的必需配置：

| 配置项 | 检查内容 |
|---|---|
| `APP_ENV` | 取值合法 |
| `HTTP_PORT` | 有效的端口号 |
| `DATABASE_URL` | 已设置，且能建立连接 |
| `REDIS_URL` | 已设置，且能建立连接 |
| `STORAGE_ROOT` | 已设置，且可转换为规范化绝对路径、可读写 |
| `JWT_SECRET` | 已设置，不得为空、不得为示例值 |
| `CORS_ALLOWED_ORIGINS` | 已设置，且不是通配 `*` |

任何一项缺失或非法，服务必须启动失败并输出明确原因，不得回退到默认值继续运行。

**端口不可用同样属于启动失败**：服务在装配 HTTP 服务之前先自检 `HTTP_PORT`
（`common/net.h` 的 `CheckListenPort` → `PortStatus`，见 `backend/src/main.cpp` 第 3 步），
不可用时打印明确原因并以非零码退出。这样做的原因：Drogon 绑定失败时只打一行 `FATAL` 日志，
进程仍留在事件循环里不退出——外部看到的是「进程活着、端口没人听」，容器编排会误判成启动成功，
本机一键启动脚本也会被误导（阶段十六发布后实测踩到过）。

自检区分两种原因，因为处理方式完全不同：

| `PortStatus` | 含义 | 处理 |
|---|---|---|
| `kInUse` | 端口已被占用（`EADDRINUSE`/`WSAEADDRINUSE`） | 停掉已有实例，或改 `HTTP_PORT` |
| `kForbidden` | 端口被系统保留或权限不足（`EACCES`/`WSAEACCES`） | 换端口；Windows 上用 `netsh int ipv4 show excludedportrange protocol=tcp` 查看保留段 |

`kForbidden` 不是理论情况：Windows 上 Docker Desktop / Hyper-V / WSL 会预留端口段
（本机实测预留 `7920-8019`、`8020-8119`、`8120-8219`、`8320-8419`，**默认的 8080 正好落在里面**），
此时没有任何进程在监听，`bind` 却直接失败——只提示"端口被占用"会把排查方向带偏。
端口是**单点配置**：改 `.env` 的 `HTTP_PORT` 即可，`scripts/*.ps1` 与 `frontend/vite.config.ts`
都会跟着走（本机改用 `9080`）。

---

## 密钥与凭据

- 不把数据库密码写入仓库。
- 不把 `JWT_SECRET` 写入仓库。
- `.env` 必须加入 `.gitignore`。
- `.env.example` 只能包含示例值。
- 默认开发密码必须在文档中明确说明，并建议启动后修改。
- 不提交密钥、密码、Token、数据库转储和用户文件。
- 明文 Token 与口令一律不进日志。

---

## 容器与编排

### Dockerfile

- 检查 Dockerfile 是否支持多阶段构建；构建阶段与运行阶段分离。
- 减少最终镜像体积。
- 后端容器使用非 root 用户运行。

### docker-compose.yml

- 完善 `docker-compose.yml`。
- 添加 PostgreSQL 健康检查。
- 添加 Redis 健康检查。
- 配置服务启动依赖，使后端在依赖健康之后才启动。

---

## 数据卷与持久化

配置数据卷：

| 数据卷 | 保存内容 |
|---|---|
| PostgreSQL 数据卷 | 用户、文件夹、文件元数据、版本、权限、分享、审计 |
| Redis 数据卷 | 缓存与会话类临时状态 |
| 文件对象存储数据卷 | 对象目录与临时分块 |

必须验证：

1. 验证全新环境可以启动。
2. 验证重启容器后数据仍然存在。
3. 验证删除并重新创建 backend 容器后文件仍然存在。

---

## 数据库迁移与初始化

- 提供数据库迁移执行方式。
- 提供初始化管理员用户方式。
- 迁移与初始化步骤必须写入文档，命令必须与实际项目结构一致。
- 默认管理员口令属于默认开发密码，必须在文档中明确说明，并建议启动后修改。

初始化与迁移命令同时作为 README 的必备章节（见下文 README 要求）。

---

## 备份与恢复

- 提供备份脚本。
- 提供数据库恢复脚本。
- 提供文件对象目录备份说明。
- 脚本使用 PowerShell（`.ps1`），落在 `scripts/` 下。
- 备份说明必须覆盖：PostgreSQL 备份、文件对象目录备份、Redis 是否备份及其影响。
- 备份与恢复流程的运行细节见 `recovery.md`。

---

## 反向代理与 HTTPS

- 提供反向代理配置示例。
- 提供 HTTPS 配置说明。
- 反向代理必须正确转发上传与下载的大文件请求，不得引入额外的请求体大小限制而截断分块。
- 生产环境的 TLS 终止位置、证书来源与刷新方式必须写明。

---

## 环境变量与生产安全说明

- 提供环境变量文档：逐项列出配置项、含义、默认值与是否必需。
- 提供生产环境安全配置说明：`APP_ENV`、`CORS_ALLOWED_ORIGINS`、`JWT_SECRET`、管理员口令、日志级别、存储路径权限。
- 无法验证的生产部署能力必须标注为未验证；不得把未验证的能力描述为可用。

---

## CI 与发布产物

| 产物 | 要求 |
|---|---|
| `.github/workflows/` | CI 必须能够通过 |
| `.gitignore` | 检查覆盖 `.env`、密钥、构建产物、数据目录 |
| `LICENSE` | 添加许可文件 |
| `CONTRIBUTING.md` | 添加贡献指南 |
| `CODE_OF_CONDUCT.md` | 添加行为准则 |
| `SECURITY.md` | 添加安全策略与漏洞报告方式 |
| `CHANGELOG.md` | 添加变更记录 |
| `docs/release-checklist.md` | 生成发布检查清单 |
| 完整构建与测试 | 执行完整构建和测试 |

### 发布准备项落点

| # | 准备项 | 落点 |
|---|---|---|
| 1 | 完善 README.md | 下文 README 要求 |
| 2 | 添加项目功能概览 | README 必备章节 3 |
| 3 | 添加系统架构图 | README 必备章节 4；`architecture.md` |
| 4 | 添加目录结构说明 | README 内容准确性要求；项目根 `AGENTS.md` 第 3 节 |
| 5 | 添加本地启动步骤 | README 必备章节 6、7 |
| 6 | 添加 Docker 启动步骤 | README 必备章节 7；本文件「容器与编排」 |
| 7 | 添加前端启动步骤 | README 必备章节 10；`frontend.md` |
| 8 | 添加数据库迁移步骤 | README 必备章节 9；本文件「数据库迁移与初始化」 |
| 9 | 添加默认管理员初始化步骤 | README 必备章节 8；本文件「数据库迁移与初始化」 |
| 10 | 添加 API 文档入口 | README 必备章节 11；`api.md` |
| 11 | 添加上传流程说明 | README 必备章节 12；`storage.md` |
| 12 | 添加断点续传流程说明 | README 必备章节 12；`storage.md` |
| 13 | 添加权限模型说明 | README 必备章节 13；`permissions.md` |
| 14 | 添加异常恢复说明 | README 内容准确性要求；`recovery.md` |
| 15 | 添加已知限制 | README 必备章节 19 |
| 16 | 添加性能测试结果，不能伪造 | README 必备章节 16；`performance.md` |
| 17 | 添加截图或页面录屏 | README 必备章节 2 |
| 18 | 添加 LICENSE | 本节产物表 |
| 19 | 添加 CONTRIBUTING.md | 本节产物表 |
| 20 | 添加 CODE_OF_CONDUCT.md | 本节产物表 |
| 21 | 添加 SECURITY.md | 本节产物表 |
| 22 | 添加 CHANGELOG.md | 本节产物表 |
| 23 | 检查 `.gitignore` | 本节产物表、发布前检查清单 |
| 24 | 检查敏感信息 | 本节产物表、发布前检查清单 |
| 25 | 检查所有 TODO 是否有明确说明 | 本节产物表、发布前检查清单 |
| 26 | 检查 CI 是否能够通过 | 本节产物表、发布前检查清单 |
| 27 | 执行完整构建和测试 | 本节产物表、发布前检查清单 |
| 28 | 生成 docs/release-checklist.md | 本节产物表 |

发布内容必须与仓库实际状态一致：

- 不声称已经实现未完成或未验证的功能。
- 不提交密钥、密码、Token、数据库转储和用户文件。
- 不伪造截图、压测结果或测试结果。
- README 中所有命令必须与实际项目结构一致。
- 检查 `.gitignore`。
- 检查敏感信息。
- 检查所有 TODO 是否有明确说明。
- 检查 CI 是否能够通过。

---

## README 要求

README 必须能够让陌生用户在不阅读源码的情况下启动项目。

### 必备章节

1. 项目介绍。
2. 截图或 GIF。
3. 功能列表。
4. 系统架构图。
5. 技术栈。
6. 环境要求。
7. Docker 启动命令。
8. 默认管理员初始化方式。
9. 数据库迁移方式。
10. 前端开发方式。
11. API 示例。
12. 上传协议说明。
13. 权限模型。
14. 文件存储目录说明。
15. 测试命令。
16. 压测命令。
17. 安全说明。
18. 数据备份说明。
19. 已知限制。
20. 后续路线图。
21. License。

### 内容准确性要求

- 不能把「目前未实现」写成「支持」。
- 不能把「未验证」写成「已验证」。
- 补充以下内容并在 README 中保持与仓库结构一致：
  - 项目功能概览（对应功能列表）。
  - 系统架构图。
  - 目录结构说明。
  - 本地启动步骤。
  - Docker 启动步骤。
  - 前端启动步骤。
  - 数据库迁移步骤。
  - 默认管理员初始化步骤。
  - API 文档入口（指向 `api.md`）。
  - 上传流程说明。
  - 断点续传流程说明。
  - 权限模型说明。
  - 异常恢复说明。
  - 已知限制。
  - 性能测试结果，不能伪造；未能运行的压测标注「未验证」。
  - 截图或页面录屏，不得伪造。
  - 性能测试环境说明。

---

## 发布前检查清单

### 部署验证

- [ ] 全新环境可以启动。
- [ ] 重启容器后数据仍然存在。
- [ ] 删除并重新创建 backend 容器后文件仍然存在。
- [ ] PostgreSQL 与 Redis 健康检查生效。
- [ ] 后端容器以非 root 用户运行。
- [ ] 镜像为多阶段构建且已瘦身。

### 备份与恢复

- [ ] 备份脚本可执行。
- [ ] 数据库恢复脚本可执行。
- [ ] 文件对象目录备份说明已写入文档。

### 文档与产物

- [ ] `README.md` 覆盖全部 21 项必备章节。
- [ ] `docs/deployment.md` 已更新。
- [ ] `LICENSE`、`CONTRIBUTING.md`、`CODE_OF_CONDUCT.md`、`SECURITY.md`、`CHANGELOG.md` 已添加。
- [ ] `docs/release-checklist.md` 已生成。
- [ ] README 中所有命令与实际项目结构一致。
- [ ] 无法验证的生产部署能力已标注为未验证。

### 仓库卫生

- [ ] `.gitignore` 已检查，`.env` 已被忽略。
- [ ] 敏感信息已检查，仓库内无密钥、密码、Token、数据库转储、用户文件。
- [ ] 所有 TODO 都有明确说明。
- [ ] CI 能够通过。
- [ ] 完整构建和测试已执行。

---

## 相关文档

- 备份恢复与故障处置：`recovery.md`
- 配置项对应的安全要求：`security.md`
- 部署后的验收与压测：`testing.md`
- 测试环境与性能结果：`performance.md`
- 对象存储目录结构：`storage.md`


## 监控与指标（阶段十三）

服务暴露 `GET /metrics`（Prometheus 文本格式，无认证、只含聚合数字）。
不要把它直接暴露到公网：用反向代理限制来源，或按下面的最小示例只允许监控主机访问。

```yaml
# docker-compose.yml 里给 backend 加标签，Prometheus 走服务名抓取
scrape_configs:
  - job_name: lan-file-hub
    metrics_path: /metrics
    static_configs:
      - targets: ["backend:8080"]
```

关键指标与用途：

| 指标 | 用途 |
|---|---|
| `lan_file_hub_http_request_duration_seconds` | 接口延迟分位（直方图，按 method + 路由模板） |
| `lan_file_hub_http_requests_total{status}` | 错误率（5xx / 4xx 分开看） |
| `lan_file_hub_errors_total{code}` | 按错误码定位问题（错误码取自 `api.md` 的封闭表） |
| `lan_file_hub_active_uploads` | 并发上传会话数 |
| `lan_file_hub_storage_free_bytes` / `_used_bytes` | 磁盘水位与占用趋势 |
| `lan_file_hub_background_jobs_total{result}` | 后台清理任务的删除/跳过/失败计数 |
| `lan_file_hub_health_status{dependency}` | `postgresql` / `redis` / `object_storage` 是否可用（0/1） |
| `lan_file_hub_metrics_dropped_series_total` | 大于 0 说明标签基数失控，需要检查调用点 |

`/health/live` 用于存活探针，`/health/ready` 用于就绪探针（依赖不可用时返回 503）。


## 阶段十五落地记录（2026-09-12）

本节记录容器化与备份恢复的**实际实现与实测结果**；上面各节是要求，这里是落地情况。

### 服务编排（第 4～8 条）

`docker compose config` 校验通过，共 5 个服务：

| 服务 | 作用 | 依赖与健康检查 |
|---|---|---|
| `postgres` | 数据库 | `pg_isready` 健康检查；卷 `postgres_data`；端口只绑 `127.0.0.1` |
| `redis` | 缓存/临时状态 | `redis-cli ping` 健康检查；卷 `redis_data`；端口只绑 `127.0.0.1` |
| `migrate` | **一次性**执行数据库迁移 | `depends_on: postgres(service_healthy)`；成功退出后 backend 才启动 |
| `backend` | 后端服务 | `depends_on: postgres/redis(service_healthy) + migrate(service_completed_successfully)`；卷 `storage_data`；镜像内 `HEALTHCHECK /health/live` |
| `frontend` | Nginx 托管前端产物 | `wget` 健康检查 |
| `init-admin` | 初始化第一个管理员 | `profiles: [admin]`，只在显式指定时运行 |

**应用进程不改 schema**：迁移由独立服务/工具执行，失败即非零退出，backend 不会带病启动。

### 数据卷（第 8 条）

| 卷 | 容器内路径 | 内容 |
|---|---|---|
| `postgres_data` | `/var/lib/postgresql/data` | 数据库 |
| `redis_data` | `/data` | AOF 持久化的 Redis 数据 |
| `storage_data` | `/data/storage` | **对象存储**（内容寻址的 `objects/` 与 `tmp/`） |

本机开发（不用容器跑后端）时对象目录是项目内的 `storage/`，两个备份脚本都会自动识别。

### 数据库迁移（第 9 条）

```powershell
.\scripts\migrate.ps1                 # 用本地构建的迁移工具（开发）
.\scripts\migrate.ps1 -InContainer    # docker compose run --rm migrate（生产路径）
```

镜像内已经带上 `lan_file_hub_migrate` 与 `migrations/`，生产不需要把源码挂进容器。

### 初始化管理员（第 10 条）

```powershell
# 本机最简单：交互式，口令不回显（桌面菜单里就是「5 创建管理员」）
.\scripts\create-admin.ps1
.\scripts\create-admin.ps1 -Username admin -Email admin@example.com

# 容器路径（口令从 .env 的 INITIAL_ADMIN_PASSWORD 读）
docker compose --profile admin run --rm init-admin

# 本地路径（等价的手工方式）
$env:INITIAL_ADMIN_PASSWORD = '<至少 8 位>'
.\scripts\migrate.ps1 -CreateAdmin alice -AdminEmail alice@example.com
```

- 口令**只从环境变量读**（不落命令行历史、不进仓库）；未设置或过短直接失败退出（实测退出码 1）。
- 已存在同名用户时提升为 ADMIN（不改口令）；已是 ADMIN 时幂等跳过（实测通过）。
- 新建用户写入 Argon2id 哈希（实测库里 `password_hash LIKE '$argon2id$%'` 为真）。
- 这是**引导口令**，登录后应立即修改。

### 备份与恢复（第 11～13 条）

```powershell
.\scripts\backup.ps1                                  # 备份到 backups\<时间戳>\
.\scripts\backup.ps1 -OutDir D:\backups -SkipStorage  # 只备份数据库
.\scripts\restore.ps1 -BackupDir backups\20260912-161146          # 演练：恢复到临时库
.\scripts\restore.ps1 -BackupDir backups\20260912-161146 -Force   # 就地覆盖（破坏性）
```

备份产物：`database.dump`（`pg_dump -Fc`）、`storage.tar.gz`（对象存储归档）、`manifest.json`
（大小、**SHA-256**、迁移版本、关键表行数）。恢复前会先核对 SHA-256，不一致直接拒绝。

设计取舍：**默认不覆盖现有数据**——恢复演练恢复到临时库 `lan_file_hub_restore_check`，
核对完自行 DROP；就地恢复必须显式 `-Force`，且脚本会**先自动备份当前状态**再动手。
对象存储采用"先解到临时目录再替换"，避免解压中途失败留下半个存储。

本机实测：备份 → 演练恢复，临时库得到 11 张表、8 个迁移版本、行数与 `manifest.json` 记录一致；
对象归档里的标记文件能被正确解出（`storage-staging/objects/zz/backup-marker.txt`）。

### 反向代理与 HTTPS（第 16、17 条）

前端镜像已是 Nginx，生产建议在前面再加一层反代终止 TLS。示例（`deploy/nginx.conf.example`）：

```nginx
server {
  listen 80;
  server_name files.example.com;
  return 301 https://$host$request_uri;          # HTTP 一律跳 HTTPS
}

server {
  listen 443 ssl;
  server_name files.example.com;
  ssl_certificate     /etc/nginx/certs/fullchain.pem;
  ssl_certificate_key /etc/nginx/certs/privkey.pem;

  # 分块上传：单个分块最大 64 MiB，反代必须放行
  client_max_body_size 128m;
  proxy_read_timeout   300s;
  proxy_request_buffering off;                    # 大文件边收边转，避免落临时文件

  location /api/ {
    proxy_pass http://backend:8080;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
  }

  location = /metrics {                           # 指标只给监控来源
    allow 10.0.0.0/8;
    deny all;
    proxy_pass http://backend:8080/metrics;
  }

  location / {
    proxy_pass http://frontend:80;
  }
}
```

- 后端会读 `X-Forwarded-For` / `X-Real-IP` 做限流分维度，反代必须设置这两个头；
- `X-Forwarded-Proto: https` + 后端 `isOnSecureConnection()` 决定是否下发 HSTS；
- 本服务不自建 TLS（见 `security.md` §8），HTTPS 一律由反代终止。

### 环境变量与生产安全（第 14、15 条）

完整清单见 `.env.example` 与本文「配置项清单」。生产必须做到：

- [ ] `JWT_SECRET` 设置为**不少于 32 字符**的随机串（生产环境为空会拒绝启动）；
- [ ] 修改 `POSTGRES_PASSWORD`（`lan_file_hub_dev` 只是本机开发默认值）；
- [ ] `INITIAL_ADMIN_PASSWORD` 用一次性强口令，创建后立刻改掉；
- [ ] `.env` 不进仓库（`.gitignore` 已包含 `.env` 与 `.env.*`）；
- [ ] 数据库与 Redis 端口只监听 `127.0.0.1`（compose 已如此配置）；
- [ ] `/metrics` 只对监控来源开放（反代白名单）；
- [ ] 容器以非 root 运行（镜像内 `USER lanfilehub`，uid 10001）；
- [ ] 用反代终止 TLS，并确认响应头里有 HSTS（仅 HTTPS 下会下发）。

### 本机验证结果（第 18～20 条）

| 项 | 结果 |
|---|---|
| `docker compose config` | 通过（5 个服务 + 依赖条件） |
| 依赖服务全新启动 | `docker compose up -d postgres redis` 后健康检查通过，迁移可执行 |
| **重启容器后数据仍在** | `docker compose restart postgres` 前后 `schema_migrations` 都是 8 行 ✅ |
| 备份/恢复演练 | 备份 → 临时库恢复 → 表/迁移/行数与清单一致，对象归档可解 ✅ |
| 删除并重建 backend 容器后文件仍在 | 见下方说明 |

**backend 镜像构建**：`docker compose build backend` 需要在 Linux 容器里从源码编译整套 C++
依赖（Drogon/OpenSSL/libpqxx…），耗时很长；本阶段已启动该构建并把 `.dockerignore` 补齐
（此前构建上下文会把本地 `build/` 与 `vcpkg_installed/` 一起送进 daemon）。
**若该构建未在本机跑完，则「删除并重新创建 backend 容器后文件仍在」属于未验证**，
不得当成已验证；对象存储落在 `storage_data` 卷上，这一点由 compose 定义与上表的卷持久化测试保证。