# 安全策略

## 支持范围

这是一个**自用/中小团队内网**部署的文件服务。安全修复只保证针对**默认分支最新提交**；
历史版本不单独维护。

## 报告漏洞

请**不要**开公开 issue。私下报告渠道（任一即可）：

1. GitHub 私有漏洞报告（仓库 → Security → Report a vulnerability）；
2. 直接联系仓库所有者（`klaraljy`）。

请在报告里给出：影响范围、复现步骤或 PoC、受影响的提交、你评估的严重程度。
会在确认后尽快回复；修复前请不要公开细节。

## 设计上的已知边界（不是漏洞，但请知悉）

- **没有真实病毒扫描**：只有 `FileScanner` 接口与 `NoopFileScanner`（明确返回"未扫描"）。
  上传的文件**不会**被查毒，不要把它当成安全边界。
- **没有端到端加密**：服务端持有明文对象；数据库与对象存储由部署者自行保护。
- **Token 存在 sessionStorage**（前端）：生产部署前建议改为 HttpOnly Cookie，
  并把 HTTPS 交给反向代理 —— 见 `docs/deployment.md`。
- **`/metrics` 不做认证**：只暴露聚合数字，但生产环境必须用反向代理限制来源。
- **限流是单实例内存态**：多实例部署时限流不共享（本项目当前定位是单实例）。

## 部署者必做

- `JWT_SECRET` 用不少于 32 字符的随机串；`POSTGRES_PASSWORD` 不要用示例值；
- 用反向代理终止 TLS，并确认响应头里有 HSTS（仅 HTTPS 下会下发）；
- 数据库与 Redis 端口不要暴露到公网（compose 默认只绑 `127.0.0.1`）；
- 容器以非 root 运行（镜像内已是 `USER lanfilehub`）；
- 定期备份并**验证恢复**：`scripts/backup.ps1` / `scripts/restore.ps1`（默认演练模式不覆盖数据）。

## 已实现的安全控制

登录与上传限流、请求体上限（JSON 256 KiB / 框架级最大分块 + 1 MiB）、路径穿越防护、
文件名净化与 `Content-Disposition` 编码、Argon2id 口令哈希、分享 Token 只存哈希、
统一 404 避免探测、CORS 白名单（禁止 `*`）、安全响应头、日志 sink 层脱敏、
管理员操作审计（含"谁查询过审计日志"）。逐条结论见 `docs/implementation-status.md` 与
`docs/security.md`。
