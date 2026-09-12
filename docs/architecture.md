# 架构与分层

项目名 lan-file-hub，C++ 命名空间 `lan_file_hub`。

## 系统组成与数据流

```text
浏览器前端
    |
    v
C++ HTTP API 服务
    |
    +-- PostgreSQL：用户、目录、文件元数据、权限、版本、分享链接
    |
    +-- Redis：上传会话、短期缓存、限流、分布式锁
    |
    +-- 本地对象存储：文件内容和文件分块
```

第一阶段使用本地磁盘作为对象存储。对象存储经 `ObjectStorage` 抽象访问，实现可替换：

```text
LocalObjectStorage
S3ObjectStorage
```

业务层不得直接调用 `std::filesystem` 访问任意用户路径。所有文件内容访问必须经过对象存储接口。

对象存储的接口定义、路径规则与一致性处理见 [storage.md](storage.md)。

## 技术栈

### 后端

| 项目 | 选型 |
| --- | --- |
| 语言标准 | C++20 |
| 构建系统 | CMake + CMakePresets + Ninja |
| 依赖管理 | vcpkg（manifest 模式） |
| HTTP 框架 | Drogon |
| 数据库 | PostgreSQL |
| 数据库客户端 | libpqxx |
| 缓存 | Redis |
| 对象存储 | 本地文件系统（经 `ObjectStorage` 抽象） |
| 加密与哈希 | OpenSSL；哈希算法 SHA-256 |
| 日志 | spdlog |
| 格式化 | fmt |
| JSON | nlohmann/json（Protobuf 不在本版范围） |
| 单元测试 | GoogleTest |
| 性能测试 | Google Benchmark |
| 本地依赖服务 | Docker Compose |
| 脚本 | PowerShell（`.ps1`） |

- 编译工具链：MSVC 14.51（`D:\VS2026`，自带 CMake 与 Ninja）。
- vcpkg 本体计划安装在 `D:\Codex Tools\vcpkg`，**尚未安装**。
- Drogon 用于完成 HTTP API。文件传输和并发上传逻辑必须独立于具体 HTTP 框架，便于测试和替换。

### 前端

| 项目 | 选型 |
| --- | --- |
| 框架 | React |
| 语言 | TypeScript |
| 构建工具 | Vite |
| UI 组件库 | Ant Design |
| 服务端状态 | TanStack Query |
| 路由 | React Router |
| HTTP 客户端 | Axios（分块上传需要 `onUploadProgress`，只有 XHR 支持，Axios 原生提供） |
| 测试 | Vitest |
| 端到端测试 | Playwright |

前端必须是可使用的管理界面，普通用户不能依赖修改配置文件或执行命令完成上传、下载、建目录、分享和恢复操作。

### 工程工具

| 工具 | 用途 |
| --- | --- |
| `clang-format` | 格式化 |
| `clang-tidy` | 静态检查 |
| `cmake-format` | CMake 文件格式化 |
| GoogleTest | 测试 |
| AddressSanitizer | 内存错误检测 |
| UndefinedBehaviorSanitizer | 未定义行为检测 |
| ThreadSanitizer | 数据竞争检测 |
| GitHub Actions | 持续集成 |
| Docker Compose | 本地依赖服务编排 |

## 目录结构

```text
lan-file-hub/
├── AGENTS.md
├── README.md
├── LICENSE
├── CMakeLists.txt
├── CMakePresets.json
├── .clang-format
├── .clang-tidy
├── docker-compose.yml
├── Dockerfile
├── backend/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── lan_file_hub/
│   │       ├── api/
│   │       ├── application/
│   │       ├── domain/
│   │       ├── infrastructure/
│   │       ├── storage/
│   │       └── common/
│   ├── src/
│   │   ├── api/
│   │   ├── application/
│   │   ├── domain/
│   │   ├── infrastructure/
│   │   ├── storage/
│   │   └── main.cpp
│   └── tests/
│       ├── unit/
│       ├── integration/
│       └── fixtures/
├── frontend/
│   ├── package.json
│   ├── src/
│   │   ├── app/
│   │   ├── pages/
│   │   ├── components/
│   │   ├── features/
│   │   ├── services/
│   │   ├── hooks/
│   │   └── types/
│   └── tests/
├── migrations/
├── docs/
│   ├── architecture.md
│   ├── api.md
│   ├── storage.md
│   ├── permissions.md
│   └── recovery.md
├── scripts/
│   ├── dev.sh
│   ├── test.sh
│   └── seed_demo_data.sh
└── .github/
    └── workflows/
        ├── backend.yml
        ├── frontend.yml
        └── docker.yml
```

