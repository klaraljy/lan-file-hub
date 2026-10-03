# 安全要求

本文件列出系统必须实现的安全条款，以及发布前必须覆盖的攻击场景；发布前的安全验收标准见 [testing.md](testing.md)。规则红线见项目根 `AGENTS.md` 第 8 节，全局密钥与隐私规则见 `E:\dsh\home\AGENTS.md` 第 6 节。

安全目标：

- 未认证用户不能访问任何受保护资源。
- 已登录用户不能读取、修改、删除自己无权访问的资源。
- 任何人不能通过构造请求读取或覆盖存储根目录之外的文件。
- 分享链接不能越权访问其他资源，过期或被撤销后立即失效。
- 数据不出现在它能被利用的地方：数据库里没有明文口令与明文分享 Token，日志与错误响应里没有敏感信息。
- 暴力破解与资源滥用被限流拦住：登录、上传、下载、请求体大小、单文件大小、用户配额、磁盘剩余空间都有上限。

## 1. 必须实现的安全条目

以下条目全部必须实现：

- 密码哈希。
- JWT 或安全 Session。
- 所有受保护 API 校验身份。
- 所有资源 API 校验权限。
- 登录失败限流。
- 上传文件名过滤。
- 路径穿越防护。
- 请求体大小限制。
- 分享 Token 使用高强度随机值。
- 数据库参数化查询。
- 防止任意文件读取。
- 防止任意文件覆盖。
- 防止越权下载。
- 敏感信息不写日志。
- CORS 使用明确白名单。
- 生产环境支持 HTTPS 配置。
- 下载响应设置合理的 `Content-Disposition`。
- 不直接执行上传文件。

关键区分：**“所有受保护 API 校验身份”和“所有资源 API 校验权限”是两条独立要求**。前者只回答“请求来自哪个用户”，后者回答“这个用户能不能对这个资源做这件事”，两者都不能省略。

## 2. 认证与会话安全

- 密码哈希用 Argon2id；禁止明文保存密码，注册、登录、修改密码全流程都不得落明文。
- 认证采用短期 JWT + 统一认证中间件；受保护接口统一经过该中间件，不允许个别接口自行解析 Token。
- `JWT_SECRET` 不得写入源码，也不得进日志或仓库；Secret 通过环境变量配置。
- Token 过期后返回 `TOKEN_EXPIRED`。
- Token 不能写入日志。
- 关键操作时必须复查用户状态：用户被禁用后，后端不能只依赖 Token 过期来拦截。被禁用用户不得登录，也不得访问受保护接口。
- 前端不得将密码保存到 LocalStorage。
- Token 处理方式必须统一，避免多个页面分别实现。
- 最终报告必须说明 Token 的保存和失效策略。

## 3. 授权与越权防护

- 所有权限必须在服务端检查；权限检查点清单见 `permissions.md`。
- **不能用前端隐藏按钮代替后端权限检查。前端隐藏按钮不是权限控制。**
- 不能以“前端不会发送非法请求”为安全依据：任何状态、任何参数都必须假定可以由任意客户端直接构造。
- 非所有者不能随意修改权限。
- 普通用户不能下载其他用户无权访问的文件。
- 普通用户不能访问其他用户的上传会话，包括使用他人 `upload_id` 上传分块或完成上传。
- 分享链接只对它绑定的资源生效，防止分享链接越权访问其他资源。
- 管理员拥有全局管理权限，但管理员访问用户资源时必须记录审计日志；管理员不能在没有审计记录的情况下静默读取用户文件，管理员操作必须可追溯。
- 权限继承规则必须写入文档（见 `permissions.md`）；删除文件夹前必须处理继承关系。

## 4. 路径与文件名安全

- 实现路径穿越防护：拒绝 `..`、绝对路径和非法控制字符；必要时使用规范化绝对路径后再次检查目标路径仍在存储根目录内。
- 业务层不得直接用 `std::filesystem` 访问用户路径，文件内容访问一律经过对象存储接口。
- 用户的原始文件名只作元数据，永远不参与物理路径拼接；`storage_key` 不能使用用户输入的原始文件名直接生成。
- 实现上传文件名过滤；文件名和 `Content-Disposition` 都要做安全处理。
- 下载响应设置合理的 `Content-Disposition`；下载名称默认使用元数据中的原始文件名。
- 防止任意文件读取，防止任意文件覆盖：上传文件不能覆盖服务器任意路径。
- 不直接执行上传文件。

