# 阶段任务书

本目录是项目实施的**唯一阶段编号权威**：阶段零 ～ 阶段十七，共 18 个。

原 `AGENTS.md` §39 曾另有一套"阶段一～九"的划分，与这里的编号和内容都对不上，已废弃
（裁决记录见 `../plan.md` §5）。执行时一律以本目录为准。

## 执行方式

1. 一次会话只执行一个阶段。
2. 开工前先读该阶段任务书、项目根 `AGENTS.md`、以及该阶段涉及的 `docs/` 文档。
3. 阶段结束时执行任务书里的验收命令，并给出真实输出。
4. 前一阶段的验收跑通后再进入下一阶段；遗留的未验证项必须显式带进下一阶段报告。
5. 每完成一个阶段，更新下表的状态列。

## 阶段索引

| # | 文件 | 目标 | 分组 | 状态 |
|---|---|---|---|---|
| 0 | `00-audit-and-plan.md` | 审计现有工程与环境，产出 `docs/implementation-status.md`，拆分可独立验证的阶段。**不实现业务功能** | MVP | ✅ 已完成（2026-09-12） |
| 1 | `01-infrastructure.md` | C++20 CMake 工程、目录骨架、构建预设、clang-format/clang-tidy、Sanitizer、配置模块、spdlog 日志、request_id、健康检查、Dockerfile 与 compose、最小 GoogleTest、CI | MVP | ✅ 完成（2026-09-12），验收项全部通过 |
| 2 | `02-auth.md` | users 表、注册、登录、密码哈希、认证中间件、`/auth/me`、ADMIN/USER、ACTIVE/DISABLED、统一错误码、前端登录注册页 | MVP | ✅ 完成（2026-09-12） |
| 3 | `03-folders.md` | folders 表、用户根目录、创建/重命名/移动/软删除、子目录接口、名称校验与同名冲突、根目录保护、路径穿越防护、基础权限检查 | MVP | ✅ 完成（2026-09-12） |
| 4 | `04-object-storage-and-transfer.md` | ObjectStorage 抽象与本地实现、files/file_versions 表、单文件上传、流式下载、服务端 SHA-256、基础权限、Range 下载、前端文件列表与上传进度 | MVP | ✅ 完成（2026-09-12） |
| 5 | `05-resumable-chunked-upload.md` | 上传会话与分块表、初始化/分块/状态查询/完成/取消、重复分块幂等、真正的断点续传、完成时服务端重算哈希、并发限制 | MVP | ✅ 完成（2026-09-12），E2E 54/54 断言通过 |
| 6 | `06-dedup-and-quota.md` | 按 SHA-256 去重与安全引用、用户配额字段与统计、初始化与完成两阶段检查配额、防止并发绕过配额、引用计数与清理策略 | MVP | ✅ 完成（2026-09-12），E2E 29/29 断言通过 |
| 7 | `07-versions-and-conflicts.md` | 文件版本表与版本创建、历史版本查询与下载、恢复历史版本、`If-Match` 与 409 冲突、版本冲突测试、前端版本历史页 | MVP | ✅ 完成（2026-09-12），E2E 48/48 断言通过 |
| 8 | `08-trash-and-restore.md` | 文件与文件夹软删除、回收站列表、恢复（含父目录不存在与同名冲突）、永久删除、过期清理与异步删除重试、审计日志、前端回收站页 | MVP | ✅ 完成（2026-09-12），E2E 42/42 断言通过 |
| 9 | `09-permissions-and-shares.md` | permissions 表、READ/WRITE/OWNER 授权与继承、后端权限检查、分享链接（只读、过期、撤销、次数限制、只存 Token 哈希）、公开查看与下载、前端权限与分享页 | MVP | ✅ 完成（2026-09-12），E2E 54/54 断言通过 |
| 10 | `10-frontend.md` | 完整前端：13 个页面、20 项交互、页面状态与上传状态分离、TanStack Query + IndexedDB、错误提示与破坏性操作确认、Vitest 与 Playwright | MVP | ✅ 完成（2026-09-12）：前端 107 用例通过、构建成功；Playwright 用例已写但因浏览器二进制未安装**未执行** |
| 11 | `11-background-jobs.md` | 后台 Worker 与任务队列、过期会话与临时分块清理、永久删除任务、对象与数据库不一致扫描、重试与指数退避、死信、优雅退出与启动恢复 | 发布增强 | ✅ 完成（2026-09-12）：后台任务框架、过期会话/临时分块清理、孤儿临时文件、对象-库一致性扫描、存储统计校准、启动恢复均已完成并有测试（221/221）；**死信的人工重试入口按任务书归属阶段十三**；真实进程内验证见 `implementation-status.md` §23.1f |
| 12 | `12-rate-limit-and-hardening.md` | 登录与上传限流、下载限速、请求体与文件大小限制、磁盘空间检查、路径穿越与文件名净化、CORS、日志脱敏、安全响应头、10 个必须覆盖的安全场景 | 发布增强 | ✅ 完成（2026-09-12）：22 条逐条有结论（磁盘预检、登录/上传限流、请求体上限、非法 ID 错误码、下载限速、CORS 与安全响应头、日志脱敏闸门、`FileScanner`+`NoopFileScanner`、安全文档）；后端 264/264，真实 HTTP 验收 47/47；发现并修复三个真实缺陷（见 `implementation-status.md` §24） |
| 13 | `13-observability-and-admin.md` | 结构化日志与 request_id、Prometheus 指标、健康检查指标、管理员用户与配额管理、系统存储统计、审计日志查询、前端管理员页 | 发布增强 | ✅ 完成（2026-09-12）：`/metrics` 与指标注册表、HTTP 请求数/耗时/错误码、上传下载与存储/健康/后台任务指标、管理员 API（用户列表/启停/配额/存储统计/审计查询）+ 越权测试、前端系统管理页；后端 284/284、前端 111/111、真实 HTTP 验收 77/77（见 `implementation-status.md` §25） |
| 14 | `14-performance-and-fault-tests.md` | 单元/PG/Redis/REST/端到端测试、并发与故障测试、TSan/ASan/UBSan、Google Benchmark、吞吐与延迟测量、`docs/performance.md` | 发布增强 | ✅ 完成（2026-09-12）：Google Benchmark 微基准（哈希 2.17 GiB/s、落盘 1.66 GiB/s）、`scripts/bench.ps1` 压测（上传 23.2 / 下载 238.9 MiB/s、P95 29 ms、失败率 0/200、内存不随文件增长）、Playwright 首次真实执行 2/2、故障注入 3 项、ASan 接入独立 triplet 并跑通 **287/287**；**TSan/UBSan 因 MSVC 不支持记为未验证**（见 `implementation-status.md` §26 与 `docs/performance.md`） |
| 15 | `15-docker-and-backup.md` | 多阶段构建与镜像瘦身、非 root 运行、compose 健康检查与数据卷、迁移与初始化管理员、备份与恢复脚本、反代与 HTTPS、验证数据持久性 | 发布增强 | 未开始 |
| 16 | `16-github-release.md` | README 完善、架构图与启动步骤、LICENSE/CONTRIBUTING/SECURITY/CHANGELOG、敏感信息检查、CI 通过、`docs/release-checklist.md` | 发布增强 | 未开始 |
| 17 | `17-final-review.md` | 30 项代码审查与发布验收，输出按严重程度排序的问题清单、通过/未通过项、未验证事项。**不为了通过而删测试或降低安全检查** | 发布增强 | 未开始 |

## MVP 冻结边界

阶段零～十完成后即形成一个可发布的 MVP，包含：

```text
用户认证            文件夹管理          单文件上传下载
分块上传            断点续传            SHA-256 校验
文件去重            文件版本            回收站
资源权限            分享链接            React 文件管理界面
Docker Compose      基础测试
```

阶段十一～十七属于可靠性、运维和发布增强，**不阻塞最初的产品演示**，
但正式发布前必须完成其中必要的安全测试与恢复测试。

冻结边界的意义是防止范围一次性扩张：在阶段十完成前，不得提前引入十一～十七的内容。