## 后端分层约束

```text
HTTP Controller
    ↓
Application Service
    ↓
Domain Model / Domain Service
    ↓
Repository / Object Storage / Redis
```

### API 层

负责：

- 解析请求。
- 校验基础参数。
- 提取用户身份。
- 调用 Application Service。
- 转换响应。
- 映射统一错误码。

API Controller 不得：

- 直接拼接 SQL。
- 直接操作数据库连接。
- 直接拼接磁盘路径。
- 实现完整业务流程。
- 自己管理跨请求的业务状态。

### Application 层

负责：

- 编排业务流程。
- 事务边界。
- 权限检查调用。
- 调用文件存储。
- 管理业务状态变化。
- 处理幂等请求。

### Domain 层

负责：

- 文件、文件夹、版本、分享链接、权限等领域模型。
- 状态转换规则。
- 业务约束。
- 不依赖 HTTP、PostgreSQL 或 Redis。

### Infrastructure 层

负责：

- PostgreSQL Repository。
- Redis Repository。
- JWT 服务。
- 哈希服务。
- 文件系统实现。
- 日志、配置、指标。

## C++ 实现规范

### 语言标准

使用 C++20。

优先使用：

- `std::unique_ptr`
- `std::shared_ptr`
- `std::span`
- `std::string_view`
- `std::filesystem`
- `std::expected` 的项目替代实现或统一 Result 类型
- `std::jthread`
- `std::stop_token`
- `std::chrono`
- `std::mutex`
- `std::shared_mutex`
- `std::condition_variable`
- `std::atomic`

`std::filesystem` 只用于基础设施内部实现，不得用它访问用户路径，见上文「系统组成与数据流」。

### 内存管理

- 默认使用 RAII。
- 禁止裸 `new` 和裸 `delete`。
- 不返回指向局部变量的引用或指针。
- 明确对象所有权。
- 不使用不必要的 `shared_ptr`。
- 不在异常路径泄漏文件句柄、锁或临时资源。

### 错误处理

统一使用 Result 或异常策略，不能混用得毫无规则。

推荐：

- 业务预期错误使用 `Result<T, Error>`。
- 数据库连接失败、磁盘 IO 失败等基础设施错误转换成统一错误类型。
- 不允许吞掉异常。
- Controller 层统一转换错误码和 HTTP 状态。
- 错误响应不能暴露绝对路径、SQL、内部堆栈或密钥。

### 文件 IO

- 使用二进制模式。
- 对大文件使用流式读写。
- 不将完整文件读入 `std::string` 或 `std::vector`。
- 每次读写使用有限大小缓冲区。
- 检查短读、短写和磁盘错误。
- 检查磁盘空间。
- 临时文件必须使用安全生成的名称。
- 合并文件时必须验证分块数量和顺序。

### 编码规范

遵循 Google C++ Style Guide：

- 类型使用 `PascalCase`。
- 函数和变量使用 `snake_case`。
- 常量使用项目统一规则。
- 每行长度遵循 `.clang-format`。
- 头文件使用 include guard 或 `#pragma once`。
- 头文件不能包含不必要的实现。
- 避免循环依赖。
- 公共接口必须有清晰注释。
- 注释解释原因，不重复代码行为。

## 并发模型

### HTTP 请求线程

HTTP 层负责：

- 接收请求。
- 解析请求。
- 读取分块数据。
- 调用应用服务。
- 返回响应。

