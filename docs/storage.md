# 存储设计

> 实现状态（2026-09-12）：**对象存储抽象、本地实现、单文件流式上传与 Range 下载已实现**
> （阶段四）。分块上传协议（§分块上传协议）在阶段五实现。
>
> 落地时的接口与下面这份草案有两处调整，以代码为准：
> - 用 `ObjectWriter`（流式写入句柄）+ `Commit(temp_key, final_key)` 取代
>   `WriteChunk` / `MergeChunks`：单文件上传不需要按分块寻址，写入过程由
>   `FileUploadSession` 驱动，边写边算 SHA-256。分块相关方法在阶段五以同一风格补充。
> - `OpenRead` 改为 `ResolveDownloadPath`：下载由 HTTP 层的 `newFileResponse` 完成，
>   它自己会分块发送，业务层不需要自己持有读句柄。

## 对象存储抽象

本地对象存储定义接口：

```cpp
class ObjectStorage {
 public:
  virtual ~ObjectStorage() = default;

  virtual StorageResult WriteChunk(
      std::string_view upload_id,
      uint64_t chunk_index,
      std::span<const std::byte> data) = 0;

  virtual StorageResult MergeChunks(
      std::string_view upload_id,
      std::string_view destination_key,
      uint64_t total_chunks) = 0;

  virtual ReadHandle OpenRead(std::string_view storage_key) = 0;

  virtual StorageResult Delete(std::string_view storage_key) = 0;

  virtual bool Exists(std::string_view storage_key) const = 0;
};
```

实际接口名称可以根据代码风格调整，但业务层必须依赖抽象接口。

业务层不得用 `std::filesystem` 访问用户路径，文件内容一律经 `ObjectStorage`（见 `../AGENTS.md` 第 5 节）。

### 本地实现的目录布局

```text
STORAGE_ROOT/
├── objects/<sha256 前两位>/<sha256>   最终对象，内容寻址
└── tmp/<uuid>                         写入中的临时对象，提交或中断后即清理
```

对象 Key 由服务端生成，**原始文件名只作元数据**；每次落盘前都会重新校验
规范化后的路径仍位于 `STORAGE_ROOT` 之内（`LocalObjectStorage::ResolveKey`）。

## 路径与对象 Key 安全

禁止：

```cpp
base_path / user_input_filename
```

必须：

- 使用数据库生成的 UUID 或服务器生成的对象 Key。
- 原始文件名只保存为元数据。
- 对文件夹名称和展示名称做校验。
- 使用规范化路径后再次检查目标路径仍在存储根目录内。
- 拒绝 `..`、绝对路径和非法控制字符。

## 分块上传协议

推荐使用 REST API。

### 初始化上传

```http
POST /api/v1/uploads
Authorization: Bearer <token>
Content-Type: application/json
```

请求：

```json
{
  "folder_id": "folder-uuid",
  "file_name": "report.pdf",
  "size_bytes": 1073741824,
  "sha256": "optional-client-sha256",
  "chunk_size": 8388608
}
```

响应：

```json
{
  "upload_id": "upload-uuid",
  "chunk_size": 8388608,
  "total_chunks": 128,
  "already_exists": false,
  "completed_chunks": []
}
```

约束：

- 默认分块大小为 8 MiB。
- 允许配置范围，例如 1 MiB 到 64 MiB。
- 单个上传会话必须限制最大文件大小（默认 10 GiB，可配置）。
- 用户配额必须在初始化和完成阶段分别检查。

### 上传分块

```http
PUT /api/v1/uploads/{upload_id}/chunks/{chunk_index}
Authorization: Bearer <token>
Content-Type: application/octet-stream
X-Chunk-Sha256: <chunk-sha256>
```

要求：

- `chunk_index` 从 0 开始。
- 重复上传同一分块必须幂等。
- 如果相同索引的内容哈希相同，返回成功。
- 如果内容不同，返回冲突错误。
- 分块写入临时文件时不得一次性载入整个文件。
- 服务端需要限制单请求最大 body 大小。
- 必须校验当前用户是该上传会话的所有者。

### 查询上传状态

```http
GET /api/v1/uploads/{upload_id}
Authorization: Bearer <token>
```

响应：

```json
{
  "upload_id": "upload-uuid",
  "status": "UPLOADING",
  "total_chunks": 128,
  "completed_chunks": [0, 1, 2, 5, 6],
  "expires_at": "2025-01-01T00:00:00Z"
}
```

前端根据该接口决定从哪些分块继续上传。

### 完成上传

```http
POST /api/v1/uploads/{upload_id}/complete
Authorization: Bearer <token>
```

服务端必须：

- 检查所有分块是否存在。
- 检查上传会话状态。
- 防止并发完成。
- 合并分块。
- 计算最终文件大小。
- 计算最终 SHA-256。
- 校验客户端声明的 SHA-256。
- 创建文件版本。
- 更新文件元数据。
- 清理临时状态。
- 写入审计日志。

### 取消上传

```http
DELETE /api/v1/uploads/{upload_id}
Authorization: Bearer <token>
```

取消后：

- 上传会话变为 `ABORTED`。
- 后续分块上传返回明确错误。
- 临时文件可以立即清理，也可以交给异步清理任务。
- 已完成的正式文件不得被误删。

## 下载设计

### 普通下载

```http
GET /api/v1/files/{file_id}/download
Authorization: Bearer <token>
```

要求：

- 下载前执行权限检查。
- 使用流式传输。
- 不得一次性将整个文件读入内存。
- 支持 `Range` 请求。
- 支持 `Content-Length`。
- 设置正确的 `Content-Type`。
- 默认使用元数据中的原始文件名作为下载名称。
- 文件不存在或对象丢失时返回明确错误并记录日志。

