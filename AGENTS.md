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
.\scripts\build.ps1 -Preset debug            # 也可 -Preset release / asan
.\scripts\test.ps1  -Preset debug            # 也可 -Preset release / asan
.\scripts\test.ps1  -Preset asan -Label unit # 只跑单元测试（不打库，快；ASan 下 160/160）
.\scripts\format.ps1 -Check                  # clang-format
.\scripts\lint.ps1                           # clang-tidy

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
- **集成测试必须串行**：`ctest -j N` 并行跑会互相抢同一个数据库，出现大批"创建根目录失败"
  这类**假失败**（实测 80 个）。串行（ctest 默认）才对。

## 11.1 Git、GitHub 与 CI（2026-09-12 首次发布时踩到的坑）

仓库已发布：`https://github.com/klaraljy/lan-file-hub`（**私有**，默认分支 `main`）。
本地目录名与仓库名一致，远程为 `origin`。

### 推送

- **直连 GitHub 经常被重置**（`Recv failure: Connection was reset`）。用户开着代理时走代理推送：

  ```powershell
  git -c http.proxy=http://127.0.0.1:65532 -c https.proxy=http://127.0.0.1:65532 push
  ```

- 推送前按全局规则检查：`git status` / `git diff` / `git remote -v` / `git branch --show-current`，
  并确认没有 `.env`、转储、`storage/`、依赖树混进去。
- **`.gitignore` 陷阱**：`/vcpkg_installed/` 有忽略，但阶段十四新增的 `/vcpkg_installed_asan/`
  一开始**没有**，导致一次提交里混进 1875 个 Postgres 头文件。**新增任何依赖目录/构建目录后，
  立刻补 `.gitignore` 与 `.dockerignore`**。同理 `/backups/`（含数据库转储与用户文件）必须忽略。

### 工作流（`.github/workflows/`）

| 工作流 | 内容 |
|---|---|
| `backend.yml` | `clang-format` 作业 + `build & test` 作业（Postgres/Redis 服务容器 + 跑迁移 + ctest，`LAN_FILE_HUB_REQUIRE_DB=1`） |
| `frontend.yml` | `tsc --noEmit`、`npm run lint`、`npm test`、`npm run build` |
| `docker.yml` | `docker compose config` + Dockerfile 语法检查 |

> **CI 现在不作为阻塞项**（用户 2026-09-12 确认）：推送后不必等结果、不必为 CI 失败立刻回头改，
> 也不要把 CI 状态写成待办；留到产品改良完善后统一处理。
> 详见全局 `AGENTS.md` §13.1。下面这些坑是**以后**再动 CI 时用的。

### 必须记住的 8 个坑

1. **新仓库首次推送不触发工作流**（GitHub 行为）。用 `gh workflow run <name> --ref main`
   手动派发一次，之后就正常了。
2. **clang-format 版本必须与开发机一致**。CI 装的是发行版自带的 18，本机是 19.1.0，
   换行对齐与 CJK 宽度判定不同 → "本地通过、CI 失败"。工作流里已用
   `pip install 'clang-format==19.1.0'` 固定版本。
3. **不要在作业级 `env:` 里设 `DATABASE_URL` / `REDIS_URL` / `JWT_SECRET`**：
   配置加载器**环境变量优先于 .env 文件**，而 `LoadConfigTest` 会写临时 `.env` 并断言文件里的值生效
   → 三个配置用例会失败。CI 里数据库连接由「准备测试配置」步骤写出的 `.env` 提供。
4. **Linux(GCC) 构建是独立的一道质量门**，`-Werror` 下会抓到 MSVC 不报的问题。
   已经被抓到的三类（提交前最好本地跑一次 `docker compose build backend`）：
   - `-Wnull-dereference`：`shared_ptr` 解引用写法（`req->attributes()->get<T>()`）→ 改成取一次局部指针或用请求头传值；
   - `-Wmissing-field-initializers`：`LoggingOptions{"info", ""}` 少一个成员 → 补 `{}`；
   - **MSVC 专有 API**：`_dupenv_s` 在 Linux 不存在 → 用 `GetEnvVar()`（`common/config.h`，内部用 `#if defined(_MSC_VER)` 分支）。