## 5. 上传与请求限制

- 请求体大小限制。
- 单文件大小限制。
- 分块上传并发限制。
- 用户存储配额限制，上传超出配额的文件必须被拒绝。
- 磁盘剩余空间检查；上传导致磁盘空间不足时必须被拦截而不是写坏存储。
- 非法请求不能导致服务崩溃；超限请求返回明确错误，不得让进程异常退出。

**阶段十二已落地的具体上限**（每条都有测试或验收脚本覆盖）：

| 限制 | 实现 | 取值 |
|---|---|---|
| 请求体大小 | 框架级 `setClientMaxBodySize`（Drogon 在读取请求体之前拒绝，413）；JSON 接口再走 `ParseJsonBody`（256 KiB） | 最大分块 + 1 MiB / 256 KiB |
| 单文件大小 | `MAX_FILE_SIZE`，上传会话内逐块累计检查 | 默认 10 GiB |
| 分块大小 | `chunk_size` 范围校验（`kMinChunkSize`–`kMaxChunkSize`） | 1–64 MiB |
| 分块并发 | 全局与单用户会话上限 | 阶段五 |
| 用户配额 | 事务内强制，并发不可绕过 | 默认 20 GiB |
| 磁盘剩余空间 | `MIN_FREE_DISK_BYTES`：创建会话时按声明大小预检、合并前再次检查 | 默认 1 GiB |
| 非法请求 | 格式错误的 ID → `400 INVALID_ARGUMENT`（不再误报 `503`）；超限 → `413 PAYLOAD_TOO_LARGE`；服务保持存活 | 集成测试 + 验收脚本场景 E/F |

> 注意：`setClientMaxMemoryBodySize` 只决定"超过就落临时文件"，**不是**上限；
> 上限必须由 `setClientMaxBodySize` 承担。这一点在阶段十二被真实流量验证过（16 MiB 登录请求曾被完整处理）。

## 6. 限流与下载控制

- 登录接口限流，覆盖重复提交登录请求的暴力破解场景。
- 上传接口限流。
- 下载限速作为下载控制的一部分：下载限速的实现要求（用户级或请求级、令牌桶或基于时间的策略等）见 `storage.md`；本节只约束它是必须存在的安全控制，不能因为“带宽够用”而不实现。
- 分享链接随机性、过期校验、撤销校验都必须完善。

**阶段十二已落地**：登录限流（账号维度 `ip|identifier` 与来源 IP 维度双保险，触发时 `429 RATE_LIMITED` + `Retry-After`，
登录成功即重置该账号窗口）与创建上传会话限流（按用户）。被拒绝的尝试不计入窗口，避免把限流变成"永久封禁"；
桶数量有上限，防止用海量 key 把限流器自身打成内存热点。

**下载限速**（`DOWNLOAD_RATE_LIMIT`，默认 `0` = 不限速）：按用户做字节级令牌桶（公开下载按来源 IP），
突发额度 1 秒；令牌不够时用事件循环定时器排队，**不 sleep 阻塞工作线程**。
开启限速后下载走 chunked 传输（拿不到 `Content-Length`），因此默认关闭、默认路径仍是零拷贝 + `Content-Length` + Range。
取消传输有两条判据：连接状态 + 「连接 10 秒内一个字节都没写进内核」的停滞看门狗——实测只信 `send()` 的返回值不行，
连接断开后它仍会返回 true（数据只是进了发送缓冲）。

## 7. 日志与错误信息安全

- 不在日志中输出密码、Token 或密码哈希。
- 敏感信息不写日志，包括密码、Token、密钥、Cookie、私钥与完整文件内容。
- 检查日志中是否泄露密码、Token、绝对路径和敏感信息。
- 错误响应不能暴露绝对路径、SQL、内部堆栈或密钥。
- 登录失败返回通用错误，避免泄露用户是否存在。
- 使用参数化数据库查询；不得将用户输入直接拼接进 SQL、排序字段、数据库表名或文件路径。
- 分享 Token 明文只在创建时返回一次，不写日志、不回显在其他接口里。

**阶段十二复核结果**：