### 下载限速

MVP 可以实现用户级或请求级限速。

限速必须：

- 不阻塞整个 Worker 线程池。
- 能够在客户端断开时取消传输。
- 使用令牌桶或基于时间的限速策略。
- 在配置中设置默认值。
- 对管理员和普通用户使用不同配置属于可选功能。

**阶段十二实现**（`common/byte_rate_limiter.*` + `api/download_streamer.*`）：

- 配置项 `DOWNLOAD_RATE_LIMIT`（字节/秒），**默认 0 = 不限速**；限速桶按用户（公开下载按来源 IP，不用 Token 做 key，
  因为 key 会进日志而 Token 不允许进日志）。
- 令牌桶：突发额度 = 1 秒的量，令牌不足时用事件循环定时器（`runAfter`）排队，**不 sleep**，不占用工作线程。
- 启用限速时下载改走异步 chunked 流，因此**没有 `Content-Length`**（Range 仍可用：返回 206 + `Content-Range`）；
  默认不限速时继续走 `newFileResponse` 的零拷贝路径，`Content-Length` 与断点续传语义不变。
- 客户端断开必须停止读盘。实测**只信 `stream->send()` 的返回值不够**：连接断开后它仍会返回 true（数据进了发送缓冲），
  于是服务端会把整个文件"发"给一个已经走掉的客户端。因此判据有两层：连接存活状态（`getConnectionPtr()` +
  `connected()/disconnected()`）以及「连接 10 秒内一个字节都没写进内核」的停滞看门狗（`bytesSent()` 无增长）。
  另外 Drogon 以 `nullptr` 回调通知结束时也会立即停止。
- 验收：`e2e-phase12.ps1` 场景 G（限速生效、内容一致、Range、断开取消）与场景 H（默认路径未被改变）。

## 去重设计

使用 SHA-256 判断内容是否已经存在。

### 去重流程

1. 客户端可以在上传初始化时提交文件大小和 SHA-256。
2. 服务端查询是否存在相同哈希。
3. 如果存在且用户有权引用该对象，可以直接创建新的文件元数据或版本。
4. 如果不存在，创建正常上传会话。
5. 上传完成后服务端必须重新计算 SHA-256，不能只相信客户端。

文件去重不能绕过权限检查。

### 阶段六的实现（已落地，行为以此为准）

| 环节 | 实现 |
|---|---|
| 物理去重 | 对象按内容寻址（`objects/<hash 前两位>/<hash>`），`Commit` 到已存在的 key 直接复用，不重复占盘 |
| 初始化预检查 | `POST /api/v1/uploads` 在客户端提交 `sha256` 时查**该用户自己的**文件版本：命中且大小一致 → 响应 `already_exists: true` 与 `existing_file` 摘要 |
| 秒传（引用已有对象） | 命中后客户端可以不传任何分块，直接 `POST .../complete`；服务端用**自己数据库里的**对象建新文件记录 |
| 完成时二次去重 | 正常上传路径在合并后算出的哈希若已有对象，`Commit` 复用该对象；日志记录去重命中 |
| 跨用户 | 物理对象可以跨用户共享（第二个用户上传同样内容时不会重复占盘），但**预检查只回答自己的内容**，不回答「系统里有没有这个哈希」，避免上传接口变成文件存在性探测工具 |
| 权限 | 引用前仍走目标目录的写权限检查；`existing_file` 只返回用户自己文件的 id、名字与大小，不含路径 |
| 安全边界 | 秒传判定用的是服务端记录（哈希 + 对象大小），客户端谎报哈希最多得到「自己已有文件的副本」，不会读到别人的内容 |
| 引用计数 | 不设独立计数器：`SELECT count(*) FROM file_versions WHERE content_hash = ?`。删除文件记录后引用数自然下降，计数不会漂移 |
| 删除物理对象 | 只有引用计数为 **0** 时才允许删除对象（阶段八的删除与阶段十一的清理任务按此判定）；阶段六已用测试钉死 |

### 场景一：完全相同文件

用户上传相同 SHA-256 和大小的文件：

- 服务端仍然重新确认对象是否存在。
- 确认对象完整性。
- 创建新的逻辑文件或版本记录。
- 不重复保存内容。

### 场景二：客户端哈希错误

客户端声明哈希为 `A`，服务端计算结果为 `B`：

- 上传状态变为 `FAILED`。
- 不创建正式文件版本。
- 临时对象进入清理队列。
- 返回 `HASH_MISMATCH`。
- 记录审计日志和错误原因。

### 场景三：哈希碰撞防御

SHA-256 作为内容寻址的主要标识，但在最终引用对象前仍应校验：

```text
大小
哈希
对象是否可读
```

对于高安全场景，可以增加对象元数据和完整性校验。

## 对象状态机与存储一致性

必须避免以下错误顺序：

```text
先更新数据库为完成
再尝试移动对象文件
```

因为对象移动失败会造成数据库指向不存在的文件。

推荐使用对象状态：

```text
TEMPORARY
READY
DELETING
DELETED
```

只有对象成功写入最终路径后，文件版本才能引用它。

与之配套的一致性要求：

- 完成上传的顺序约束见 `../AGENTS.md` 第 6 节；数据库与磁盘不是同一个事务系统。
- 引用对象前必须校验大小、哈希与对象是否可读（见本文「哈希碰撞防御」）。
- 失败或取消遗留的临时对象进入清理队列，不立即删除；已完成的正式文件不得被误删。
- 数据库事务边界与状态枚举见 `data-model.md`。
