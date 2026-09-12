# 变更记录

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)；
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [未发布]

### 计划中（阶段十六～十七）

- GitHub 发布准备：LICENSE 类型待定（见 `AGENTS.md` 待确认事项）、Release 产物与校验和
- 最终 30 项复核与交付报告

## [0.1.0] - 2026-09-12

阶段零～十五完成的第一个可用版本。自用起步，按工业级标准实现。

### 新增

- **认证与用户**：注册、登录、登出、当前用户；Argon2id 口令哈希、短期 JWT + 统一认证中间件、
  被禁用用户立即失效（403 FORBIDDEN）。
- **文件与目录**：文件夹树（根目录幂等创建）、重命名/移动、同名冲突检测、
  列表分页与排序白名单、按名称过滤。
- **上传**：分块上传（默认 8 MiB，1–64 MiB 可配）、真正可恢复的断点续传（服务端记录已收分块）、
  客户端与服务端双端 SHA-256 校验、内容寻址去重、临时分块与合并、失败路径统一清理。
- **下载**：流式（零拷贝）与 Range 断点续传、`Content-Disposition` RFC 6266 编码、
  下载限速（可选，字节级令牌桶 + 断线取消）。
- **版本与回收站**：文件多版本、`If-Match` 乐观并发、版本回滚、软删除 + 回收站恢复、
  异步永久删除（带重试与指数退避）。
- **权限与分享**：READ/WRITE/OWNER 三级 + 管理员，目录继承取最高级；
  分享链接（只存 Token 哈希、可过期/撤销/限次、统一 404 防探测）；管理员访问留审计。
- **可靠性**：后台清理线程（过期会话与临时分块、孤儿对象、对象-库一致性扫描、
  存储统计校准、启动恢复重排队列）、优雅退出、`MIN_FREE_DISK_BYTES` 磁盘预检。
- **安全加固**：登录（账号 + IP 双维度）与上传限流、请求体上限、CORS 白名单与安全响应头、
  日志 sink 层脱敏、SQL 全参数化、`FileScanner` 接口与 `NoopFileScanner`（明确不查毒）。
- **可观测性**：Prometheus `/metrics`（HTTP/上传/下载/存储/后台任务/健康指标，标签基数有界）、
  结构化日志带 `request_id`、`/health/live` 与 `/health/ready`。
- **管理员功能**：用户列表与搜索、启用/禁用、改配额、系统存储统计、审计日志查询（含查询留痕）、
  前端系统管理页。
- **前端**：登录注册、文件列表/网格、上传面板（分块与重试）、版本历史、回收站、
  分享与权限弹窗、分享给我的、公开分享落地页、系统管理页；Vitest 单测 + Playwright 端到端。
- **部署**：多阶段 Dockerfile（非 root）、docker compose（依赖健康检查、迁移服务、
  数据卷、初始化管理员）、备份与恢复脚本（SHA-256 清单、默认演练模式）、
  反向代理与 HTTPS 示例、Prometheus 抓取配置。

### 已知限制

- **没有真实病毒扫描**（只有接口与空实现）；**TSan/UBSan 未运行**（MSVC 不提供，
  依赖为 MSVC 静态库无法混用 clang）。
- 没有多实例/负载均衡、长稳压测与真实局域网链路数据；性能数字与限制见 `docs/performance.md`。
- 前端 Token 存 sessionStorage，生产建议改 HttpOnly Cookie（需配 HTTPS）。
- 后端镜像需要在 Linux 容器内编译整套 C++ 依赖，首次构建耗时很长。

[未发布]: https://github.com/klaraljy/lan-file-hub/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/klaraljy/lan-file-hub/releases/tag/v0.1.0
