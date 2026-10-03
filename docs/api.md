# API 规范

## 前缀与统一响应

API 前缀：

```text
/api/v1
```

统一成功响应：

```json
{
  "data": {},
  "request_id": "request-uuid"
}
```

统一错误响应：

```json
{
  "error": {
    "code": "FILE_NOT_FOUND",
    "message": "File not found.",
    "details": {}
  },
  "request_id": "request-uuid"
}
```

每个请求生成唯一 `request_id`，成功与错误响应都必须带上它。错误响应不得暴露绝对路径、SQL、内部堆栈或密钥（见 `../AGENTS.md` 第 7 节）。

API 必须支持：

- 分页。
- 排序白名单。
- 请求 ID。
- 统一时间格式。
- 统一错误码。
- 明确的 HTTP 状态码。

- 请求体上限：JSON 接口 **256 KiB**（超限 `413 PAYLOAD_TOO_LARGE`）；任何请求的框架上限是"最大分块 64 MiB + 1 MiB"。
- 格式错误的 ID（例如把 `not-a-uuid` 当 `folder_id`）统一返回 `400 INVALID_ARGUMENT`，不得报成 `503`——客户端错误不伪装成服务故障。

不得将用户输入直接作为 SQL 排序字段、数据库表名或文件路径。

## 错误码

错误码是封闭集合，只能使用下表的值，不得自造。

| 错误码 | 含义与触发场景 |
|---|---|
| `INVALID_ARGUMENT` | 请求参数不合法：缺失、类型错误或取值越界，例如 `page_size` 超出 1–100、`sort_by` 不在白名单、`chunk_index` 格式非法。 |
| `UNAUTHENTICATED` | 未提供凭证或凭证无效，受保护接口的身份校验未通过。 |
| `TOKEN_EXPIRED` | Token 已过期，需要重新认证。 |
| `FORBIDDEN` | 身份有效但缺少所需权限：资源权限不足、不是该上传会话的所有者、非管理员访问管理员接口。 |
| `INVITE_REQUIRED` | 该实例已关闭自助注册（`ALLOW_SELF_REGISTRATION=false`），而注册请求没有带邀请参数。返回 HTTP 403。 |
| `INVITE_INVALID` | 邀请不可用：已过期、次数用完或已被撤销。返回 HTTP 400。（**token 根本不存在**时用 `RESOURCE_NOT_FOUND`，两者分开是为了不让外部枚举"哪条邀请还在"。） |
| `RESOURCE_NOT_FOUND` | 请求的通用资源不存在，例如用户、权限记录、分享链接。 |
| `RESOURCE_CONFLICT` | 通用资源冲突，例如同一父目录下名称重复、重复创建。 |
| `FILE_NOT_FOUND` | 指定 `file_id` 的文件不存在；对象丢失时同样返回明确错误并记录日志。 |
| `FOLDER_NOT_FOUND` | 指定 `folder_id` 的文件夹不存在。 |
| `UPLOAD_NOT_FOUND` | 指定 `upload_id` 的上传会话不存在。 |
| `UPLOAD_EXPIRED` | 上传会话已过期（状态 `EXPIRED`），不能再继续上传。 |
| `UPLOAD_ABORTED` | 上传会话已被取消（状态 `ABORTED`），后续分块上传返回明确错误。 |
| `CHUNK_INVALID` | 分块请求不合法：索引越界或非法、缺少或非法的 `X-Chunk-Sha256`、请求体超过该会话声明的分块大小。 |
| `FILE_REJECTED` | 上传内容未通过安全扫描（`FileScanner` 判定为恶意）。对象不会被任何文件引用，交由后台清理任务回收。 |
| `CHUNK_CONFLICT` | 相同 `chunk_index` 已存在且内容不同（哈希不同），服务端拒绝覆盖。 |
| `UPLOAD_INCOMPLETE` | 完成上传时仍有分块缺失，会话不能进入合并。 |
| `HASH_MISMATCH` | 服务端重算的 SHA-256 与客户端声明的哈希不一致：上传状态变为 `FAILED`，不创建正式文件版本，返回该错误码。 |
| `QUOTA_EXCEEDED` | 超出用户存储配额：在创建上传会话时、上传完成合并前、创建新版本前、文件恢复时、文件复制或去重引用时检查配额。 |
| `STORAGE_FULL` | 磁盘可用空间不足：上传初始化阶段与每个分块写入前都要检查。 |
| `STORAGE_UNAVAILABLE` | 依赖不可用（如 PostgreSQL 不可用、对象存储不可用）：API 返回该错误或数据库相关错误，不得向用户返回成功响应。 |
| `FILE_VERSION_CONFLICT` | 客户端提交的版本不是当前版本：返回 HTTP 409，响应中带 `current_version_id`。 |
| `RATE_LIMITED` | 触发限流，例如登录失败限流、上传或下载限流计数；客户端应退避后重试。 |
| `PAYLOAD_TOO_LARGE` | 单个请求体过大：JSON 接口超过 256 KiB，或任何请求超过框架上限"最大分块 64 MiB + 1 MiB"。由 HTTP 层在读取请求体之前拒绝，返回 HTTP 413。 |
| `INTERNAL_ERROR` | 未预期的服务端内部错误。 |

HTTP 状态码必须明确。已明确的状态码对应关系：

- 版本冲突返回 `409 FILE_VERSION_CONFLICT`。
- 限流对应 HTTP 429（属于前端可重试错误）。
- HTTP 401、403、404、409、413、422 属于不可直接重试的错误。

## 分页与排序

所有列表接口都必须支持分页。

参数：

```text
page
page_size
sort_by
sort_order
```

服务端必须限制：

```text
1 <= page_size <= 100
```

`sort_by` 只能从白名单中选择：

```text
name
size
created_at
updated_at
```

禁止将客户端传入的 `sort_by` 直接拼接进 SQL。

对于大量文件，后续可以从偏移分页升级为游标分页，但 MVP 阶段可以使用偏移分页。

## 认证端点

> 状态：**已实现**（阶段二，2026-09-12）。

```text
POST /api/v1/auth/register    注册（匿名）
POST /api/v1/auth/login       登录（匿名）
POST /api/v1/auth/logout      登出（需认证）
GET  /api/v1/auth/me          当前用户（需认证）
```

### 注册

```http
POST /api/v1/auth/register
Content-Type: application/json

{ "username": "alice", "email": "alice@example.com", "password": "correct-horse-battery" }
```

```json
{
  "data": {
    "access_token": "eyJhbGciOiJIUzI1NiIs...",
    "token_type": "Bearer",
    "expires_in": 3600,
    "user": {
      "id": "b730a85f-4543-40c9-ab02-f51cae8729ab",
      "username": "alice",
      "email": "alice@example.com",
      "role": "USER",
      "status": "ACTIVE",
      "storage_quota_bytes": 21474836480,
      "used_storage_bytes": 0,
      "created_at": "2026-09-12T02:14:50Z",
      "updated_at": "2026-09-12T02:14:50Z"
    }
  },
  "request_id": "738b0382-b465-4d3f-9181-b34d285dfacb"
}
```

