# AGENTS.md — lan-file-hub

局域网文件同步与团队文件服务：面向个人和中小团队的私有文件管理与局域网共享系统，支持用户、文件夹、
上传下载、断点续传、文件版本、回收站、分享链接和资源权限。

本文件只写**每次会话都必须遵守的规则**和**必须知道的命令**。设计细节按需查阅：

| 需要什么 | 看哪里 |
|---|---|
| 产品定位、目标用户、边界 | `docs/brief.md` |
| 系统组成、分层、并发模型、日志与指标 | `docs/architecture.md` |
| 表结构、状态枚举、事务边界、配额 | `docs/data-model.md` |
| 端点、错误码、分页、认证方案 | `docs/api.md` |
| 对象存储、路径安全、分块协议、去重 | `docs/storage.md` |
| 权限模型、分享链接 | `docs/permissions.md` |
| 安全要求与必须覆盖的攻击场景 | `docs/security.md` |
| 故障处置、后台任务、备份恢复 | `docs/recovery.md` |
| 页面、前端状态机、上传算法 | `docs/frontend.md` |
| 测试清单与验收标准 | `docs/testing.md` |
| 配置项、容器、部署、发布产物 | `docs/deployment.md` |
| 本次精简的来龙去脉、待确认事项 | `docs/plan.md` |
| 阶段任务书（编号唯一权威） | `docs/phases/` |

全局规则（项目与临时目录约定、工具位置、Git/GitHub 流程、密钥与隐私、代码风格、汇报格式）见
`E:\dsh\home\AGENTS.md`，本文件不重复。

---

## 1. 范围

**做**：浏览器可用的文件管理界面、大文件分块上传与真正可恢复的断点续传、服务端 SHA-256 校验与去重、
文件版本、回收站与恢复、用户级资源权限、分享链接、Docker Compose 启动、可测试可恢复可部署。

**不做**（MVP 阶段不得引入，也不得为"以后可能用到"提前实现）：多节点自动同步、P2P 传输、
局域网节点发现、桌面同步客户端、S3 协议兼容、在线 Office 编辑、在线视频转码、真实病毒扫描引擎、
多租户计费、企业单点登录、分布式一致性协议、端到端加密、AI 文件分类与语义搜索。

可以预留接口，但不得为空泛的"未来扩展"引入抽象或分布式系统。

本项目**与 AI 无关**：不引入 AI 能力，不做语义搜索、智能分类或推荐，不依赖任何模型服务、
向量库或推理框架。它就是一个文件存储与共享系统。

## 2. 技术决策（已定，不要再来问）

| 项 | 决策 |
|---|---|
| 语言 | C++20 |
| 编译器 | MSVC 14.51（`D:\VS2026`） |
| 构建 | CMake + CMakePresets + Ninja |
| 依赖管理 | vcpkg manifest 模式（`vcpkg.json`），本体装在 `D:\Codex Tools\vcpkg` |
| HTTP 框架 | Drogon；文件传输与并发上传逻辑必须独立于 HTTP 框架 |
| 数据库 | PostgreSQL（libpqxx） |
| 缓存 | Redis（只存临时与高频状态，不作唯一事实来源） |
| 对象存储 | 本地文件系统，经 `ObjectStorage` 抽象；业务层不得直接访问用户路径 |
| 认证 | 短期 JWT（单 Token）+ 统一认证中间件；密码用 Argon2id |
| 哈希 | SHA-256 |
| JSON | nlohmann/json |
| 分块 | 默认 8 MiB（可配 1–64 MiB），`chunk_index` 从 0 开始 |
| 单文件上限 | 10 GiB（可配） |
| 用户默认配额 | 20 GiB（可配） |
| 删除策略 | 软删除 + 后台永久清理 |
| 权限 | 用户级 READ / WRITE / OWNER，管理员 ADMIN |
| 前端 | React + TypeScript + Vite + Ant Design + TanStack Query + React Router |
| 前端 HTTP | Axios（分块上传需要 `onUploadProgress`，XHR 才支持） |
| 测试 | GoogleTest（C++）、Vitest + Playwright（前端）、Google Benchmark（性能） |
| 本地依赖服务 | Docker Compose 跑 PostgreSQL 与 Redis |
| 脚本 | PowerShell `.ps1`（Windows 开发环境），不提供只写 `.sh` 的脚本 |

## 3. 目录结构

```text
lan-file-hub/
├── AGENTS.md  README.md  LICENSE  CHANGELOG.md
├── CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy
├── Dockerfile  docker-compose.yml  .env.example  .gitignore
├── backend/
│   ├── include/lan_file_hub/{api,application,domain,infrastructure,storage,common}/
│   ├── src/{api,application,domain,infrastructure,storage}/  src/main.cpp
│   └── tests/{unit,integration,fixtures}/
├── frontend/src/{app,pages,components,features,services,hooks,types}/  frontend/tests/
├── migrations/
├── docs/{brief,architecture,data-model,api,storage,permissions,security,recovery,frontend,testing,deployment,plan}.md
├── docs/phases/                 # 阶段任务书
├── scripts/                     # PowerShell 脚本：build / test / format / lint / seed
└── .github/workflows/{backend.yml,frontend.yml,docker.yml}
```