5. **容器内可能解析不了外网域名**（`vcpkg` 报 `Couldn't resolve host name`）。
   构建镜像时用宿主代理：

   ```powershell
   docker compose build --build-arg HTTPS_PROXY=http://host.docker.internal:65532 `
                        --build-arg HTTP_PROXY=http://host.docker.internal:65532 backend
   ```

   后端镜像要在 Linux 容器里从源码编译整套依赖，**首次约 40 分钟**；`vcpkg` 的 clone 在
   runner/本机都可能抖动失败，脚本与工作流里都有重试。

6. **`ctest --test-dir <dir>` 会把测试的工作目录设成那个目录**，而集成测试夹具用相对路径读 `.env`
   → 所有打库的用例 12ms 内失败。已在 `backend/tests/CMakeLists.txt` 里给
   `gtest_discover_tests` 显式设置 `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}`，无论怎么调 ctest 都对。

7. **`write` / `edit` 工具写出来的是无 BOM 的 UTF-8**，而 `scripts/*.ps1` 里有中文：
   PowerShell 5.1 对无 BOM 文件按 ANSI(GBK) 解析 → 中文乱码，甚至报一堆假语法错误。
   改完脚本必须补回 BOM 并复核：

   ```powershell
   $p = 'E:\DeepSeekProjects\lan-file-hub\scripts\start.ps1'
   $t = [System.IO.File]::ReadAllText($p, (New-Object System.Text.UTF8Encoding($false)))
   [System.IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding($true)))
   $b = [System.IO.File]::ReadAllBytes($p)   # 必须是 EF BB BF
   ```

   语法自检（无 BOM 会误报）：

   ```powershell
   $e = $null; [System.Management.Automation.Language.Parser]::ParseFile($p, [ref]$null, [ref]$e); @($e).Count
   ```

8. **Windows 保留端口段会让 8080 根本绑不上**（Docker Desktop / Hyper-V / WSL 启动时预留；
   本机实测预留了 `7920-8019`、`8020-8119`、`8120-8219`、`8320-8419`——**8080/8081/8180 全在里面**）。
   症状极具迷惑性：`bind` 返回 `WSAEACCES`（"access permissions"）、**没有任何进程在监听**，
   而 Drogon 只会打一行 `Bind address failed`。查看保留段：

   ```powershell
   netsh int ipv4 show excludedportrange protocol=tcp
   ```

   处理：把 `.env` 的 `HTTP_PORT` 改成保留段外的端口（**本机用 9080**）。
   `scripts/*.ps1` 与 `frontend/vite.config.ts` 都跟着 `.env` 走（`scripts/_env.ps1` 统一读取），
   不需要改代码；前端代理由 `start.ps1` 注入 `LFH_BACKEND_URL`，所以前端也不用配。
   也可以在管理员 PowerShell 里释放保留段，但那是系统级改动，先确认再动。
   后端已经能区分**「被占用」与「被系统保留」**（`common/net.h` 的 `PortStatus`），
   报错信息里直接给出上面的排查命令。

### 本机一键启动（用户日常用）

- **桌面只有一个按钮**：`lan-file-hub.cmd` 双击进小菜单（1 启动 / 2 停止 / 3 状态 / 4 打开浏览器 / 5 创建管理员 / Q 退出，
  回车即启动）。菜单逻辑在 `scripts/menu.ps1`，实际动作是 `scripts/start.ps1` 与 `scripts/stop.ps1`。
- 也可命令行直用：`.\scripts\menu.ps1 -Action start|stop|status|open|admin`；
  `start.ps1 -Release / -NoBrowser / -NoFrontend`、`stop.ps1 -WithDeps`（连容器一起停）。
- ⚠️ **不要把 `start.ps1` 的输出接进管道**（`.\scripts\start.ps1 | Select-Object ...`）：
  它启动的后台进程会一直持有那个管道句柄，外层会永远等下去（看起来像"卡死"）。
  要看输出就重定向到文件（`*> out.txt`）或直接在控制台里跑。
- `menu.ps1` 在**非交互式**调用（stdin 被重定向）时会直接打印状态后退出，不进 `Read-Host` 菜单——
  否则它会一直等输入，看起来像卡死（自动化测试踩到过）。
- 后端"在不在跑"**只看 `/health/live`，不看进程名**：Drogon 绑定端口失败时只打一行 `FATAL`
  日志、进程还可能继续留在事件循环里（僵尸：进程活着、端口没人听）。只看进程名会误判成
  "已在运行"从而永远不再启动；`start.ps1` 现在按健康探活判断，并清掉占着后端端口却不健康的旧进程。
- 后端端口**只有一处配置**：`.env` 的 `HTTP_PORT`（本机是 `9080`，因为 8080 落在 Windows
  保留段里）。`scripts/_env.ps1` 负责读取，`menu.ps1` / `start.ps1` / `bench.ps1` 共用；
  前端代理地址由 `start.ps1` 用 `LFH_BACKEND_URL` 注入 vite。**不要在脚本里硬编码端口。**
- 后端启动前自检监听端口（`common/net.h` 的 `CheckListenPort` → `PortStatus`）：
  被占用或被系统保留时明确报错并以非零码退出，绝不进入事件循环——不会再出现"启动了但连不上"。
- 脚本里的等待**一律有界且打进度点**：依赖 20 秒 / 后端 10 秒 / 前端 15 秒；失败立刻打印日志末尾
  并中止启动（不再静默重试 50 秒，也不在失败后还打印"启动完成"）。改脚本时不要重新引入长等待。
- **管理员账号必须显式创建**：注册页只创建普通用户。菜单按 5，或
  `.\scripts\create-admin.ps1 -Username admin -Email a@b.c`（口令交互输入、不回显，
  至少 8 位；用户名已存在则**提升为 ADMIN 且不改口令**，已是管理员则幂等跳过）。
  **口令只从环境变量传给工具，不进命令行、不落盘、不进日志**，仓库里不放默认口令。
- **手机 / 其它电脑访问**：前端 vite 必须监听所有网卡（`frontend/vite.config.ts` 里
  `host: true` + `strictPort: true`）——只绑 localhost 时手机连不上（实测踩到过）。
  `start.ps1` 与菜单状态会打印 `📱 手机访问: http://<局域网IP>:5173`。
  手机只需访问 **5173**：`/api` 与 `/health` 由 vite 代理到本机后端，不需要手机直连后端端口
  （本机实测 `node.exe` 与 `lan_file_hub_server.exe` 都有 Public Profile 入站放行规则）。
  校园网常开**客户端隔离**导致设备互访不通，真机测试优先用**手机热点**（电脑连手机热点）。
- 排查启动问题：后端日志 `%TEMP%\lfh-backend.log`，前端日志 `%TEMP%\lfh-frontend.log`。

### 仍未验证（不要写成已完成）

- ThreadSanitizer / UndefinedBehaviorSanitizer：MSVC 不提供，依赖又是 MSVC 静态库，无法混用 clang；
- 前端镜像（nginx 托管 dist）未单独构建验证；
- 真实局域网链路性能、长稳压测、生产 HTTPS 端到端（只有 nginx 示例）。

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

---

## 14. 用户明确指示（2026-09-21，优先级高于本文件其他流程性内容）

> 依据全局 `AGENTS.md` §1.1：**用户亲自指示**的规则优先于全局文件里的流程性规则。
> 本节内容由用户 2026-09-21 真机试用后提出，未经用户同意不得删改、不得降级为"建议"。

### 14.1 判定标准：不会用 = 缺陷

- 目标用户是**不懂技术的人**（用户原话："我作为一个什么都不懂得的人"）。任何功能，
  如果用户**不知道从哪里进入、不知道下一步点哪里、不知道对方该做什么**，那就是**产品缺陷**，
  不是"用户没看文档"。修法是改界面 / 改流程，而不是写一段更长的说明。
- 每一项面向用户的能力，必须在界面上**自己说清楚**（按钮文案、空状态提示、操作后的结果提示），
  并在 `docs/guide.md`（大白话使用说明）里有对应步骤。
- 提交任何面向用户的功能前，自查一句话：**"我妈能不能照着界面点出来？"** 不能就改。

### 14.2 同账号多端：**不需要"传"**

- 同一个账号在电脑和手机上登录，看到的是**同一份文件**（服务端统一存储），
  手机端只要登录 → 进「我的文件」→ 下载即可，**不存在"给自己传文件"这个动作**。
- 界面与文档必须明确写出这一点（这是用户 2026-09-21 亲口问的第一个问题，说明现在完全没说清）。

### 14.3 给"某个人"传文件：必须能选人，不许要 UUID

- 现状缺陷：授权弹窗要求填**用户 ID（UUID）**，而界面上任何地方都查不到 UUID
  → 等于**没法按人分享**。这是必须修的缺陷。
- 要求：分享/授权时必须能**按用户名或邮箱选人**（下拉/搜索选择器），全程不出现 UUID。
- 另一端（接收方）必须有个明确的地方能看到"别人给我的东西"：就是左侧「分享给我的」，
  界面上要写清楚"别人授权给你的文件会出现在这里"。

### 14.4 分享链接必须"手机点开就能用"

- 现状缺陷：分享链接按 `window.location.origin` 生成，用户在电脑上用 `127.0.0.1:5173` 打开时，
  生成的链接是 `http://127.0.0.1:5173/s/<token>` —— **手机上 127.0.0.1 指手机自己，必然打不开**
  （用户 2026-09-21 实测踩到）。
- 要求：链接一律用**局域网可达地址**（`http://<本机局域网IP>:5173/s/<token>`）；
  生成时若发现当前访问地址是 `localhost/127.0.0.1`，**必须给出局域网地址版本**并提示用户。
  最好直接提供二维码，手机扫码即开。

### 14.5 必须覆盖的传输场景（每一种都要能跑通并有说明）

| 场景 | 要求 |
|---|---|
| 同一账号：电脑 → 手机 | 手机登录同账号直接下载（见 14.2，无需"传"） |
| 同一账号：手机 → 电脑 | 手机上传，电脑端「我的文件」里直接看到 |
| 不同账号：我给对方 | 按用户名/邮箱选人授权（14.3）或发局域网分享链接（14.4） |
| 不同账号：对方给我 | 对方同样操作；我在「分享给我的」里看到并下载 |
| 电脑 ↔ 电脑、手机 ↔ 手机 | 同上规则，与设备无关（都是浏览器访问同一服务端） |
| 管理员 vs 普通用户 | 管理员多出「系统管理」（用户管理/指标/审计）；**普通用户不得看到或进入管理员功能**，权限判定一律在后端 |

### 14.6 网络：局域网与手机热点都要能用

- 两种网络都必须能跑通，并且用户**不需要懂 IP**：启动脚本 / 菜单要直接给出"手机该打开哪个地址"。
- 校园网等开启客户端隔离的网络要能识别并明确提示"这个网络下手机连不上电脑，请改用手机热点"。

### 14.7 必须交付的文档

- `docs/guide.md`：**给普通人看的大白话使用说明**（怎么启动、手机怎么连、各场景怎么操作、常见问题），
  README 顶部要有明显入口。
- 技术细节仍放各自的 `docs/*.md`，不要把大白话说明塞进设计文档。