校验规则：用户名 3–64 位，仅限 `A-Za-z0-9._-`；邮箱 ≤ 254 且需为合法格式；
密码 8–1024 字节。用户名与邮箱**不区分大小写**唯一。

### 登录

```http
POST /api/v1/auth/login
Content-Type: application/json

{ "username": "alice", "password": "correct-horse-battery", "remember": true }
```

`username` 字段也接受邮箱（带 `@` 时前端会改用 `email` 字段，后端两者都认）。
响应结构与注册完全一致。

#### `remember`：记住我（2026-10-03 新增，用户要求）

用户反馈"每次打开 App 都要重新登录"——这是**产品缺陷**，所以登录与注册都接受一个可选的
布尔字段 `remember`：

| `remember` | 令牌有效期 | 前端存哪 |
|---|---|---|
| `true` | `JWT_REMEMBER_EXPIRE_SECONDS`（默认 **30 天**） | `localStorage`（跨浏览器/App 重启还在） |
| 缺省或 `false` | `JWT_EXPIRE_SECONDS`（默认 **1 小时**） | `sessionStorage`（关掉就没了） |

- ⛔ 字段缺失、类型不对一律按 `false` 处理：**默认必须是短令牌**，解析写错也不能默默发长效令牌。
- 响应里的 `expires_in`（秒）就是本次实际用的有效期，客户端据此决定存储位置与提示语；
  **两个方向都有集成测试**（`integration/auth_flow_test.cpp` 的 `RememberMeControlsTokenLifetime`）。
- 安全代价写在明处：令牌放 `localStorage` 会拉长 XSS 的窃取窗口，所以「记住我」是显式勾选
  （默认勾上，方便非技术用户），公用电脑可以取消。生产环境的正解仍是 HttpOnly + Secure +
  SameSite Cookie（见下文「认证方案」）。

### 登出

```http
POST /api/v1/auth/logout
Authorization: Bearer <token>
```

返回 `{"data":{"logged_out":true},"request_id":"..."}`。

JWT 是**无状态**的，服务端没有会话可销毁，登出由客户端丢弃 Token 完成，
服务端只记录审计日志。Token 黑名单需要 Redis，属于阶段十二的范围。

### 当前用户

```http
GET /api/v1/auth/me
Authorization: Bearer <token>
```

返回 `data` 为 `User` 对象（不含 `password_hash`）。

### 认证相关的错误

| 场景 | HTTP | 错误码 |
|---|---|---|
| 用户名/邮箱已被占用 | 409 | `RESOURCE_CONFLICT` |
| 字段不合规（用户名格式、邮箱格式、密码长度） | 400 | `INVALID_ARGUMENT` |
| 用户名或密码错误 | 401 | `UNAUTHENTICATED` |
| 用户不存在 | 401 | `UNAUTHENTICATED`（**与上一条响应完全相同**，避免枚举账号） |
| 缺少或格式错误的 `Authorization` 头 | 401 | `UNAUTHENTICATED` |
| Token 签名无效或声明不全 | 401 | `UNAUTHENTICATED` |
| Token 已过期 | 401 | `TOKEN_EXPIRED` |
| 账号已被禁用（口令正确，或持有未过期 Token） | 403 | `FORBIDDEN` |

要点：

- 密码使用 **Argon2id** 哈希（默认 m=64 MiB、t=3、p=1，每次随机 16 字节盐），
  编码为 PHC 字符串存储；数据库里没有明文，响应里也没有 `password_hash`。
- 登录时即使用户不存在也会执行一次哈希校验，避免通过响应时间枚举账号。
- **先校验口令再检查状态**：口令不对的人不该知道这个账号已被禁用。
- 被禁用用户即使持有未过期的 Token 也无法访问受保护接口（过滤器每次复查状态）。
- Token 不能写入日志；错误信息里不回显 Token 内容。
- 登录失败限流属于阶段十二。

## 文件夹端点

> 状态：**已实现**（阶段三，2026-09-12）。`POST .../restore` 属于阶段八，尚未实现。

```text
GET    /api/v1/folders/root                    当前用户根目录（缺失时自动补建）
GET    /api/v1/folders/{folder_id}             详情
GET    /api/v1/folders/{folder_id}/path        从根到该目录的链路（面包屑用）
GET    /api/v1/folders/{folder_id}/children    子目录列表（分页 + 排序白名单）
POST   /api/v1/folders                         新建
PATCH  /api/v1/folders/{folder_id}             重命名或移动
DELETE /api/v1/folders/{folder_id}             软删除（含整棵子树）
POST   /api/v1/folders/{folder_id}/restore     恢复（阶段八）
```

全部需要认证。`Folder` 对象的对外表示：

```json
{
  "id": "b8144141-89b9-49a3-8d8b-4e9aeff53a9d",
  "owner_id": "bd7dc06c-510f-4cc4-986d-c897be6b7648",
  "parent_id": "c3711ae0-18e7-456d-b58a-7007317f60c0",
  "name": "documents",
  "status": "ACTIVE",
  "is_root": false,
  "created_at": "2026-09-12T02:28:22Z",
  "updated_at": "2026-09-12T02:28:22Z",
  "deleted_at": null
}
```

内部字段 `path_key`（物化路径）**不对外暴露**。

### 根目录

每个用户恰好有一个根目录：`parent_id` 为 `null`、`name` 为空串、`is_root` 为 `true`。
`GET /folders/root` 是幂等的——不存在时会创建，注册时预置失败也能自愈。

### 子目录列表

```http
GET /api/v1/folders/{folder_id}/children?page=1&page_size=50&sort_by=name&sort_order=asc
```

```json
{
  "data": {
    "items": [ /* Folder 数组 */ ],
    "total": 2,
    "page": 1,
    "page_size": 50
  },
  "request_id": "..."
}
```

`sort_by` 白名单：`name`、`created_at`、`updated_at`；不在白名单内返回 400。
只返回 `status = 'ACTIVE'` 的直接子目录，已软删除的不出现。

### 新建

```http
POST /api/v1/folders
{ "parent_id": "<uuid>", "name": "documents" }
```

### 重命名 / 移动

```http
PATCH /api/v1/folders/{folder_id}
{ "name": "new-name" }          // 重命名
{ "parent_id": "<uuid>" }       // 移动
```

一次只能提交其中一项，同时提交返回 400（避免「改名成功但移动失败」的半成品状态）。

### 删除