| 发现 | 处理 |
|---|---|
| 脱敏原本是**逐调用点** opt-in 的（`RedactWithContext`），漏一个调用点就会把口令写进日志 | 在 spdlog **sink 层**加了最后一道闸：`MakeRedactingSink` 在写盘前用配置里的 secrets 替换敏感串；调用点仍然各自脱敏，两层都要有 |
| 新写的限速下载把**对象绝对路径**写进了错误日志（违反"不得记录绝对路径"） | 改成记录对象 Key（内容寻址 Key 不含用户路径）；控制器把 `storage_key` 传进来 |
| 公开分享下载记录了 `share_token_prefix`（Token 前 6 位） | 改成记录 `share_ref`＝Token 的 SHA-256 前 8 位：可跨日志关联，且不可反推 |
| 登录日志记录 username / email / IP | 保留：这是必要的安全审计信息（爆破排查），且不含凭据；已在此处显式记录该取舍 |
| 数据库异常文本（含 SQL 与连接串）进的是 `Error.detail` | `detail` 只进日志、不进响应体（`error_response.cpp` 有注释与实现约束），且日志层已脱敏 |

测试：`logging_redaction_test.cpp`（4 项，含"调用点忘了脱敏也不会泄露"）。

## 8. CORS、响应头与 HTTPS

- CORS 使用明确白名单，不允许用 `*`；允许的来源通过配置提供。
- 增加安全响应头。
- 生产环境支持 HTTPS 配置。

**阶段十二已落地**：

- `CORS_ALLOWED_ORIGINS` 白名单在配置层校验：空列表与 `*` 都直接拒绝启动（`config.cpp`）。
- CORS 头在 `api/security_headers.cpp` 里统一处理：预检 `OPTIONS` 在**路由之前**应答（204 + 允许的方法/头，`Max-Age=600`），
  避免落到未注册的 OPTIONS 路由上变成 404；只有命中白名单的 `Origin` 才回 `Access-Control-Allow-Origin`，绝不回通配；
  非白名单来源的预检直接 403，普通请求不回 CORS 头（由浏览器拦截，不泄露白名单内容）；响应始终带 `Vary: Origin` 防止缓存串站。
- 安全响应头（所有响应）：`X-Content-Type-Options: nosniff`、`X-Frame-Options: DENY`、`Referrer-Policy: no-referrer`、
  `X-XSS-Protection: 0`、`Permissions-Policy: geolocation=(), microphone=(), camera=()`。
- **CSP 按请求路径分两套**（`BuildContentSecurityPolicy()`，2026-10-02 修）：
  - 数据接口（`/api`、`/health`）：`default-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`；
  - **网页**（后端自托管前端时的 `/`、`/assets/*`、`/favicon.ico` 与 SPA 路由）：`default-src 'self'; script-src 'self';
    style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self' data:; connect-src 'self';
    object-src 'none'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'`。
    只放行**同源**资源（不引 CDN）；`style-src` 必须带 `'unsafe-inline'`，因为 Ant Design 运行时注入样式。
  - ⛔ **为什么必须分开**：给网页套 API 那套 `default-src 'none'` 会把页面自己的 `/assets/index-*.js` 与
    `/favicon.ico` 一起拦掉 —— HTTP 全是 200，浏览器里却是**白屏**（2026-10-02 用户实测踩到；
    只用状态码验证发现不了，必须真在浏览器里渲染一次）。单元测试 `SecurityHeadersTest.WebPagesGetUsableCsp`
    与 `ApiAndHealthPathsKeepStrictCsp` 守着这条边界。
- HSTS **仅在连接确实走 TLS 时**下发（明文 HTTP 下下发会把站点锁死）；本服务不自建 TLS，HTTPS 交给反向代理，见 `deployment.md`。
- 验收：`e2e-phase12.ps1` 场景 I（11 项）覆盖预检、白名单外来源、CORS 头、安全头与 HSTS 缺席。

## 9. 邀请制注册（2026-10-02，用户明确要求）

**不是所有人都能注册**：默认仍允许自助注册（个人自用），把 `ALLOW_SELF_REGISTRATION` 设成 `false`
之后，注册必须带一条管理员发的有效邀请。

- **token 只存哈希**：与分享链接同一条口径 —— 库里只有 `invites.token_hash`（SHA-256），
  明文只在 `POST /api/v1/admin/invites` 的响应里出现一次；列表接口**不返回** token。
  单元测试 `InviteTest.TokenHashIsStableAndHidesPlaintext`、集成测试
  `InviteFlowTest.StoresOnlyHashNotPlaintext` 守着这两点。