临时文件统一放 `E:\DeepSeekProjects\_tmp\lan-file-hub\`（全局规则）。

## 4. 命令

本节的命令**必须与仓库实际一致**。阶段一落地脚本后回来核对；不一致时改本节，不要改代码去迁就文档。

```powershell
# 构建与测试（阶段一创建，CMakePresets 至少提供 debug / release / asan / tsan）
.\scripts\build.ps1 -Preset debug
.\scripts\test.ps1  -Preset debug
.\scripts\format.ps1 -Check          # clang-format
.\scripts\lint.ps1                   # clang-tidy

# 前端
cd frontend; npm ci; npm run dev; npm run test; npm run lint; npx playwright test

# 依赖服务与整体启动
docker compose up -d postgres redis
docker compose up --build
```

阶段一之前脚本还不存在，等价的裸命令是 `cmake --preset debug` / `cmake --build --preset debug` /
`ctest --preset debug`。

工具位置（`cmake` 与 clang 系**不在 PATH**，脚本里集中写绝对路径，不要散落到文档和 CI 中）：

```text
CMake 3.31.6   D:\Codex Tools\CMake\cmake-3.31.6-windows-x86_64\bin\cmake.exe
MSVC + Ninja   D:\VS2026（自带 CMake 与 Ninja；或走 VS 开发者环境）
clang-format   D:\Codex Tools\LLVM\bin\clang-format.exe    19.1.0
clang-tidy     D:\Codex Tools\LLVM\bin\clang-tidy.exe      19.1.0
ninja / node / npm / git / docker   已在 PATH
```

> ⚠️ `D:\Codex Tools\clang-format\bin\clang-format.exe` 与 `clang-format18\bin\` 下的同名文件
> **不可用**（`--version` 退出码 1、无任何输出，是坏壳）。真正的二进制在各自
> `clang_format\data\bin\` 下；直接用上面 LLVM 那一份，与 clang-tidy 同版本。

## 5. 硬约束（红线）

分层：`HTTP Controller → Application Service → Domain → Repository / ObjectStorage / Redis`。

- Controller 不得：拼 SQL、管数据库连接、拼磁盘路径、实现完整业务流程、持有跨请求业务状态。
- 业务层不得用 `std::filesystem` 访问用户路径；文件内容一律经 `ObjectStorage`。
- 用户的原始文件名只作元数据，**永远不参与物理路径拼接**；对象 Key 由服务端生成。
- 大文件全程流式：不把整个文件读进内存，不用无界缓冲区，检查短读短写与磁盘错误。
- 禁止裸 `new` / `delete`；文件句柄、锁、临时资源一律 RAII；`std::jthread` + `stop_token` 管线程。
- 不吞异常；业务预期错误用 Result 类型，基础设施错误转统一错误类型，Controller 统一映射 HTTP 状态。
- 不在锁内做磁盘或网络 IO；不持有数据库锁时访问网络；锁的作用域尽可能小。
- 所有后台线程必须可停止，服务退出时有序 join；不用全局可变状态保存业务数据。
- C++ 遵循 Google C++ Style：类型与函数 `PascalCase`，变量 `snake_case`，私有成员后缀 `_`，
  常量 `k` 前缀，以 `.clang-format` / `.clang-tidy` 为准。
- 新增业务功能同步新增测试；新增 API 同步更新 `docs/api.md`；不引入当前阶段不需要的依赖。
- 不用 TODO 或空函数代替核心逻辑；不返回未经验证的成功结果。
- 无法实际验证的功能，报告里必须写"未验证"，不得描述为已完成。

## 6. 数据与一致性要点

细节见 `docs/data-model.md`。

- 表：`users` `folders` `files` `file_versions` `permissions` `share_links` `upload_sessions`
  `upload_chunks` `audit_logs`。
- 状态枚举：File `ACTIVE/TRASHED/PURGED`；UploadSession `INITIATED/UPLOADING/MERGING/VERIFYING/
  COMPLETED/FAILED/ABORTED/EXPIRED`；对象 `TEMPORARY/READY/DELETING/DELETED`。
- 上传完成的顺序不能反：分块落盘 → 合并 → **服务端重算大小与 SHA-256** → 对象进最终路径 →
  再在事务里创建版本、更新 `current_version_id`。先改数据库再挪对象 = 数据库指向不存在的文件。
- 数据库与磁盘不是同一个事务系统：失败遗留的对象交给后台清理任务，不立即删除。
- 必须幂等：重复上传相同分块、重复完成上传、重复取消上传、重复恢复文件、重复删除分享链接。
- 版本冲突不静默覆盖：返回 409 与当前版本，客户端可用 `If-Match`。

## 7. API 约定

细节与完整错误码见 `docs/api.md`。

- 前缀 `/api/v1`；成功 `{data, request_id}`；失败 `{error:{code,message,details}, request_id}`。
- 每个请求生成唯一 `request_id`；错误码只能用 `docs/api.md` 的表，不得自造。
- 列表接口必须分页：`1 <= page_size <= 100`，`sort_by` 走白名单，禁止把用户输入拼进 SQL 或路径。
- 错误响应不得暴露绝对路径、SQL、堆栈或密钥。

## 8. 安全红线

细节与必须覆盖的攻击场景见 `docs/security.md`。

- 权限一律在后端校验；前端隐藏按钮不算权限控制。前端不会发非法请求也不能作为安全依据。
- 受保护接口一律走认证中间件；被禁用用户不得登录或访问；关键操作要复查用户状态。
- 密码只存哈希；分享 Token 只存哈希，明文只在创建时返回一次。
- `JWT_SECRET`、数据库密码只来自环境变量，不得进源码、日志或仓库。
- 上传文件名与 `Content-Disposition` 必须净化；拒绝 `..`、绝对路径与控制字符。
- 密码、Token、密钥、完整文件内容不得进日志；CORS 用明确白名单，不用 `*`。
- 本期没有真实病毒扫描（`NoopFileScanner`），README 必须写明这一点。
- 管理员访问用户资源必须留审计记录，不得静默读取。

## 9. 前端红线

细节见 `docs/frontend.md`。

- TanStack Query 管服务端数据；未完成上传任务存 IndexedDB，刷新后从后端上传会话恢复状态。
- 密码不进 LocalStorage；Token 处理集中实现，不允许各页面各写一套。
- 所有 API 错误展示用户可理解的消息；所有破坏性操作必须二次确认。
- 页面状态与上传状态必须区分（枚举见 `docs/frontend.md`），不得用 loading / error 两个值糊弄。
- 不能假设分块按顺序到达；重试只对可重试错误做，并有次数上限。

## 10. 阶段与执行方式

- 阶段任务书在 `docs/phases/`，编号为**阶段零～阶段十七**，这是唯一权威；本文件不再另立阶段表。
- 阶段零～十构成可发布的 MVP；阶段十一～十七是可靠性、运维与发布增强，不阻塞最初演示，
  但项目定位是**工业级、公司可采用**，所以这七项是**发布前必做**，不是可选项。
- 一次会话只执行一个阶段。开工前先读该阶段任务书、本文件、以及该阶段涉及的 `docs/` 文档。
- 阶段零只做审计、记录和必要文档，不实现业务功能。
- 前一阶段的验收命令跑通后再进入下一阶段；上一阶段遗留的未验证项必须显式带进下一阶段报告。

## 11. 验证

- 每次改动后跑与改动相关的检查：格式化、静态检查、构建、单元测试、集成测试。
- 阶段结束时必须执行该阶段任务书里的验收命令，并在报告中给出真实输出。
- 因环境、依赖、网络或权限跑不起来时，报告必须写明：哪些没跑、为什么、风险是什么。
- 不得把"命令已启动"说成"验证通过"；不得为了让测试通过而删除或跳过测试。

## 12. 提交

- 提交信息用 `feat:` / `fix:` / `test:` / `docs:` / `refactor:` / `build:` / `chore:` 前缀加英文简述，
  一次提交只包含一个逻辑变化。
- 不得提交：`.env`、真实密钥、本地上传的文件、数据库数据目录、编译产物、IDE 配置、大体积测试文件。
- `.gitignore` 至少覆盖 `/build/`、`/out/`、`/dist/`、`/node_modules/`、`/.env`、`/storage/`、`*.log`、`*.tmp`。
- Git 与 GitHub 的操作流程、检查项和需要确认的动作见全局 `AGENTS.md` 第 5 节。

## 13. 待确认事项

工具相关的一切（vcpkg、Docker、编译器、格式化工具、依赖服务）按全局 `AGENTS.md` 第 2、3 节执行：
额外工具放 `D:\Codex Tools`，安装前先列目录确认没有现成的，**不必逐项询问**。

**项目定位已确认**：自用起步，按工业级标准做，目标是公司可采用——安全、审计、故障恢复、
可运维性是交付标准而不是加分项（见 `docs/brief.md`）。**与 AI 无关**，不引入任何 AI 能力。

执行节奏已确认：**不设时间预期**，按阶段稳步推进；全程**本地开发**，
Git 远程仓库与上传留到项目末尾再做。

| # | 事项 | 影响 | 谁来定 |
|---|---|---|---|
| 1 | 许可证类型 | 仓库产物，不影响开发 | 阶段十六发布前再定；私有自用可以不写 `LICENSE` |

许可证的取舍见 `docs/plan.md` §10。