`DELETE` 是软删除：`status` 变为 `TRASHED`、写入 `deleted_at`，
**整棵子树一并标记**，物理内容保留到阶段八的回收站清理。

### 约束与错误

| 场景 | HTTP | 错误码 |
|---|---|---|
| 名称非法（空、含 `/` `\`、控制字符、`.`、`..`、超长） | 400 | `INVALID_ARGUMENT` |
| 同一父目录下重名（不区分大小写） | 409 | `RESOURCE_CONFLICT` |
| 移动到自身或自己的子目录 | 400 | `INVALID_ARGUMENT` |
| 重命名 / 移动 / 删除根目录 | 403 | `FORBIDDEN` |
| 目录不存在、已删除，或**属于其他用户** | 404 | `RESOURCE_NOT_FOUND` |
| 排序字段不在白名单 | 400 | `INVALID_ARGUMENT` |

要点：

- 他人资源一律返回 404 而不是 403，**避免通过状态码探测资源是否存在**。
- 移动不许造成环：用物化路径判断目标是否在源的子树内（`target.path_key` 以 `source.path_key` 开头即拒绝）。
- 移动会**重写整棵子树的 `path_key`**，否则后续子树查询会错。
- 删除与移动都会记录操作者（当前写入结构化日志；`audit_logs` 表在阶段八引入）。
- 管理员可以访问用户资源，但调用方必须留下审计记录（docs/architecture.md）。

## 文件端点

> 状态：**部分实现**（阶段四，2026-09-12）。已实现上传、下载、元数据、列表；
> 重命名/删除在阶段八，版本相关在阶段七。

```text
POST   /api/v1/files?folder_id=&name=   单文件上传（请求体流式写入）
GET    /api/v1/files?folder_id=         目录内文件列表（分页 + 排序白名单）
GET    /api/v1/files/{file_id}          元数据
GET    /api/v1/files/{file_id}/download 下载（支持 Range）
PATCH  /api/v1/files/{file_id}          重命名（阶段八）
DELETE /api/v1/files/{file_id}          软删除（阶段八）
GET    /api/v1/files/{file_id}/versions             历史版本（阶段七）
GET    /api/v1/files/{file_id}/versions/{vid}/download  历史版本下载（阶段七）
POST   /api/v1/files/{file_id}/restore  恢复（阶段八）
```

`File` 对象的对外表示（**不暴露 `storage_key`**）：

```json
{
  "id": "0c2fb1df-cc6e-473d-8ca6-c050e92596bc",
  "owner_id": "0831bdf2-2e95-4d98-b0e5-a5175965e48b",
  "folder_id": "7f757c02-ac27-4683-9049-28d1a7d5250b",
  "current_version_id": "392e3aec-6bf9-4349-b642-0cf980628c8c",
  "name": "hello.txt",
  "mime_type": "text/plain",
  "size_bytes": 38,
  "content_hash": "2df786e97dbb059a5e7a26bc4d1668de9eb023afcdff320075e3ff693ded89bb",
  "status": "ACTIVE",
  "created_at": "2026-09-12T02:41:39Z",
  "updated_at": "2026-09-12T02:41:39Z",
  "deleted_at": null
}
```

### 上传

```http
POST /api/v1/files?folder_id=<uuid>&name=report.pdf
Content-Type: application/pdf

