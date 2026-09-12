# 数据模型与一致性

数据库使用 PostgreSQL。所有表结构、状态枚举和事务边界以此文为准。

## 概览

### 必须建立的表

```text
users
folders
files
file_versions
permissions
share_links
upload_sessions
upload_chunks
audit_logs
```

### 表关系

| 关系 | 说明 |
| --- | --- |
| `folders.owner_id` → `users.id` | 文件夹归属 |
| `folders.parent_id` → `folders.id` | 目录树自引用 |
| `files.owner_id` → `users.id` | 文件归属 |
| `files.folder_id` → `folders.id` | 文件所在目录 |
| `files.current_version_id` → `file_versions.id` | 当前版本 |
| `file_versions.file_id` → `files.id` | 版本属于文件，`is_current` 标记当前版本 |
| `permissions.resource_*` → `folders.id` / `files.id` | 按 `resource_type` 多态引用 |
| `upload_sessions.folder_id` → `folders.id` | 目标目录 |
| `upload_chunks.upload_session_id` → `upload_sessions.id` | 分块属于上传会话 |
| `audit_logs.actor_id` → `users.id` | 操作者 |

## 实体字段与枚举

### User

```text
id
username
email
password_hash
role
status
storage_quota_bytes
used_storage_bytes
created_at
updated_at
```

角色：

```text
USER
ADMIN
```

用户状态：

```text
ACTIVE
DISABLED
```

密码不得明文存储。使用 Argon2id；bcrypt 不采用。

### Folder

```text
id
owner_id
parent_id
name
path_key
status
created_at
updated_at
deleted_at
```

约束：

- 同一父目录下不能有同名有效文件夹。
- 文件夹名称不能包含路径分隔符。
- 不允许通过 `..` 穿越目录。
- 根目录不可删除。
- 删除文件夹时必须明确处理子文件和子目录。
- 建议 MVP 使用软删除。

### 文件夹约束

必须支持的操作：

```text
创建文件夹
获取子目录
重命名文件夹
移动文件夹
删除文件夹
恢复文件夹
```

#### 命名规则