### 后台任务线程

后台线程负责：

- 临时上传清理。
- 过期上传会话清理。
- 永久删除任务。
- 文件哈希计算。
- 病毒扫描接口调用。
- 存储统计更新。
- 审计日志异步写入。

### 线程安全要求

- 不共享可变对象，除非有明确同步策略。
- 优先使用消息传递和任务队列。
- 不在锁内执行慢速 IO。
- 锁的作用域必须尽可能小。
- 不允许持有数据库锁时访问网络或磁盘。
- 所有后台任务都必须支持停止。
- 服务退出时必须等待 Worker 有序退出。

### 资源限制

必须配置：

```text
最大并发上传数
单用户最大并发上传数
单个文件最大大小
单个分块最大大小
最大下载并发数
最大请求体大小
最大文件夹深度
最大文件名长度
磁盘最小剩余空间
```

## Redis 使用边界

Redis 用于临时和高频状态，不作为文件内容的唯一存储。

Redis 保存：

```text
上传会话缓存
上传限流计数
下载限流计数
短期分享访问限制
分布式锁
短期用户会话
```

数据库仍然必须保存上传会话的持久状态，保证 Redis 重启后可以恢复。

Redis 锁必须：

- 设置过期时间。
- 使用唯一锁值。
- 释放时校验锁拥有者。
- 不能永久阻塞业务。
- 获取失败时返回明确错误或进行有限重试。

## 上传并发控制

上传路径上必须同时限制的并发维度见上文「并发模型 → 资源限制」清单，其中与上传直接相关的四项是：

```text
全局并发上传数
单用户并发上传数
单上传会话并发分块数
单 IP 并发请求数
```

限制手段：

- 进程内信号量限制当前进程资源。
- Redis 计数限制多实例资源。
- 数据库状态防止同一上传会话被重复完成。
- 上传会话锁防止并发合并。

不得使用无限制创建线程的方式处理上传。

推荐队列模型：

```text
HTTP 接收分块
    ↓
有限容量任务队列
    ↓
固定数量 IO Worker
    ↓
对象存储
```

队列满时返回：

```text
503 STORAGE_BUSY
```

或者让请求等待一个有限时间后返回错误。

## 可观测性

### 日志字段

所有请求生成唯一 `request_id`。

日志至少包含：

```text
timestamp
level
request_id
user_id
method
path
status_code
duration_ms
error_code
```

上传相关日志还应包含：

```text
upload_id
file_id
chunk_index
chunk_size
```

禁止记录：

```text
password
jwt
share_token
database_password
完整文件内容
```

### 监控指标

后端至少提供：

```text
http_requests_total
http_request_duration_seconds
http_errors_total
upload_sessions_active
upload_chunks_total
upload_failures_total
upload_bytes_total
download_bytes_total
storage_used_bytes
storage_free_bytes
database_connections
redis_errors_total
background_jobs_active
background_jobs_failed
```

### 健康检查与管理端点

```http
GET /health/live
GET /health/ready
GET /metrics
```

含义：

- `live`：进程是否正常运行。
- `ready`：数据库、必要依赖和存储是否可用。
- `metrics`：Prometheus 指标。

健康检查不能因为某一个非核心组件短暂不可用而导致进程崩溃。

## 审计日志

审计日志的字段定义、表结构与索引见 [data-model.md](data-model.md)。本节规定必须记录的动作。

### 动作清单

```text
LOGIN
CREATE_FOLDER
UPLOAD_INITIATED
CHUNK_UPLOADED
FILE_COMPLETED
FILE_DOWNLOADED
FILE_MOVED
FILE_DELETED
FILE_RESTORED
SHARE_CREATED
SHARE_REVOKED
PERMISSION_CHANGED
```

管理员访问用户文件时仍然必须记录审计日志。

## 管理员权限边界

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

管理员不能：

- 直接绕过审计读取文件。
- 修改系统数据库中的密码哈希。
- 直接通过用户输入指定服务器路径。
- 删除正在使用的系统目录。