<文件的原始字节>
```

请求体就是文件内容本身（不是 multipart），文件名与目标目录走查询参数。
服务端**边收边写盘边计算 SHA-256**，整个文件不会进内存。

- 单文件上限由 `MAX_FILE_SIZE` 控制（默认 10 GiB），超限返回 400。
- 同名冲突返回 409；阶段四不覆盖，覆盖语义在阶段七作为新版本处理。
- 文件名规则与文件夹一致：不含 `/`、`\`、控制字符，不能是 `.` 或 `..`，最长 255。
- 客户端提供的 `Content-Type` 会被清洗：不满足 `type/subtype` 形式的一律回落到
  `application/octet-stream`，避免响应头注入。
- 上传中断或失败时**不返回成功**，临时对象会被删除（实测不留残留）。

### 下载

```http
GET /api/v1/files/{file_id}/download
Range: bytes=0-499
```

| 情况 | 响应 |
|---|---|
| 无 `Range` | **200**，`Accept-Ranges: bytes`，流式发送整个文件 |
| 合法 `Range` | **206 Partial Content** + `Content-Range: bytes 0-499/1234` |
| `Range` 起点越界 | **416** + `Content-Range: bytes */1234` |

`Content-Disposition` 按 RFC 6266 生成。含非 ASCII 文件名时同时给出 ASCII 回退名与
`filename*=UTF-8''<百分号编码>`，保证响应头始终是合法 ASCII：

```text
Content-Disposition: attachment; filename="______.txt"; filename*=UTF-8''%E4%B8%AD%E6%96%87.txt
```

#### 下载票据（大文件流式下载，2026-09-21）

```http
POST /api/v1/files/{file_id}/download-ticket
POST /api/v1/files/{file_id}/versions/{version_id}/download-ticket
```

浏览器直接导航下载时**带不了 `Authorization` 头**，而用 axios 取 blob 会把整个文件读进浏览器内存
（几 GB 必崩）。所以改成两步：先换一张票据，再用票据下载。

```json
{
  "data": {
    "ticket": "eyJhbGciOiJIUzI1NiJ9...",
    "url": "/api/v1/files/<file_id>/download?ticket=eyJhbGciOiJIUzI1NiJ9...",
    "expires_in": 60
  },
  "request_id": "uuid"
}
```

- 票据是**签名的 JWT**（复用会话令牌那套密钥与库），声明里写死 `fid`（必要时 `vid`）与 `sub`（用户）。
- 有效期 **60 秒**；`GET .../download?ticket=<票据>` 时 `AuthFilter` 会认票据，
  但 handler 仍会**再校验一次文件/版本是否与票据一致**，并且**照常检查权限**（票据 ≠ 授权）。
- **票据不能当会话令牌用**：`Verify()` 会拒绝 `purpose=download` 的令牌；反过来会话令牌也不能当票据用
  （单元测试覆盖了这两个方向）。
- 签发接口本身要求登录 + 对该文件有 READ 权限；日志里只记「谁给哪个文件签了票据」，**不记票据内容**。

### 列表

```http
GET /api/v1/files?folder_id=<uuid>&page=1&page_size=50&sort_by=name&sort_order=asc
```

`sort_by` 白名单：`name`、`size`、`created_at`、`updated_at`。

### 存储与错误

| 场景 | HTTP | 错误码 |
|---|---|---|
| 文件名非法 | 400 | `INVALID_ARGUMENT` |
| 超过单文件大小上限 | 400 | `INVALID_ARGUMENT` |
| 同名文件已存在 | 409 | `RESOURCE_CONFLICT` |
| 目标目录不存在或不属于自己 | 404 | `FOLDER_NOT_FOUND` |
| 文件不存在、已删除或属于他人 | 404 | `RESOURCE_NOT_FOUND` |
| 磁盘写入失败 | 507 | `STORAGE_FULL` |
| 元数据在但对象文件丢失 | 404 | `RESOURCE_NOT_FOUND`（并写错误日志） |

要点：

- 对象按内容寻址存放：`objects/<sha256 前两位>/<sha256>`，**原始文件名只作元数据**，
  永远不参与物理路径拼接（docs/storage.md）。
- 相同内容的两次上传会复用同一个对象文件，但各自有独立的元数据记录。
- `storage_key` 不对客户端暴露。
- 权限检查在服务端完成；他人资源返回 404 而不是 403，避免探测资源是否存在。

版本冲突（乐观并发控制）：更新文件时支持客户端传递：

```http
If-Match: <current-version-id>
```

如果客户端提交的版本不是当前版本：

```text
409 FILE_VERSION_CONFLICT
```

响应包含当前版本，客户端据此刷新后重试：

```json
{
  "error": {
    "code": "FILE_VERSION_CONFLICT",
    "message": "文件已被其他人修改，请刷新后重试",
    "details": {
      "current_version_id": "version-uuid"
    }
  },
  "request_id": "uuid"
}
```

> 结构化附加信息统一放在 `error.details` 里（该字段在统一响应信封中一直存在）。
> `details` 只允许放客户端本来就能看到的标识（版本 id 等），不得放路径、SQL 或密钥。

## 最近文件端点（2026-10-03 新增）

```text
GET /api/v1/recent-files?page=1&page_size=20
```

给「最近文件」页用（用户要求把它从占位说明做成**真列表**）：**跨目录**按 `updated_at` 倒序列出
**自己的**有效文件，并带上"在哪个文件夹"，否则用户只看到文件名、不知道去哪找。

- 需要登录（`AuthFilter`）；只列 `owner_id = 自己` 且 `status = ACTIVE` 的文件
  —— 别人分享给你的在「分享给我的」（`/api/v1/shared-with-me`），两处职责不混。
- ⛔ 路径**不用** `/api/v1/files/recent`：那会和 `/api/v1/files/{file_id}` 抢匹配（Drogon 路由）。
- 分页与排序规则同其它列表（`page_size` 1–100）。

```json
{
  "data": {
    "items": [
      {
        "id": "uuid", "name": "报告.pdf", "size_bytes": 102400, "mime_type": "application/pdf",
        "updated_at": "2026-10-03T01:02:03Z", "current_version_id": "uuid",
        "folder_id": "uuid", "folder_name": "工作"
      }
    ],
    "total": 12, "page": 1, "page_size": 20
  },
  "request_id": "uuid"
}
```

- `folder_name` 为空串表示**根目录**（界面显示「我的文件」）。

## 版本端点

```text
GET  /api/v1/files/{file_id}/versions                        历史版本列表
GET  /api/v1/files/{file_id}/versions/{version_id}/download  历史版本下载（支持 Range）
POST /api/v1/files/{file_id}/versions/{version_id}/restore   恢复到历史版本
```

**版本只增不减**：

- 上传新内容（`POST /uploads` 带 `file_id`）会追加一个版本，版本号 = 当前最大 + 1。
- 恢复历史版本**不会删除任何东西**，而是把该版本的内容作为一个**新版本**追加，
  因此恢复本身也可以再被回退。
- 旧版本的对象不会被覆盖：每个版本有自己的 `storage_key`，历史版本始终可下载。

历史版本列表返回（新的在前，`storage_key` 不对客户端暴露）：

```json
{
  "data": {
    "items": [
      {
        "id": "version-uuid",
        "file_id": "file-uuid",
        "version_number": 2,
        "size_bytes": 14680064,
        "content_hash": "64 位小写十六进制",
        "created_by": "user-uuid",
        "created_at": "2026-01-02T03:04:05Z",
        "is_current": true
      }
    ],
    "total": 2
  },
  "request_id": "uuid"
}
```

历史版本下载：

- 支持 `Range`，语义与当前版本下载一致（206 / 416）。
- 响应带 `X-File-Version: <版本号>`，`Content-Disposition` 的文件名带 `.v<版本号>` 后缀。
- 该版本对应的对象缺失时返回 `404 RESOURCE_NOT_FOUND` 并写错误日志。

恢复历史版本：

- 需要写权限；他人资源一律 `404`。
- 可以带 `If-Match: <current-version-id>`：与服务端当前版本不一致返回 `409 FILE_VERSION_CONFLICT`，
  `details.current_version_id` 给出真实当前版本。
- **幂等**：当前内容已经等于目标版本时不追加新版本，直接返回文件（重复点击恢复不会刷出一堆版本）。
- 配额按版本累计（历史版本占空间），超出配额返回 `409 QUOTA_EXCEEDED`。

### 上传新版本

给已有文件加版本，用同一个上传接口 + `file_id`：

```json
POST /api/v1/uploads
{
  "file_id": "file-uuid",
  "size_bytes": 14680064,
  "sha256": "64 位小写十六进制",
  "chunk_size": 8388608
}
```

- 此时 `folder_id` 与 `file_name` 由服务端按文件自身补齐，客户端传了也会被忽略
  （版本不能改名、不能换目录）。
- 请求头 `If-Match: <current-version-id>` 等价于 `expected_version_id`，两者取其一即可。
- 内容与当前版本相同 → 不追加重复版本（幂等）。
- 版本号由服务端在同一事务里计算，并发提交只有一个能成功，另一个得到 `409 FILE_VERSION_CONFLICT`。

## 上传端点

```text
POST   /api/v1/uploads
GET    /api/v1/uploads/{upload_id}
PUT    /api/v1/uploads/{upload_id}/chunks/{chunk_index}
POST   /api/v1/uploads/{upload_id}/complete
DELETE /api/v1/uploads/{upload_id}
```

### 1. 创建上传会话

`POST /api/v1/uploads`

```json
{
  "folder_id": "uuid",
  "file_name": "big.bin",
  "size_bytes": 21474836480,
  "mime_type": "application/octet-stream",
  "chunk_size": 8388608,
  "sha256": "64 位小写十六进制（可选）"
}
```

成功返回 `200`：

```json
{
  "data": {
    "upload_id": "uuid",
    "chunk_size": 8388608,
    "total_chunks": 2560,
    "total_size": 21474836480,
    "status": "INITIATED",
    "already_exists": false,
    "completed_chunks": [],
    "expires_at": "2026-01-02T03:04:05Z"
  },
  "request_id": "uuid"
}
```

- `chunk_size` 可选，默认 8 MiB；允许范围 **1 MiB ~ 64 MiB**，越界返回 `400 INVALID_ARGUMENT`。
- 上传**不使用**"单个请求传整个文件"：单个请求体上限为最大分块 + 1 MiB，超过该上限由 HTTP 层返回 `413 PAYLOAD_TOO_LARGE`。
- `size_bytes` 必填（`0` 合法，表示空文件）；超过单文件上限返回 `400 INVALID_ARGUMENT`。
- `sha256` 可选；提供时必须是 64 位小写十六进制，否则 `400 INVALID_ARGUMENT`。
- 目标目录不存在或无写权限返回 `404 FOLDER_NOT_FOUND`（不区分「不存在」与「无权限」）。
- 同目录已有同名文件返回 `409 RESOURCE_CONFLICT`（在会话创建阶段就拦住，避免传完几十 GB 才报错）。
- 并发上传超过用户级或全局上限返回 `503 STORAGE_BUSY`。
- 超出存储配额返回 `409 QUOTA_EXCEEDED`（这里的判定用客户端声明的大小，只为止损）。
- 会话有有效期（默认 24 小时，`UPLOAD_SESSION_TTL` 可配），过期后不能继续上传分块。

#### 去重预检查（`already_exists`）

当请求带了 `sha256`，且**该用户自己**已经有同样内容的文件时：

```json
{
  "data": {
    "upload_id": "uuid",
    "already_exists": true,
    "existing_file": { "file_id": "uuid", "name": "first.bin", "size_bytes": 12582912 }
  },
  "request_id": "uuid"
}
```

- 客户端可以**一个分块都不传**，直接调用 `complete` 建出新文件记录，内容指向同一物理对象（秒传）。
- 只回答「你自己有没有这份内容」，不回答「系统里有没有这个哈希」——避免上传接口变成
  文件存在性探测工具；别人的内容查不到 `existing_file`。
- 判定依据是服务端数据库里的对象（哈希 + 对象大小），客户端谎报哈希最多得到
  「自己已有文件」的副本，读不到别人的内容。
- 跨用户的**物理**去重仍然生效（相同内容在磁盘上只有一份），只是不通过这个字段暴露。


### 2. 上传分块

`PUT /api/v1/uploads/{upload_id}/chunks/{chunk_index}`

- 请求体是**原始字节**，不是 multipart。
- `chunk_index` 从 0 开始，必须小于 `total_chunks`，否则 `422 CHUNK_INVALID`。
- 请求头 `X-Chunk-Sha256` 可选但要填就填对：与请求体实际哈希不符返回 `422 CHUNK_INVALID`。
- 同一索引重复上传：内容一致 → `200` 且 `already_present: true`（不重复写盘）；内容不同 → `409 CHUNK_CONFLICT`，**不覆盖已存分块**。
- 非本会话所有者一律 `404 UPLOAD_NOT_FOUND`。
- 会话已过期返回 `410 UPLOAD_EXPIRED`；已取消返回 `409 UPLOAD_ABORTED`；已失败返回 `409 UPLOAD_ABORTED`。

### 3. 查询上传状态

`GET /api/v1/uploads/{upload_id}` → `200`

```json
{
  "data": {
    "upload_id": "uuid",
    "status": "UPLOADING",
    "file_name": "big.bin",
    "total_size": 21474836480,
    "chunk_size": 8388608,
    "total_chunks": 2560,
    "completed_chunks": [0, 1, 2, 5],
    "received_bytes": 25165824,
    "expires_at": "2026-01-02T03:04:05Z"
  },
  "request_id": "uuid"
}
```

断点续传就是靠这个接口：客户端用 `completed_chunks` 过滤出缺失分块。**分块不保证按顺序到达**，客户端不能假设前缀连续。

### 4. 完成上传

`POST /api/v1/uploads/{upload_id}/complete` → `200`，返回创建好的文件元数据（与文件端点同结构）。

- 分块不齐返回 `422 UPLOAD_INCOMPLETE`，**会话不会被破坏**，可以补齐后重试。
- 服务端重新合并、**重新计算大小与 SHA-256**；与客户端声明的 `sha256` 不一致返回 `422 HASH_MISMATCH`，会话置为 `FAILED` 并清理分块。
- 超出配额返回 `409 QUOTA_EXCEEDED`：这是**权威判定**（用服务端算出的真实字节数，
  在事务里锁住用户行后比对），会话置为 `FAILED` 并清理分块。
- 内容已存在时（含秒传路径）复用同一物理对象，不重复占盘；配额按**逻辑大小**计算，
  同一用户引用两次算两份。
- 重复调用幂等：已完成时直接返回同一个文件记录。
- 并发调用同一个会话时只有一个请求能进入合并，其余返回 `409 RESOURCE_CONFLICT`（或在前者已完成后幂等返回同一文件）。

### 5. 取消上传

`DELETE /api/v1/uploads/{upload_id}` → `200 {"data":{"cancelled":true}}`

- 幂等：重复取消仍返回 `200`。
- 取消后分块（数据库记录 + 磁盘文件）立即清理，并发额度随之释放。
- 对已完成的会话返回 `409 RESOURCE_CONFLICT`。

### 其他要点

- 需要时可以用 `Idempotency-Key: <client-generated-key>` 标记重复提交。
- 上传协议、分块落盘位置与路径安全见 [storage.md](storage.md)，前端算法与重试策略见 [frontend.md](frontend.md)。
- 会话过期、失败与清理的运维处置见 [recovery.md](recovery.md)。

## 分享端点

```text
POST   /api/v1/resources/{resource_id}/shares
GET    /api/v1/resources/{resource_id}/shares
DELETE /api/v1/shares/{share_id}
GET    /api/v1/public/shares/{token}
GET    /api/v1/public/shares/{token}/download