- **消耗是原子的**：一条 `UPDATE ... WHERE used_count < max_uses AND revoked_at IS NULL
  AND (expires_at IS NULL OR expires_at > now())`，并发下不会把一条邀请用超；
  注册失败（用户名被占用等）会把次数**归还**，不白吃一次。
- **错误码分开**：token 根本不存在 → `404 RESOURCE_NOT_FOUND`；过期/用完/撤销 → `400 INVITE_INVALID`。
  分开是为了不让外部靠错误码枚举"哪条邀请还存在"。
- **日志不记明文**：只记 `invite_id` 与 token 哈希前 8 位。
- 建/查/撤销邀请**只有管理员**能做（后端判角色；前端不显示按钮不算控制）。
- 参数上限：`max_uses` 1–1000、有效期 5 分钟–1 年、备注 ≤ 200 字，越界一律 `INVALID_ARGUMENT`。

实测（2026-10-02，便携实例真实 HTTP）：公开校验 200、乱填 token 404、用邀请注册 200、
**重复使用 400**、关掉自助注册后不带邀请 **403 INVITE_REQUIRED**、带邀请 200、撤销后 400、
列表不含 token；界面上"关闭自助注册"时注册按钮变灰并写明原因。

## 10. 病毒扫描现状

**本项目没有真实病毒扫描能力。** 阶段十二只落地了接口与空实现，并把钩子真正接进了上传完成路径：

```cpp
// backend/include/lan_file_hub/application/file_scanner.h
class FileScanner {
 public:
  virtual ~FileScanner() = default;
  [[nodiscard]] virtual Result<ScanResult> Scan(std::string_view storage_key,
                                                std::string_view content_hash) = 0;
  [[nodiscard]] virtual std::string_view name() const = 0;
};
```

- 本期实现是 `NoopFileScanner`，它明确返回 `kSkipped`（**不是** `kClean`）——没有扫描能力就不能假装扫过。
- 接入点在 `UploadService`：判定为 `kInfected` 时上传以 `422 FILE_REJECTED` 失败，对象不被任何文件引用，
  由后台清理任务回收；扫描器自身报错时**失败关闭**（fail-closed），不默认放行。
- 替换实现只需在 `main.cpp` 换掉 `NoopFileScanner`，业务层不用改。
- README「已知限制」已写明"上传的文件不会被查毒"，不得让使用者误以为已经查毒。

## 10. 必须覆盖的安全测试场景

以下场景必须有对应测试，发布前必须真实执行并给出输出：

| # | 场景 | 期望结果 | 覆盖位置 |
|---|---|---|---|
| 1 | 未登录读取文件。 | 拒绝。 | `auth_flow_test.cpp`；E2E 阶段五/九 |
| 2 | 用户 A 读取用户 B 的文件。 | 拒绝。 | `sharing_flow_test.cpp`、`file_flow_test.cpp` |
| 3 | 用户 A 修改用户 B 的权限。 | 拒绝。 | `sharing_flow_test.cpp` |
| 4 | 用户 A 使用用户 B 的 `upload_id`。 | 拒绝。 | `upload_flow_test.cpp`（`OtherUsersCannotTouchTheSession`） |
| 5 | 通过 `../` 读取存储根目录外文件。 | 拒绝。 | `object_storage_test.cpp`（Key 校验 + 包含性检查） |
| 6 | 使用过期分享 Token 下载文件。 | 拒绝。 | `sharing_flow_test.cpp`；E2E 阶段九 |
| 7 | 使用被撤销分享 Token 下载文件。 | 拒绝。 | `sharing_flow_test.cpp`；E2E 阶段九 |
| 8 | 重复提交登录请求。 | 被限流拦截。 | `rate_limiter_test.cpp` + E2E 阶段十二场景 A/B/C |
| 9 | 上传超出配额的文件。 | 拒绝。 | `dedup_quota_test.cpp` |
| 10 | 上传导致磁盘空间不足。 | 拒绝，不破坏已有数据。 | `dedup_quota_test.cpp`（保留空间不可满足）+ E2E 阶段十二场景 F |

发布前的完整安全验收标准见 [testing.md](testing.md)。