| # | 规则 |
| --- | --- |
| 1 | 名称不能为空。 |
| 2 | 名称长度不能超过配置上限。 |
| 3 | 禁止 `/`、`\` 和控制字符。 |
| 4 | 不允许使用 `.` 和 `..`。 |
| 5 | 同一父目录下名称不重复。 |
| 6 | 是否大小写敏感必须统一，推荐数据库中使用大小写敏感名称并在产品文档中明确。 |

#### 移动规则

移动资源时必须检查：

1. 当前用户是否拥有源资源的写权限。
2. 当前用户是否拥有目标目录的写权限。
3. 目标目录不能是当前目录的子目录。
4. 不能移动根目录。
5. 不能造成同名冲突。
6. 移动操作必须写入审计日志。

### File

```text
id
owner_id
folder_id
current_version_id
original_name
mime_type
size_bytes
content_hash
storage_key
status
created_at
updated_at
deleted_at
```

文件状态：

```text
ACTIVE
TRASHED
PURGED
```

`storage_key` 不能使用用户输入的原始文件名直接生成。

推荐使用：

```text
objects/{sha256_prefix}/{sha256}
```

或：

```text
objects/{file_id}/{version_id}/content
```

### FileVersion

```text
id
file_id
version_number
content_hash
size_bytes
storage_key
created_by
created_at
is_current
```

版本号从 1 开始递增。

上传新内容时：

1. 创建临时上传任务。
2. 所有分块上传成功。
3. 合并文件。
4. 计算最终哈希。
5. 校验客户端声明的哈希。
6. 创建新版本。
7. 将文件的 `current_version_id` 指向新版本。
8. 将旧版本标记为非当前版本。

### 版本创建

- 每次成功上传新内容都创建一个新版本。
- 不能覆盖旧版本的物理对象，除非旧版本已经被明确清理。

### 乐观并发控制

更新文件时支持客户端传递：

```http
If-Match: <current-version-id>
```

如果客户端提交的版本不是当前版本：

```text
409 FILE_VERSION_CONFLICT
```

响应包含：

```json
{
  "error": {
    "code": "FILE_VERSION_CONFLICT",
    "message": "The file has been changed by another user.",
    "current_version_id": "version-uuid"
  }
}
```

### 冲突副本

冲突副本属于可选流程：如果产品流程允许自动保留两个用户的修改，可以创建冲突副本，例如命名为：

```text
report (conflict from user-2 2025-01-01 12-00-00).pdf
```

冲突副本必须：

- 创建独立文件或独立版本。
- 不覆盖原文件。
- 记录原始文件 ID。
- 写入审计日志。

### Permission

```text
id
resource_type
resource_id
subject_type
subject_id
permission
created_at
updated_at
```

建议初版支持用户级权限：

```text
subject_type = USER
permission = READ | WRITE | OWNER
```

管理员拥有全局管理权限，但管理员访问用户文件时仍然必须记录审计日志。

权限判定与继承规则见 [permissions.md](permissions.md)。

### ShareLink

```text
id
resource_type
resource_id
token_hash
permission
expires_at
max_downloads
download_count
created_by
created_at
revoked_at
```

分享链接只保存 Token 的哈希值，不保存明文 Token。

分享链接访问必须支持：

- 是否过期。
- 是否被撤销。
- 是否达到下载次数。
- 是否允许下载文件夹。
- 是否需要密码。

密码分享属于可选功能，不纳入最小 MVP。

### UploadSession

```text
id
user_id
file_name
folder_id
total_size
chunk_size
total_chunks
received_chunks
expected_hash
status
temp_storage_key
expires_at
created_at
updated_at
```

上传状态：

```text
INITIATED
UPLOADING
MERGING
VERIFYING
COMPLETED
FAILED
ABORTED
EXPIRED
```

`received_chunks` 不建议直接存为不受约束的大 JSON。可以单独设计 `upload_chunks` 表。

### AuditLog

字段定义以本节为准；必须记录的动作清单见 [architecture.md](architecture.md)。

```text
id
actor_id
action
resource_type
resource_id
request_id
ip_address
metadata
created_at
```

## 状态转换

以下两张状态转换表由上文的状态枚举与删除、恢复、后台清理、上传流程等要求推导得出，不是逐字照搬的迁移表；实现时如与 `docs/phases/` 中的阶段任务书冲突，以阶段任务书为准。

### File

| 起始状态 | 触发 | 目标状态 |
| --- | --- | --- |
| `ACTIVE` | 删除文件（事务内） | `TRASHED` |
| `TRASHED` | 文件恢复 | `ACTIVE` |
| `TRASHED` | 后台永久清理任务 | `PURGED` |

### UploadSession

| 起始状态 | 触发 | 目标状态 |
| --- | --- | --- |
| `INITIATED` | 开始上传分块 | `UPLOADING` |
| `UPLOADING` | 所有分块完成，开始合并 | `MERGING` |
| `MERGING` | 合并完成，开始校验哈希 | `VERIFYING` |
| `VERIFYING` | 校验通过并创建版本 | `COMPLETED` |
| 任意进行中状态 | 磁盘空间不足、写入失败等错误 | `FAILED` |
| 任意进行中状态 | 客户端取消上传 | `ABORTED` |
| 任意未完成状态 | 到达 `expires_at` 并被后台任务清理 | `EXPIRED` |

### FileVersion

版本号只增不减：每次成功上传新内容都会追加一条 `file_versions` 记录并把 `is_current` 转移过去，旧版本记录保留。相关规则见上文「版本创建」与「乐观并发控制」。

### 对象（ObjectStorage）

对象状态机 `TEMPORARY` / `READY` / `DELETING` / `DELETED` 的完整定义见 [storage.md](storage.md)。

分块大小默认 8 MiB（可配 1–64 MiB），`chunk_index` 从 0 开始；分块协议与落盘布局见 [storage.md](storage.md)。

## 建议索引

```text
users(username)
users(email)
folders(parent_id, name)
files(folder_id, original_name)
files(owner_id)
files(content_hash)
file_versions(file_id, version_number)
permissions(resource_type, resource_id, subject_id)
upload_sessions(user_id, status)
upload_chunks(upload_session_id, chunk_index)
audit_logs(actor_id, created_at)
```

## 一致性要求

数据库记录和磁盘文件不是同一个事务系统，因此必须明确处理不一致。

上传完成使用以下顺序：

1. 在数据库中创建上传会话。
2. 分块写入临时目录。
3. 数据库记录每个已完成分块。
4. 所有分块完成后合并临时文件。
5. 计算最终哈希。
6. 将临时文件移动到最终对象路径。
7. 在数据库事务中创建版本并更新当前版本。
8. 将上传会话标记为 `COMPLETED`。
9. 写入审计日志。

如果第 6 步之后数据库事务失败，必须保留临时对象或最终对象，并由后台清理任务处理，不能立即删除可能仍需要恢复的文件。

## 事务边界

### 创建文件夹

数据库事务内完成：

1. 检查父目录存在。
2. 检查用户有写权限。
3. 检查同名资源。
4. 创建目录。
5. 写入审计日志。

### 删除文件

数据库事务内完成：

1. 检查用户权限。
2. 检查当前状态。
3. 更新为 `TRASHED`。
4. 写入审计日志。
5. 创建异步清理或延迟删除记录。

文件物理删除不得在数据库事务中长时间执行。

### 完成上传

完成上传不得先改数据库再移对象，否则数据库会指向不存在的文件。对象状态机与对象路径规则见 [storage.md](storage.md)。

## 幂等要求

以下操作必须幂等：

- 重复上传相同分块。
- 重复完成上传。
- 重复取消上传。
- 重复恢复文件。
- 重复删除分享链接。

需要时使用：

```http
Idempotency-Key: <client-generated-key>
```

## 存储统计和配额

用户存储配额必须在以下阶段检查：

1. 创建上传会话时。
2. 上传完成合并前。
3. 创建新版本前。
4. 文件恢复时。
5. 文件复制或去重引用时。

推荐维护：

```text
users.used_storage_bytes
users.storage_quota_bytes
```

但该字段不能完全作为唯一事实来源。

定期任务需要根据有效文件版本重新计算使用空间，并修复统计偏差。

计算规则必须明确：

- 相同物理对象被多个文件引用时，是否重复计算。
- 回收站文件是否占用空间。
- 历史版本是否占用空间。
- 失败上传临时文件是否占用空间。

MVP 推荐：

- 回收站和历史版本占用空间。
- 同一用户重复引用相同内容时按逻辑文件大小计算。
- 物理去重只用于节省磁盘空间。
- 存储统计任务定期校准。

默认值：单文件上限 10 GiB（可配），用户默认配额 20 GiB（可配）。

### 阶段六的实现口径（权威，测试已覆盖）

```text
users.used_storage_bytes = 该用户所有 file_versions.size_bytes 之和
```

即：

| 问题 | 结论 |
|---|---|
| 相同物理对象被多个文件引用 | **按逻辑大小重复计算**：同一用户传两份相同内容算两份配额，物理去重只省磁盘 |
| 回收站文件 | **占用空间**（文件记录还在，版本还在） |
| 历史版本 | **占用空间**（阶段七起每个版本都计入） |
| 失败上传的临时分块 | **不占用**：失败/取消会立刻清掉分块，且从未计入统计 |

强制方式：

- 会话创建时做**预检查**（用客户端声明的大小，只为尽早报错）。
- 提交时在 `FileRepository::CreateWithFirstVersion` 的**同一个事务**里
  `SELECT ... FROM users WHERE id = $1 FOR UPDATE` 锁住用户行，再比对
  `used_storage_bytes + version.size_bytes <= storage_quota_bytes`，
  通过后在同一事务里累加统计。并发上传因此不可能同时通过检查（有集成测试钉死）。
- 超出配额返回 `QUOTA_EXCEEDED`（HTTP 409），上传会话置为 `FAILED` 并清理分块。
- `used_storage_bytes` 只是缓存：`UserRepository::RecalculateUsedStorage` 与迁移 0005 的
  回填使用同一个口径重算，阶段十一的定期任务直接复用。