# 别人分享给我的资源（含权限级别；只返回仍有效、且我是被授权人的记录）
GET    /api/v1/shared-with-me
```

要点：

- 创建成功后只能向用户展示一次明文 Token，服务端不保存明文 Token，只保存 Token 的哈希值。
- 支持选择只读或可写、设置过期时间、撤销链接、查看访问次数。
- 分享链接访问必须校验：是否过期、是否被撤销、是否达到下载次数、是否允许下载文件夹、是否需要密码。
- 密码分享属于可选功能，不纳入最小 MVP。
- 重复删除分享链接必须幂等。
- `/api/v1/public/shares/...` 为公开接口，但仍须完成上述校验。

## 权限端点

```text
GET    /api/v1/resources/{resource_id}/permissions
PUT    /api/v1/resources/{resource_id}/permissions/{user_id}
DELETE /api/v1/resources/{resource_id}/permissions/{user_id}
```

要点：

- 权限级别为 `READ`、`WRITE`、`OWNER`、`ADMIN`：
  - `READ`：查看目录、读取文件、下载文件。
  - `WRITE`：包含 READ，并允许上传、更新、移动、重命名和删除。
  - `OWNER`：管理资源权限、分享链接和删除资源。
  - `ADMIN`：管理用户、系统配置和全局资源。
- 所有权限必须在服务端检查，前端隐藏按钮不是权限控制。
- 权限检查必须覆盖：查看目录、获取文件元数据、下载文件、初始化上传、上传分块、完成上传、删除文件、恢复文件、移动文件、创建分享链接、修改权限、查看版本、下载历史版本。
- 权限继承与模型细节见 `permissions.md`。

## 权限端点

```text
GET    /api/v1/resources/{resource_id}/permissions
PUT    /api/v1/resources/{resource_id}/permissions/{user_id}
DELETE /api/v1/resources/{resource_id}/permissions/{user_id}
GET    /api/v1/resources/{resource_id}/effective-permission
```

要点：

- `resource_id` **不带类型**：服务端先按文件查，再按文件夹查；两处都没有返回 `404 RESOURCE_NOT_FOUND`。
- 需要 **OWNER**（资源所有者）或 ADMIN；被授予 WRITE 的人也不能改权限。
- `PUT` 幂等：同一用户对同一资源只有一条记录，重复授予等于改级别。
  请求体 `{"permission": "READ" | "WRITE" | "OWNER"}`；非法值返回 `400 INVALID_ARGUMENT`。
- 被授权人必须存在且处于 ACTIVE，否则 `404`（不存在）或 `400`（已禁用）。
- `DELETE` 幂等：删除不存在的授权也返回 `200`。
- `effective-permission` 返回当前用户对该资源的有效级别
  （`NONE` / `READ` / `WRITE` / `OWNER` / `ADMIN`），**已算上文件夹继承**。
  前端用它决定展示哪些按钮；后端在每次操作时仍然独立校验。
- 授权与撤销都写审计日志。继承算法与动作-级别对照见 [permissions.md](permissions.md) §3。

## 分享端点

```text
POST   /api/v1/resources/{resource_id}/shares
GET    /api/v1/resources/{resource_id}/shares
DELETE /api/v1/shares/{share_id}
GET    /api/v1/public/shares/{token}
GET    /api/v1/public/shares/{token}/download

# 别人分享给我的资源（含权限级别；只返回仍有效、且我是被授权人的记录）
GET    /api/v1/shared-with-me
```

创建分享（需要 OWNER）：

```json
POST /api/v1/resources/{resource_id}/shares
{
  "permission": "READ",
  "expires_in_seconds": 604800,
  "max_downloads": 10
}
```

响应里 `token` 是**明文，只出现这一次**：

```json
{
  "data": {
    "id": "share-uuid",
    "resource_type": "file",
    "resource_id": "file-uuid",
    "permission": "READ",
    "expires_at": "2026-01-09T03:04:05Z",
    "max_downloads": 10,
    "download_count": 0,
    "token": "43 个字符的高强度随机值",
    "url": "/api/v1/public/shares/<token>",
    "download_allowed": true,
    "token_visible_once": true
  },
  "request_id": "uuid"
}
```

- 服务端只保存 `SHA-256(token)`；列表接口**不回传明文，也不回传哈希**。
- `permission` 只能是 `READ` 或 `WRITE`（分享链接上不提供 OWNER）。
- `expires_in_seconds` 最长 365 天；`max_downloads` 必须大于 0；两者都可省略（不过期、不限次）。
- 撤销：`DELETE /api/v1/shares/{share_id}`，幂等；只有分享创建者或资源所有者能撤销。

## 邀请端点（邀请制注册，2026-10-02 新增）

用户要求：**不是所有人都能注册，要通过管理员邀请**。默认仍允许自助注册（个人自用方便），
把 `.env` 的 `ALLOW_SELF_REGISTRATION` 设成 `false` 之后，注册就必须带一条有效邀请。

```text
POST   /api/v1/admin/invites                 建邀请（ADMIN）
GET    /api/v1/admin/invites                 列出邀请（ADMIN）
DELETE /api/v1/admin/invites/{invite_id}     撤销（ADMIN，幂等）
GET    /api/v1/public/invites/{token}        校验邀请（公开，注册页用）
GET    /api/v1/public/registration-policy    当前是否允许自助注册（公开）
```

**建邀请**：请求体 `{ "note": "给小王", "expires_in_seconds": 604800, "max_uses": 1 }`
（都可省略；`expires_in_seconds` 5 分钟–1 年，省略 = 不过期；`max_uses` 1–1000，默认 1）。

```json
{
  "data": {
    "invite": { "id": "uuid", "note": "给小王", "created_at": "2026-10-02T12:00:00Z",
                "expires_at": "2026-10-09T12:00:00Z", "max_uses": 1, "used_count": 0,
                "remaining_uses": 1, "revoked_at": null },
    "token": "只在这一次返回的明文"
  },
  "request_id": "uuid"
}
```

- ⛔ **`token` 明文只在这里返回一次**：库里只存 `token_hash`（SHA-256），之后任何接口都取不回来
  —— 与分享链接同一条口径（`docs/security.md`）。
- 列表接口**不含 token**，只给状态（`used_count` / `remaining_uses` / `expires_at` / `revoked_at`）。
- 非管理员调用 → `403 FORBIDDEN`；日志只记 `invite_id` 与 token 哈希前缀，**不记明文**。

**公开校验**：`GET /api/v1/public/invites/{token}` → `{ "note", "expires_at", "remaining_uses" }`；
token 不存在 → `404 RESOURCE_NOT_FOUND`，过期/用完/撤销 → `400 INVITE_INVALID`。

**注册**：`POST /api/v1/auth/register` 新增可选字段 `invite_token`。

- 关闭自助注册时缺它 → `403 INVITE_REQUIRED`；给了但无效 → `400 INVITE_INVALID`。
- 开着自助注册时也接受它（便于知道"这个人是被谁邀请来的"），无效则当作没带。
- 消耗是**一条原子 UPDATE**（`used_count < max_uses AND 未撤销 AND 未过期`），并发下不会用超；
  注册失败（例如用户名被占用）会把次数**归还**，不白吃一次。

## 用户名录端点（按人分享，2026-09-21 新增）

```text
GET /api/v1/users/lookup?q=<关键词>
```

给「发送给…」用：**按用户名或邮箱找人**，让用户可以选人分享，而不是填用户 UUID
（用户实测反馈：界面上根本查不到 UUID，等于没法按人分享）。

- 需要登录（`AuthFilter`）；任何普通用户都可调用。
- `q` 去掉首尾空白后**至少 2 个字符**，否则返回 `400 INVALID_ARGUMENT`。
- 匹配规则：用户名或邮箱的**大小写不敏感模糊匹配**（`ILIKE '%…%'`，通配符已转义）；
  排除调用者自己，排除 `status != ACTIVE` 的用户；最多返回 **10** 条。
- **只返回 `id` 与 `username`**，不返回邮箱、角色、配额等任何其它字段（最小暴露）：

```json
{
  "data": { "items": [ { "id": "user-uuid", "username": "alice" } ] },
  "request_id": "uuid"
}
```

- 拿到 `id` 后按既有的授权接口分享：`PUT /api/v1/resources/{resource_id}/permissions/{user_id}`
  （见「权限端点」）。

## 系统信息端点（当前局域网地址，2026-10-02 新增）

```text
GET /api/v1/system/network
```

给前端拼**分享链接**用：返回服务端**此刻**的局域网地址。

用户反馈（2026-10-02）：开机之后切换网络（校园网 ↔ 手机热点），地址不会跟着变，只能重启。
实际上**不需要重启**（后端监听 `0.0.0.0`，换网络后照样在听），变的只是"该让手机打开哪个地址"，
所以这个接口**每次请求现算**：用 UDP 套接字 `connect` 一次外部地址（**不发任何数据包**，
只让内核按当前路由表选出口网卡）再 `getsockname()`，拿到的就是当前该用的本机地址。

- 需要登录（`AuthFilter`）——局域网地址不必对未登录的人公开。
- 没有默认路由（例如只接了交换机、没有网关）时 `lan_address` 为空串、`detected` 为 `false`，
  前端会退回启动脚本注入的地址，并给出"别人手机打不开"的提醒。

```json
{
  "data": {
    "lan_address": "10.200.18.61",
    "detected": true,
    "request_host": "127.0.0.1:9180"
  },
  "request_id": "uuid"
}
```

- `request_host` 是客户端请求时带的 `Host`（`127.0.0.1:9180` / 局域网 IP / 域名），仅用于排查。
- 前端拿到后拼 `http://<lan_address>:<当前页面端口>`（便携包 9180、开发 5173），
  见 `frontend/src/services/publicUrl.ts` 与 `networkApi.ts`。

## 配套手机应用端点（安卓安装包，2026-10-03 新增）

```text
GET /api/v1/app/release     安装包信息：版本 / 大小 / 构建时间 / SHA-256 / 下载地址
GET /api/v1/app/download    下载安装包本身（application/octet-stream）
```

用户要求（2026-10-03）：网页「我的」里要有一个**永远指向最新 APK** 的入口，而且"改完就更新的" ——
重新构建一次 App、刷新网页，卡片上的大小与时间就必须跟着变。所以：

- 信息**不写死**：后端每次请求现看磁盘上的安装包（位置见 `APP_APK_PATH`，
  便携包默认 `WEB_ROOT/downloads/lan-file-hub.apk`）。大小与时间取自**真实文件**，
  版本号与构建时间取自构建脚本写出的同名侧车（`lan-file-hub.apk.json`）——
  ⛔ 但**只有侧车与磁盘上这个包的字节数一致时才采信它**，否则整体忽略（版本号空着、时间用文件时间）：
  实测踩到过"只换包不换侧车 → 卡片显示新的大小配旧的时间"这种自相矛盾的组合；
  `sha256` **一律对真实文件现算**（只在这个包变化时算一次），不照抄侧车；
- 文件没换（大小 + 修改时间都没变）就复用上次算好的结果，换了立刻重算，**不需要重启后端**；
- `scripts\build-android.ps1` 出包后**自动**写侧车并同步到下载位置 —— 这就是"改完就更新的"。

这两个端点**不需要登录**（有意为之，不是漏加中间件）：装 App 这一步不该先登录
（手机上可能是系统浏览器/文件管理器直接打开下载地址），而 APK 是客户端程序本身、
不含任何用户数据（前端静态资源本来就是公开的）。用户数据仍然一律走 `AuthFilter` 与服务端权限校验。

```json
{
  "data": {
    "available": true,
    "file_name": "lan-file-hub.apk",
    "version_name": "1.0",
    "version_code": 1,
    "size_bytes": 346945,
    "built_at": "2026-10-03T01:01:18Z",
    "sha256": "5308c13e4bb4ebb81c3f7ee5fa4bb53f04e79cf90c8dc3d272f20eee0094d263",
    "download_path": "/api/v1/app/download"
  },
  "request_id": "uuid"
}
```

- 服务器上还没有安装包时：本接口仍返回 **200** 且 `available: false`（前端显示
  "这台服务器上还没有准备好安装包"），下载接口返回 **404 `RESOURCE_NOT_FOUND`**；都不报 500。
- 下载响应带 `Content-Disposition: attachment`（文件名已净化，见 `BuildContentDisposition`）
  与 `Cache-Control: no-cache`：浏览器/手机点一下就是**原生下载**（流式落盘，不走 blob），
  重新构建之后拿到的一定是新包。
- 前端拼下载地址时走 `frontend/src/services/publicUrl.ts` 的**同一套"手机点得开"规则**
  （`buildAppDownloadUrl`）：电脑上用 127.0.0.1 打开时会换成局域网地址，二维码扫了才下得到。

## 在线状态端点（广场，2026-09-21 新增）

```text
POST /api/v1/presence/heartbeat   心跳（客户端每 30 秒一次）
GET  /api/v1/presence/online      在线列表（广场）
PUT  /api/v1/presence/visibility  设置「是否让别人看到我在线」
```

给「广场」用：谁在线、在哪台设备上，以及同一账号的多台设备（互传入口）。

- 全部需要登录。心跳体：`{"device_id":"<本机生成>","device_name":"Chrome · Windows","platform":"web"}`；
  `device_id` 只允许 `[A-Za-z0-9._-]`、长度 ≤ 64，否则 `400 INVALID_ARGUMENT`。
- 心跳响应：`{"visible":true,"online":[…],"my_devices":[…],"ttl_seconds":90}`。
  `online` 里**不含自己**，且**不含隐身用户**；同一个人多设备只出现一次（取最新活跃的那台）。
  `my_devices` 是当前账号的全部在线设备（含 `device_id`，前端据此判断"哪台是我这台"）。
- **隐私**：`PUT /api/v1/presence/visibility {"visible":false}` → 隐身。
  默认可见（用户要求"只要在线就默认显示"）；隐身用户不出现在**别人**的列表里，自己始终能看到自己的设备。
- **状态不落库**：在线状态保存在服务端进程内存里，**90 秒**没有心跳即视为离线（`ttl_seconds`），
  服务重启即全部清空。多实例部署时各实例各自记账（本项目单机单进程）。
- 只暴露"是谁 + 什么设备 + 最后活跃时间"；**不返回其它用户的 `device_id`**。

公开访问（无需登录）：

- `GET /api/v1/public/shares/{token}` → 资源名、类型、大小、MIME、权限、剩余次数、`can_download`。
  **不含** owner、对象 Key 等内部标识。
- `GET /api/v1/public/shares/{token}/download` → 支持 `Range`（206 / 416），带
  `X-Robots-Tag: noindex` 与 `Cache-Control: no-store`。
- **过期、被撤销、超出下载次数、Token 不存在，统一返回 `404 RESOURCE_NOT_FOUND`**，不泄露具体原因。
- 下载次数的检查与自增在同一条 UPDATE 里完成，并发下载不会同时通过。
- 文件夹分享可以查看元数据，但下载返回 `403 FORBIDDEN`（说明不支持打包下载）。

## 回收站端点

```text
GET    /api/v1/trash
POST   /api/v1/trash/{resource_id}/restore
DELETE /api/v1/trash/{resource_id}/purge
```

要点：

- `GET /api/v1/trash` 是列表接口，必须支持分页与排序白名单，并支持按类型筛选、显示删除时间。
- 恢复规则与文件夹/文件的 `restore` 一致：原父目录不存在时恢复到用户根目录，同名冲突时拒绝并返回冲突信息，恢复必须幂等。
- `purge` 为永久删除，删除前检查权限与资源状态，删除数据库引用后异步删除对象存储内容并记录清理任务，删除失败必须可重试。
- 不允许在 HTTP 请求中长时间同步删除大量目录树。

## 管理员 API

管理员 API 必须单独分组：

```text
/api/v1/admin/users
/api/v1/admin/storage
/api/v1/admin/audit-logs
/api/v1/admin/system
```

管理员可以：

- 创建、禁用和恢复用户。
- 修改用户配额。
- 查看系统存储状态。
- 查看失败任务。
- 查看审计日志。
- 触发孤儿文件扫描。
- 触发清理任务。

管理员不得执行的操作见 `architecture.md`。

## 认证方案

默认方案：短期 JWT + 统一认证中间件（减少实现复杂度）。

本版采用短期 JWT 单 Token；双 Token（Access Token + Refresh Token）不在本版范围。

JWT 要求：

- Access Token 使用短过期时间。
- Secret 通过环境变量配置。
- Token 不能写入日志。
- 受保护接口统一经过认证中间件。
- 用户禁用后，后端不能只依赖 Token 过期，应在关键操作时检查用户状态。

不建议将 JWT 长期放在浏览器 `localStorage` 中。优先考虑：

- HttpOnly Cookie。
- Secure Cookie。
- SameSite Cookie。
- CSRF 防护。

若项目为了简化前后端开发使用内存 Token，必须在 README 中明确其生产限制。

> **本项目的现状（2026-10-03 更新）**：默认仍是"内存 + `sessionStorage`"的短令牌；
> **只有用户显式勾选「记住我」**（默认勾上）时，令牌才写 `localStorage` 并配 30 天有效期
> —— 这是用户真机反馈"每次都要重登"之后的有意取舍，风险与替代方案见上文登录一节，
> 以及 `README.md` 的「认证与 Token 策略」。生产环境（公司可用）仍应按上面四条改造。


## 管理员接口（阶段十三）

全部需要 ADMIN 角色：认证过滤器校验身份，`AdminService` 再判角色，非管理员一律 `403 FORBIDDEN`。
响应**不含** `password_hash`、Token 或敏感配置。

```text
GET    /api/v1/admin/users?page=&page_size=&search=&status=&sort_by=&sort_order=
PATCH  /api/v1/admin/users/{user_id}/status      {"status": "ACTIVE" | "DISABLED"}
PATCH  /api/v1/admin/users/{user_id}/quota       {"storage_quota_bytes": 21474836480}
GET    /api/v1/admin/storage
GET    /api/v1/admin/audit-logs?page=&page_size=&actor_id=&action=&resource_type=&resource_id=
```

- `sort_by` 白名单：`name`/`username`/`email`/`created_at`/`used_storage_bytes`/`storage_quota_bytes`；
  未知字段返回 `400 INVALID_ARGUMENT`（**不静默回退**）。`search` 同时匹配用户名与邮箱，LIKE 通配符会被转义。
- 禁用用户是幂等的；管理员**不能禁用自己**（`400 INVALID_ARGUMENT`）。被禁用用户立刻无法登录，
  已签发的 Token 也在过滤器处失效（`403 FORBIDDEN`，见上文登录错误表）。
- 改状态与改配额会写审计（`admin_disable_user` / `admin_enable_user` / `admin_set_quota`）；
  **查询审计日志本身也留痕**（`admin_query_audit_logs`）。审计写入失败只记服务端日志，不回滚业务操作。
- `GET /api/v1/admin/storage` 字段：`user_count`、`file_count`、`version_count`、`used_bytes`（按文件版本累计）、
  `free_bytes`（对象存储所在卷可用）、`active_uploads`。

## 指标端点（阶段十三）

```text
GET /metrics      # Prometheus 文本格式，text/plain; version=0.0.4
```

- **不做认证**（Prometheus 抓取端的常规做法），只暴露聚合数字：不含用户标识、文件名或路径。
  需要隔离时用反向代理限制来源。
- 标签基数有界：路由标签取**路由模板**（如 `/api/v1/files/{file_id}/download`），未匹配记 `unmatched`，
  方法走白名单，错误码取自本文件的封闭表；每个指标最多 200 条序列，超出计入
  `lan_file_hub_metrics_dropped_series_total`（自我暴露标签设计错误）。
- 主要指标：`lan_file_hub_http_requests_total`、`lan_file_hub_http_request_duration_seconds`、
  `lan_file_hub_errors_total`、`lan_file_hub_uploads_total`、`lan_file_hub_upload_bytes_total`、
  `lan_file_hub_upload_chunk_retries_total`、`lan_file_hub_downloads_total`、`lan_file_hub_active_uploads`、
  `lan_file_hub_storage_used_bytes`、`lan_file_hub_storage_free_bytes`、`lan_file_hub_background_jobs_total`、
  `lan_file_hub_health_status`。


## 健康检查端点

```text
GET /health/live      # 存活：只表示进程在运行，不依赖任何外部组件
GET /health/ready     # 就绪：逐个检查必要依赖，任一不可用返回 503
```

```json
GET /health/ready
{
  "status": "ok",
  "check": "ready",
  "components": [
    { "name": "postgresql", "required": true, "status": "ok", "latency_ms": 1, "detail": "" },
    { "name": "redis", "required": true, "status": "ok", "latency_ms": 0, "detail": "" },
    { "name": "object_storage", "required": true, "status": "ok", "latency_ms": 0, "detail": "" }
  ],
  "request_id": "…"
}
```

- 两个端点**都不需要认证**（供容器与负载均衡探针使用），只返回组件状态与延迟，不含凭据。
- `detail` 已脱敏（不含连接串、口令、绝对路径）。