# 实施状态

> 审计日期：2026-09-12 ｜ 对应阶段：阶段零（项目审计与实施计划）
> 本文件由阶段零产出。每个阶段结束时更新「阶段进度」与「发现的问题」两节。
>
> **最新进度：阶段零～五已完成**（阶段一～五的实施记录见第 13～17 节）。
> 第 1～12 节是阶段零当时的审计快照，其中的"没有任何代码""未初始化 Git"等结论
> 描述的是**阶段零之前**的状态，保留原文以便追溯，不代表现在。

## 1. 结论摘要

- 项目目前**只有规划文档，没有任何代码**：32 个 Markdown 文件、0 行业务代码、0 个构建文件。
- **未初始化 Git**：项目目录与全部祖先目录都没有 `.git`，`git status` 返回
  `fatal: not a git repository`。因此指令加载的「项目根」回退到当前工作目录，恰好就是项目目录。
- 工具链基本齐备，但有 **3 个必须先解决的问题**才能进入阶段一：
  1. `AGENTS.md` 里写的 clang-format 路径指向一个**坏的可执行文件**（本次审计发现并已修正）；
  2. **vcpkg 未安装**，阶段一无法拉齐 Drogon / libpqxx / OpenSSL / GoogleTest 等依赖；
  3. **Docker 守护进程未运行**，compose 与 PostgreSQL/Redis 集成测试都跑不了。
- 阶段划分已存在（`docs/phases/`，阶段零～十七），本次审计确认了阶段依赖链与阶段一的前置条件，
  不需要重新拆分。

## 2. 当前项目状态

| 项 | 值 |
|---|---|
| 项目根 | `E:\DeepSeekProjects\lan-file-hub` |
| 文件总数 | 32（全部为 `.md`），目录 2 个（`docs/`、`docs/phases/`） |
| 源代码 | 无 |
| 构建文件 | 无（`CMakeLists.txt`、`CMakePresets.json`、`package.json`、`Dockerfile`、`docker-compose.yml` 均不存在） |
| 工程配置文件 | 无（`.clang-format`、`.clang-tidy`、`.gitignore`、`.env.example` 均不存在） |
| 测试与 CI | 无（无 `tests/`、无 `.github/`） |
| 版本控制 | 未初始化（无 `.git`） |
| 临时目录 | `E:\DeepSeekProjects\_tmp\lan-file-hub\` **尚未创建**（本次审计未产生临时文件） |
| 隐藏文件 | 无 |

## 3. 已完成内容

**功能层面：0 完成。** 目前完成的全部是文档与规则，来自上一轮的「精简规划」工作，不是阶段零的产出：

| 文件 | 作用 |
|---|---|
| `AGENTS.md` | 项目级规则：技术决策表、红线、命令与工具路径、阶段指针、待确认 |
| `docs/brief.md` | 项目简报：定位（自用起步、工业级、公司可采用）、目标用户、核心循环、范围、里程碑 |
| `docs/architecture.md` | 系统组成、后端分层、并发模型、Redis 边界、可观测性、C++ 实现规范、管理员边界 |
| `docs/data-model.md` | 表结构、字段与状态枚举、索引、事务边界、幂等、配额与统计规则 |
| `docs/api.md` | API 约定、21 个错误码、32 条端点、分页排序、认证方案 |
| `docs/storage.md` | ObjectStorage 抽象、路径安全、分块上传协议、下载设计、去重、对象状态机 |
| `docs/permissions.md` | 四级权限、13 个检查点、权限继承、分享链接规则 |
| `docs/security.md` | 安全要求与必须覆盖的 10 个攻击场景 |
| `docs/recovery.md` | 删除与恢复、故障处置、后台任务、备份与恢复 |
| `docs/frontend.md` | 页面清单、20 项交互、两套状态机、上传算法与重试 |
| `docs/testing.md` | 测试分层清单、Sanitizer 与性能基准、验收标准 |
| `docs/deployment.md` | 配置、容器、数据卷、迁移、备份、CI、发布产物 |
| `docs/plan.md` | 本次精简的来龙去脉、44 节内容去向映射、冲突裁决、环境实测、待确认 |
| `docs/phases/` | 18 个阶段任务书 + 阶段索引与 MVP 冻结边界 |

文档完整性已验证：0 个 BOM、29 个相对链接 0 断链、每个文件单一 H1、关键条目（错误码、状态枚举、
数值约束）留存检查通过。

## 4. 未完成内容

> 以下是**阶段零当时**的快照。截至阶段五结束，阶段一～五的交付物均已落地
> （见第 13～17 节的实施记录），**阶段六～十七未开始**。

阶段一～十七**全部未开始**。具体到交付物：

| 类别 | 状态 |
|---|---|
| 后端 C++ 代码 | 0 行（`backend/` 不存在） |
| 前端代码 | 0 行（`frontend/` 不存在） |
| 数据库迁移 | 0 个（`migrations/` 不存在） |
| 测试 | 0 个 |
| 脚本 | 0 个（`scripts/` 不存在） |
| 容器化 | 0 个（`Dockerfile`、`docker-compose.yml` 不存在） |
| CI | 0 个（`.github/workflows/` 不存在） |
| README / CHANGELOG | 不存在 |

`AGENTS.md` §2 技术决策表里的每一项都还没有落地，包括 Drogon、libpqxx、Redis 客户端、Argon2id、
nlohmann/json、GoogleTest、React 前端与 vcpkg manifest。

## 5. 构建方式

**当前：不存在可用的构建方式**——没有任何构建文件，没有任何可编译的源码。

规划中的构建方式（阶段一落地，落地后必须回来核对）：

```powershell
.\scripts\build.ps1 -Preset debug     # 阶段一创建；等价裸命令见下
cmake --preset debug                  # CMakePresets 至少提供 debug / release / asan / tsan
cmake --build --preset debug
ctest --preset debug
```

工具绝对路径（`cmake` 与 clang 系**不在 PATH**，脚本里集中写，不要散落到文档与 CI）：

```text
CMake 3.31.6    D:\Codex Tools\CMake\cmake-3.31.6-windows-x86_64\bin\cmake.exe
MSVC + Ninja    D:\VS2026（自带 CMake 与 Ninja）
clang-format    D:\Codex Tools\LLVM\bin\clang-format.exe    19.1.0
clang-tidy      D:\Codex Tools\LLVM\bin\clang-tidy.exe      19.1.0
ninja / node / npm / git / docker    已在 PATH
```

依赖管理：vcpkg manifest 模式（`vcpkg.json`），本体计划装 `D:\Codex Tools\vcpkg`，**尚未安装**。

## 6. 测试方式

**当前：不存在任何测试**，也没有可运行的测试命令。

规划中的测试分层（阶段一建立骨架，阶段二起逐步填充）：GoogleTest（C++ 单元与集成测试）、
Vitest + Playwright（前端）、Google Benchmark（性能）、ASan/UBSan/TSan 三套 Sanitizer 构建。
测试清单与验收标准见 `docs/testing.md`。

## 7. 环境审计结果

### 7.1 编译与工具链

| 组件 | 状态 | 版本 | 位置 / 备注 |
|---|---|---|---|
| MSVC | ✅ | `cl` 19.51.36256，工具集 14.51.36231 | `D:\VS2026` |
| Windows SDK | ✅ | 10.0.22621.0、10.0.26100.0 | `C:\Program Files (x86)\Windows Kits\10` |
| CMake | ✅ 不在 PATH | 3.31.6 | `D:\Codex Tools\CMake\cmake-3.31.6-windows-x86_64\bin` |
| Ninja | ✅ 在 PATH | 1.12.1 | `D:\Codex Tools\Ninja` |
| clang-tidy | ✅ 不在 PATH | 19.1.0 | `D:\Codex Tools\LLVM\bin` |
| clang-format | ⚠️ 见问题 1 | 19.1.0 | 可用副本在 `D:\Codex Tools\LLVM\bin` |
| Git | ✅ | 2.55.0.windows.1 | `E:\Git\cmd\git.exe` |
| Node | ✅ | v24.13.0 | `F:\Program Files\nodejs` |
| npm | ✅ | 11.6.2 | 同上；调用时有 verbose 噪声，见问题 6 |
| Docker CLI | ✅ | 29.7.2 | `D:\Codex Tools\Docker\resources\bin` |
| Docker 守护进程 | ❌ 未运行 | — | Docker Desktop 未启动 |
| vcpkg | ❌ 未安装 | — | 阶段一前置项 |
| conan | ❌ 未安装 | — | 已决定不用 |
| WSL | ❌ 无发行版 | — | 若将来需要 Linux 构建环境需另装 |

### 7.2 依赖服务

| 服务 | 状态 | 说明 |
|---|---|---|
| PostgreSQL | ❌ 完全没有 | 无 `psql` 客户端、无 Windows 服务、5432 端口无监听。本机 `D:\Codex Tools` 有 MySQL 与 Redis，**没有 PostgreSQL**，计划由 Docker 提供 |
| Redis | ⚠️ 二进制在，未运行 | `D:\Codex Tools\Redis\redis-server.exe` 存在；6379 端口无监听 |

### 7.3 磁盘

| 盘 | 剩余 / 总量 | 备注 |
|---|---|---|
| C: | **8.1 GB / 100 GB** ⚠️ | 偏紧，注意 vcpkg 缓存、npm 缓存、MSVC 中间产物 |
| D: | 49.2 GB / 114.7 GB | 工具与 DockerData 所在盘 |
| E: | 59.7 GB / 160 GB | 项目与构建目录所在盘 |
| F: | 44.7 GB / 100 GB | Node 安装盘 |
| H: | 124.7 GB / 465.7 GB | 空闲最多 |

Docker 数据目录已配置在 `D:\Codex Tools\DockerData`（存在 `disk`、`main`），没有落在紧张的 C: 上，
这一点是对的。

## 8. 发现的问题

| # | 严重度 | 问题 | 影响 | 处理 |
|---|---|---|---|---|
| 1 | **High** | `AGENTS.md` 原写的 `D:\Codex Tools\clang-format\bin\clang-format.exe`（108,401 字节）**执行即失败**：`--version` 退出码 1、无任何输出；`clang-format18\bin\` 下同名文件同样如此 | 阶段一的格式化检查会直接失败 | **已修正**：改用 `D:\Codex Tools\LLVM\bin\clang-format.exe`（19.1.0，与 clang-tidy 同源），并在 `AGENTS.md` §4 加了警告 |
| 2 | **High** | vcpkg 未安装 | 阶段一无法拉齐 Drogon、libpqxx、OpenSSL、GoogleTest 等依赖，构建无法开始 | 阶段一前置：装到 `D:\Codex Tools\vcpkg` |
| 3 | **Medium** | Docker 守护进程未运行 | `docker compose up` 与 PostgreSQL/Redis 集成测试跑不了；阶段一验收项之一无法执行 | 阶段一前置：启动 Docker Desktop |
| 4 | **Medium** | PostgreSQL 完全没有（无客户端、无服务、端口未监听） | 阶段一起就需要数据库；本机没有现成的 | 走 Docker Compose 提供；不要在 Windows 上单独装一套 |
| 5 | **Medium** | C: 只剩 8.1 GB | vcpkg 默认会把下载缓存与已装包放自己目录（本项目放 D:，可控）；npm 全局缓存默认在 C:，前端依赖体积大 | 前端 `node_modules` 落在 E:（项目内）无碍；必要时把 npm 缓存也移到 D: |
| 6 | Low | 通过 `npm.ps1` 调用 npm 会往 stderr 输出一堆 verbose 日志 | 会污染脚本与 CI 输出，也可能干扰输出解析 | 脚本里用 `npm.cmd` 或加 `--silent` |
| 7 | Low | 无 Git，无法用 `git diff` 判断「是否存在用户已有修改」 | 任务第 5 项无法做强判定 | 按文件系统事实说明（见 §9）；不影响开发 |
| 8 | Low | WSL 无已安装发行版 | 若将来要走 Linux 构建或 CI 本地复现需先装 | 本阶段不动，记录备查 |

## 9. 关于「是否存在用户已有修改」（任务第 5、6 项）

- 项目**没有版本控制**，因此无法用 `git diff` / `git status` 判定，这一项**无法做强判定**——
  如实记录，不假装做过。
- 按文件系统事实判定：32 个文件全部是 2026-09-12 由规划会话生成的 Markdown；目录内
  **没有构建产物、没有 IDE 配置目录、没有隐藏文件、没有临时文件**，`_tmp\lan-file-hub` 尚未创建。
- 结论：**当前没有任何需要保护的既有代码或用户改动**，阶段一新建目录与文件时不存在覆盖风险。
- 阶段一落地 Git 之后，这一项才能按标准流程检查。

## 10. 推荐的实施顺序

### 10.1 阶段一之前（P0，缺一不可）

1. 安装 vcpkg 到 `D:\Codex Tools\vcpkg`（先列目录确认没有现成的——已确认没有）。
2. 启动 Docker Desktop，确认 `docker info` 能返回 Server 版本。
3. ~~修正 clang-format 路径~~ ——本次审计已完成。

### 10.2 阶段依赖链

阶段编号不重排，沿用 `docs/phases/README.md`。关键依赖关系：

```text
阶段零 ──> 阶段一 ──> 阶段二 ──> 阶段三 ──> 阶段四 ──> 阶段五 ──> 阶段六
                                          └──> 阶段七（版本，依赖四）
                                                     └──> 阶段八（回收站，依赖三/四）
                                                            └──> 阶段九（权限分享，依赖三/四）
                                                                   └──> 阶段十（前端，依赖二～九）
阶段十一（后台任务）依赖八；阶段十二（加固）依赖二、九；阶段十三（可观测性）依赖一；
阶段十四（性能与故障）依赖五、十一；阶段十五（部署）依赖一；阶段十六（发布）依赖全部；
阶段十七（终审）最后执行。
```

不能跳过的硬依赖，单独点名：

- **阶段六必须在阶段五之后**：去重与配额的两阶段检查点（初始化、完成）建立在分块上传的完成流程上。
- **阶段八、九必须在阶段三、四之后**：软删除与权限检查都要求资源模型和对象存储已经存在。
- **阶段十四必须在阶段十一之后**：性能与故障测试要覆盖后台任务的重试与恢复路径。
- **阶段十七必须最后**：它是对全部成果的发布验收，不修改代码（除非是阻塞发布的明显缺陷）。

### 10.3 阶段进度

| 阶段 | 名称 | 状态 |
|---|---|---|
| 零 | 项目审计与实施计划 | ✅ 完成（2026-09-12） |
| 一 | 工程基础设施 | ✅ 完成（2026-09-12）；Docker Compose 与 PostgreSQL 就绪检查因 WSL 被禁用曾**阻塞未验证**，后续已恢复并跑通 |
| 二 | 用户认证 | ✅ 完成（2026-09-12） |
| 三 | 文件夹和基础资源模型 | ✅ 完成（2026-09-12） |
| 四 | 本地对象存储和单文件上传下载 | ✅ 完成（2026-09-12） |
| 五 | 可恢复分块上传 | ✅ 完成（2026-09-12），E2E 54/54 断言通过 |
| 六 | 文件去重和配额 | 未开始 |
| 七 | 文件版本和并发修改冲突 | 未开始 |
| 八 | 回收站、恢复和异步清理 | 未开始 |
| 九 | 资源权限和共享链接 | 未开始 |
| 十 | 前端完整文件管理界面 | 未开始 |
| 十一 | 后台任务、过期清理和服务恢复 | 未开始 |
| 十二 | 限流、下载控制和安全加固 | 未开始 |
| 十三 | 可观测性和管理员功能 | 未开始 |
| 十四 | 性能、并发和故障测试 | 未开始 |
| 十五 | Docker、部署和数据备份 | 未开始 |
| 十六 | GitHub 发布准备 | 未开始 |
| 十七 | 最终审查和验收 | 未开始 |

## 11. 本次审计执行的命令

全部为只读检查；除本文件与 `AGENTS.md` 的 clang-format 路径修正外，没有修改任何东西。

```text
git -C <项目根> status                          # fatal: not a git repository
Get-ChildItem <项目根> -Recurse -File / -Force   # 目录结构与隐藏项
Test-Path <各类构建与工程文件>                   # CMakeLists / package.json / Dockerfile / ...
Test-Path <祖先目录>\.git                       # 判定项目根
cmd /c vcvars64.bat && cl                       # MSVC 版本
Get-ChildItem D:\VS2026\VC\Tools\MSVC           # 工具集版本
Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include"   # Windows SDK
cmake --version / ninja --version / node --version / npm --version / git --version
docker --version / docker info                  # CLI 与守护进程
clang-format --version / clang-tidy --version   # 含坏副本诊断
Get-Command vcpkg / conan / psql                # 依赖管理与数据库客户端
Get-Service *postgres* / Get-NetTCPConnection -LocalPort 5432,6379
Get-ChildItem D:\Codex Tools\Redis / DockerData
Get-CimInstance Win32_LogicalDisk               # 磁盘空间
wsl -l -q                                       # WSL 发行版
```

## 12. 本次审计未做的事

- 未实现任何业务功能，未创建任何源码、构建或测试文件。
- 未初始化 Git，未做任何提交或远程操作（用户已明确：全程本地开发，末尾再上传）。
- 未安装或升级任何工具（vcpkg 等留到阶段一前置）。
- 未运行构建或测试——没有可构建、可测试的东西，因此**阶段一之前不存在任何「验证通过」的结论**。
- 未删除、覆盖或格式化任何已有文件。

---

## 13. 阶段一实施记录（2026-09-12）

### 13.1 交付物

| 文件 / 目录 | 说明 |
|---|---|
| `CMakeLists.txt` | 根构建脚本：C++20、统一警告（含 `Werror` 开关）、Sanitizer 开关、依赖查找 |
| `CMakePresets.json` | `debug` / `release` / `asan` 三套预设；vcpkg toolchain、安装目录、overlay、缓存路径都在这里 |
| `vcpkg.json` | 依赖清单：drogon、libpqxx、hiredis、spdlog、fmt、nlohmann-json、openssl、gtest |
| `vcpkg-overlays/libpqxx/` | 见 13.4 问题 3 |
| `.clang-format` | Google 基线，列宽 100，中文注释不重排 |
| `.clang-tidy` | Google 命名基线 + 项目检查集 |
| `backend/CMakeLists.txt` | `lan_file_hub_core` 静态库 + `lan_file_hub_server`；整档链接（Drogon 控制器自注册需要） |
| `backend/include/lan_file_hub/common/{result,config,logging,request_id,uuid}.h` | 公共头 |
| `backend/include/lan_file_hub/infrastructure/health_check.h` | 健康检查接口 |
| `backend/src/common/*.cpp` | 配置读取、日志、request_id、UUID |
| `backend/src/infrastructure/health_check.cpp` | PostgreSQL / Redis / 对象存储探测，失败原因脱敏 |
| `backend/src/api/{health_controller,request_id_middleware}.{h,cpp}` | `/health/live`、`/health/ready` 与 request_id 中间件 |
| `backend/src/main.cpp` | 启动流程：配置 → 日志 → 存储目录 → 健康检查器 → HTTP 服务 |
| `backend/tests/unit/*.cpp` + `fixtures/temp_dir.h` | 27 个单元测试 |
| `Dockerfile` / `docker-compose.yml` / `frontend/Dockerfile` | 容器化（**未构建验证**，见 13.5） |
| `.env.example` | 环境变量示例与取值区间说明 |
| `.gitignore` | 构建产物、凭据、运行时数据 |
| `scripts/{_vsenv,build,test,format,lint}.ps1` | 构建/测试/格式/静态检查，工具绝对路径集中在 `_vsenv.ps1` |
| `.github/workflows/{backend,docker}.yml` | CI：格式检查 + 编译 + 测试；compose 文件校验 |
| `README.md` | 快速开始、命令、配置、已知限制 |

**未实现任何业务功能**（阶段一约束）：没有用户、文件夹、文件、上传等接口。

### 13.2 实测结果

```text
.\scripts\build.ps1                    -> 退出码 0，无警告无错误（LAN_FILE_HUB_WERROR=ON）
.\scripts\test.ps1                     -> 100% tests passed, 0 failed out of 27（3.5 秒）
.\scripts\format.ps1                   -> clang-format 退出码 0
clang-tidy（21 个文件）                 -> 见 13.5
```

健康检查（服务实际运行 + curl 实测）：

| 请求 | 结果 |
|---|---|
| `GET /health/live` | **200 OK**，`{"check":"live","status":"ok","service":"lan-file-hub","request_id":"..."}` |
| 响应头 | 含 `x-request-id`（新生成 UUID） |
| `GET /health/ready` | **503 Service Unavailable**（PostgreSQL 不可用，符合预期） |
| └ `postgresql` | `unavailable`，`port 5432 failed: Connection refused`，2055 ms |
| └ `redis` | `ok`，1 ms（用本机 `D:\Codex Tools\Redis\redis-server.exe`） |
| └ `object_storage` | `ok`，0 ms，并自动创建 `storage/` |
| `X-Request-Id: my-trace-0001` | 原样透传，响应头与响应体一致 |
| `X-Request-Id: bad id with spaces` | 被判定非法，替换为新的 UUID v4 |

结论：**就绪检查在依赖不可用时确实返回 503，没有谎报就绪**；凭据不出现在输出中（有专门测试覆盖）。

### 13.3 环境实测补充（相对阶段零）

| 组件 | 状态 | 版本 |
|---|---|---|
| vcpkg | ✅ 已安装 | 2026-07-27，位于 `D:\Codex Tools\vcpkg` |
| 依赖 | ✅ 全部装好 | drogon 1.9.13、openssl 3.6.4、libpqxx 8.0.2、spdlog 1.17.0、gtest 1.18.0、hiredis 1.4.1、nlohmann-json 3.12.0、fmt 12.2.0、libpq 18.4、trantor 1.5.28 |
| Redis | ✅ 可本机运行 | `D:\Codex Tools\Redis\redis-server.exe`，6379 已实测监听 |
| Docker | ✅ 可用（2026-09-12 补验） | ServerVersion 29.7.2。用户启用 `WSLService` 后，重启 Docker Desktop 恢复 |
| PostgreSQL | ✅ 可用（2026-09-12 补验） | 容器 `lan-file-hub-postgres`（postgres:16-alpine），5432 已映射，`pg_isready` 通过 |

### 13.4 本阶段解决的技术问题（都会复发，记在这里）

1. **`.ps1` 脚本必须带 UTF-8 BOM**。PowerShell 5.1 按 ANSI 解析脚本，UTF-8 无 BOM 时中文注释会让
   `param()` 块解析错乱（实测默认值丢失、语法报错）。这与全局规则里「DSH 配置文件必须无 BOM」
   不冲突——那条针对 JSON/YAML/Markdown，`.ps1` 恰恰相反。**改脚本后必须补回 BOM**。
2. **CMake 的 vcpkg 集成默认把依赖装到 `${CMAKE_BINARY_DIR}/vcpkg_installed`**，与项目根那份不是
   同一个目录，会重复安装整套依赖。已在 `CMakePresets.json` 里固定
   `VCPKG_INSTALLED_DIR=${sourceDir}/vcpkg_installed`。
3. **libpqxx 8.0.2 的 `pqxx.dll` 导出了 245 个 `std::basic_string_view` 成员函数**
   （`dumpbin /exports` 可复现），导致消费方链接期必然 `LNK2005` / `LNK1169`。
   试过 `/Zc:dllexportInlines-`（无效，挡不住基类内联成员被导出），最终用
   `vcpkg-overlays/libpqxx` 把该包改为**静态库**解决——没有 DLL 就没有导出表。
   该 overlay 只影响这一个包，其余依赖仍是动态库。
4. **vcpkg 不接受通过 `OPTIONS` 传 `CMAKE_CXX_FLAGS`**：会以
   `z_vcpkg_warn_ambiguous_system_variables` 警告并覆盖，实测开关不生效。
5. **`vcpkg install --clean-after-build` 会连 downloads 目录一起清理**，导致下次构建重新下载。
   本机到 GitHub 的链路不稳，建议不要再加这个开关。
6. **vcpkg 清单里 `description` 数组不能有空字符串**，空行要写 `"  ."`，否则报
   `unexpected end of line`。
7. **clang-tidy 19.1.0 与 VS2026 的 STL 不兼容**：STL 要求 Clang 20+，不加
   `-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` 会直接 `STL1000` 报错、任何 STL 头都解析不了。
   已写进 `scripts/lint.ps1`。长期方案是升级 LLVM 到 20+。
8. **网络**：GitHub 直连不稳定（`curl error 56`），vcpkg 会自动读取系统代理。
   大文件下载可用 `curl -C -` 断点续传手动补进 `vcpkg\downloads`。

### 13.5 未验证 / 阻塞事项

| 项 | 状态 | 原因 |
|---|---|---|
| `docker compose config` 校验 | ✅ **已验证** | 退出码 0，compose 文件合法 |
| `docker compose up -d postgres redis` | ✅ **已验证**（2026-09-12 补验） | postgres:16-alpine 拉取成功；两容器均 `Up (healthy)`，端口 5432 / 6379 已映射 |
| `/health/ready` 全组件 `ok` | ✅ **已验证** | 服务连真实 PostgreSQL：**200 OK**，postgresql 41ms / redis 3ms / object_storage 0ms |
| `docker compose up --build`（backend / frontend 镜像） | **未验证** | 后端镜像需要在 Linux 上重编整套 C++ 依赖，留给阶段十五 |
| `Dockerfile` 构建 | **未验证** | 同上 |
| `.github/workflows/*` | **未运行** | 尚未创建 GitHub 远程仓库，CI 从未执行过 |
| clang-tidy 全量扫描 | ✅ 已运行（退出码 0） | 首轮 60+ 条告警，已修掉命名与格式类；剩余 24 条见 13.6，属**非阻塞的风格项**，留到阶段十四统一清理 |
| `release` 预设与 `asan` 预设 | 未运行 | 阶段一验收只要求 debug 通过；Release/ASan 待阶段十四统一验证 |
| `frontend/` | 占位 | 真正的 React 应用在阶段十 |

### 13.6 clang-tidy 剩余告警（非阻塞，留到阶段十四）

首轮 60+ 条，命名与格式类已修完（方法统一为 Google 的 PascalCase，
`Result::Error()` 因遮蔽类型名改为 `GetError()`）。剩余 24 条记录在案：

| 条数 | 检查项 | 说明 |
|---|---|---|
| 9 | `clang-diagnostic-pragma-once-outside-header` | **误报**：直接把头文件当主文件分析才会出现。可在 lint 脚本里加 `-Wno-pragma-once-outside-header` 消掉 |
| 4 | `cppcoreguidelines-pro-bounds-constant-array-index` | `uuid.cpp` 里的 `bytes[i]`、`kHex[...]`，下标为运行期变量 |
| 4 | `cppcoreguidelines-rvalue-reference-param-not-moved` | Drogon 的 `FilterCallback&&` / `AdviceChainCallback&&` 签名固定，不能改 |
| 3 | `cppcoreguidelines-pro-bounds-array-to-pointer-decay` | `spdlog::logger(name, sinks.begin(), sinks.end())` 等 |
| 1 | `cppcoreguidelines-no-malloc` + `owning-memory` | `config.cpp` 里 `_dupenv_s` 配套的 `std::free`，MSVC 要求成对使用 |
| 1 | `bugprone-exception-escape` | 待确认具体位置 |
| 1 | `readability-use-anyofallof` | `request_id.cpp` 的字符校验循环改用 `std::ranges::all_of` |
| 1 | `bugprone-implicit-widening-of-multiplication-result` | `logging.cpp` 的 `10U * 1024U * 1024U` 常量算法 |
| 1 | `readability-identifier-naming` | `config.cpp` 里的局部 `kLevels`（常量前缀规则对局部变量过严） |

这些都不影响构建、测试与运行，`scripts/lint.ps1` 当前退出码为 0。

---

## 14. 阶段二实施记录（2026-09-12）

### 14.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0001_create_users.sql` | users 表 + 大小写不敏感唯一索引 + 状态/角色/配额 CHECK 约束 |
| `backend/src/tools/migrate_main.cpp` | 独立迁移工具 `lan_file_hub_migrate`，把已应用版本记在 `schema_migrations` |
| `backend/include/lan_file_hub/domain/user.h` + `src/domain/user.cpp` | User 领域模型与枚举转换 |
| `.../infrastructure/database.{h,cpp}` | libpqxx 连接池，RAII 租约，断线自动重连 |
| `.../infrastructure/password_hasher.{h,cpp}` | Argon2id（PHC 编码），长度上下限校验 |
| `.../infrastructure/jwt_service.{h,cpp}` | HS256 签发与校验，区分 `TOKEN_EXPIRED` 与 `UNAUTHENTICATED` |
| `.../infrastructure/user_repository.{h,cpp}` | 参数化 SQL；唯一冲突映射为 `RESOURCE_CONFLICT` |
| `.../application/auth_service.{h,cpp}` | 注册/登录/当前用户/登出；恒定时间登录；先验口令再查状态 |
| `.../application/app_context.{h,cpp}` | 进程内服务装配与凭据脱敏入口 |
| `.../api/error_response.{h,cpp}` | 错误码 → HTTP 状态码 → 统一 JSON 信封 |
| `backend/src/api/auth_controller.{h,cpp}` | 4 个认证端点 |
| `backend/src/api/auth_filter.{h,cpp}` | Bearer Token 校验 + 用户状态复查 |
| `frontend/`（整套 React 工程） | Vite + TS + Ant Design + TanStack Query + Axios；登录页、注册页、受保护路由、登录态恢复 |
| `frontend/src/services/tokenStore.ts` | Token 只进 sessionStorage，**明确不写 localStorage** |

### 14.2 实测结果

```text
后端构建          -> 退出码 0，无警告无错误（/WX 开启）
后端测试          -> 100% tests passed, 0 failed out of 57（单元 + 打真实 PostgreSQL 的集成）
迁移              -> 首次应用 1 个；重复执行跳过（幂等）
前端 tsc --noEmit -> 无类型错误
前端 vitest       -> 5/5 通过
前端 vite build   -> 成功，dist 产物 794 KB（gzip 257 KB）
```

端到端实测（服务运行 + curl）：

| 场景 | 结果 |
|---|---|
| 注册 | 200，返回 Token 与用户信息，**响应不含 password_hash** |
| 重复注册（用户名或邮箱，大小写不敏感） | 409 `RESOURCE_CONFLICT` |
| 弱密码 | 400 `INVALID_ARGUMENT` |
| 登录（用户名 / 邮箱） | 200 |
| 错误口令 | 401 `UNAUTHENTICATED`「用户名或密码错误」 |
| **不存在的用户** | 401，**与错误口令的响应完全一致**（防账号枚举） |
| 无 / 非法 `Authorization` 头 | 401 |
| 篡改 Token | 401 `UNAUTHENTICATED` |
| 有效 Token 访问 `/me` | 200 |
| 登出 | 200 |
| **禁用后仍持有未过期 Token** | **403 `FORBIDDEN`**（状态复查生效） |
| 被禁用用户用正确口令登录 | 403 `FORBIDDEN` |
| 服务日志泄露检查 | 不含口令、不含 `access_token`、不含 `eyJ` |

### 14.3 本阶段解决的技术问题

1. **jwt-cpp 0.7 必须显式选择 JSON traits**：只 `#include <jwt-cpp/jwt.h>` 时
   `jwt::create` / `jwt::claim` / `jwt::decode` 都不存在，需再加
   `<jwt-cpp/traits/nlohmann-json/defaults.h>`，并用具名类型
   `jwt::decoded_jwt<jwt::traits::nlohmann_json>`。
2. **jwt-cpp 的 `decoded_jwt` 构造即解码**，在 try 之外构造会因非法 Token 直接抛出，
   必须用 `std::optional` 承接。它的 `verify(jwt, ec)` 重载可以精确区分过期与无效。
3. **libpqxx 8 的 `result[i]` 返回 `row_ref` 而不是 `row`**；用模板兼容两者后，
   又必须写 `row["x"].template as<T>()`，否则触发 C4864（依赖名缺 `template`）。
4. **Drogon 的 `HttpFilter<T>` 派生类不能手动 `registerFilter`**，
   否则触发静态断言；控制器里按类名（本例为全局命名空间的 `AuthFilter`）引用即可。
5. **vcpkg 的 argon2 port 不提供 CMake config**，只能 `find_path` + `find_library`。
6. **vitest 自带一份 vite**，把 `test` 字段写进 `vite.config.ts` 会让 plugins 类型冲突；
   拆成 `vitest.config.ts` 即可。

### 14.4 未验证 / 遗留事项

| 项 | 状态 | 说明 |
|---|---|---|
| 前端容器镜像构建 | **未验证** | 需要 Docker 构建 Node 层；留到阶段十五 |
| 前端页面自动化测试（Playwright） | 未做 | 阶段十 |
| 注册/登录接口限流 | 未实现 | 阶段十二（任务书明确） |
| Token 黑名单 / 主动失效 | 未实现 | 需要 Redis，阶段十二 |
| ADMIN 角色的接口 | 未做 | 角色字段与令牌声明已就绪，管理员接口在阶段十三 |
| HttpOnly Cookie 认证 | 未做 | 当前为 Bearer Token；生产前必须改造（已写入 README 与 api.md） |
| `frontend/package-lock.json` | 已生成 | 应提交进仓库以固定依赖版本 |

---

## 15. 阶段三实施记录（2026-09-12）

### 15.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0002_create_folders.sql` | folders 表 + 每用户唯一根目录 + 同级不重名（不区分大小写）+ 物化路径约束，并为存量用户补建根目录 |
| `.../domain/folder.{h,cpp}` | Folder 模型、状态枚举、名称校验 |
| `.../common/pagination.{h,cpp}` | 分页与排序白名单（sort_by 只映射到固定 SQL 片段） |
| `.../infrastructure/folder_repository.{h,cpp}` | 增删改查、`EnsureRoot`、子树移动、子树软删除、祖先链 |
| `.../application/permission_checker.{h,cpp}` | 权限检查接口 + 阶段三的「所有者/管理员」实现（阶段九替换） |
| `.../application/folder_service.{h,cpp}` | 名称校验、同名冲突、根目录保护、防环移动、软删除 |
| `backend/src/api/folder_controller.{h,cpp}` | 7 个文件夹端点 |
| `frontend/src/`（文件夹相关） | 面包屑、目录列表、新建/重命名/删除（含二次确认） |

### 15.2 实测结果

```text
后端构建 -> 退出码 0，无警告无错误
后端测试 -> 100% tests passed, 0 failed out of 86（阶段二为 57，新增 29）
前端     -> tsc 无错误；vitest 5/5；vite build 成功
迁移     -> 0002 应用成功；重复执行跳过
```

端到端实测（真实 PostgreSQL + curl）：

| 场景 | 结果 |
|---|---|
| `GET /folders/root` | 200，`is_root=true`、`parent_id=null`、`name=""` |
| 新建 / 重命名 / 移动 | 200 |
| 同级重名（含大小写不同） | 409 |
| 名称 `..` / 含 `/` | 400 |
| 移动到自己的子目录 / 自身 | 400 |
| 重命名或删除根目录 | 403 |
| 删除后列表不再出现，详情返回 404 | ✅ 软删除生效且数据仍在库里（`TRASHED` + `deleted_at`） |
| 他人资源读/改/删 | 全部 404（不泄露存在性） |
| `GET /{id}/path` 面包屑 | `<root> > lv1 > lv2`（3 层） |
| 排序 desc / 非法 sort_by / 分页 | 排序生效；非法字段 400；`page_size=1` 时 `total=2` |

### 15.3 本阶段解决的技术问题

1. **`BuildOrderBy` 的排序片段带 `f.` 前缀，但 SQL 没给表起别名** → `GET children`
   报 503。修法是 `FROM folders f`，并在注释里写明这两处必须一致。
2. **数组不能直接转 `std::initializer_list`**：`std::begin/end(kSortWhitelist)` 编译失败，
   改成在调用点直接写列表字面量。
3. **`[[nodiscard]]` 函数放进 `EXPECT_THROW` 会触发 C4834**（警告即错误），
   改成 `EXPECT_THROW({ (void)fn(); }, ...)`。

### 15.4 设计说明（后续阶段会用到）

- **物化路径 `path_key`**：格式 `/<祖先id>/.../<自身id>/`。它让三件事各只需一条 SQL：
  判断目标是否在自己的子树内（前缀比较）、移动时重写整棵子树、取祖先链做面包屑。
  移动时有 `SELECT ... FOR UPDATE` 保护，避免并发移动把路径撕成两半。
- **权限接口先于权限表**：阶段三只有「所有者 + 管理员」，
  但 `ResourcePermissionChecker` 接口已定，阶段九替换实现即可，调用方不用改。
- **他人资源返回 404 而不是 403**：403 会告诉攻击者「这个 id 存在」。

### 15.5 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 前端分页控件 | 列表只取第一页，超出一页时给出提示；阶段十补 |
| 前端拖拽移动 | 后端 `PATCH parent_id` 已可用，UI 在阶段十 |
| `audit_logs` 表 | 阶段三的删除/移动只写结构化日志，审计表在阶段八引入 |
| 文件夹权限继承 | 阶段九（当前只有所有者/管理员两级） |

---

## 16. 阶段四实施记录（2026-09-12）

### 16.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0003_create_files.sql` | files + file_versions 表、同级不重名（不区分大小写）、内容寻址与哈希格式约束、每文件唯一当前版本 |
| `.../common/sha256.{h,cpp}` | 流式 SHA-256（OpenSSL EVP），可反复 Update |
| `.../common/http_util.{h,cpp}` | Range 解析（三种写法 + 416 判定）与 RFC 6266 的 Content-Disposition |
| `.../domain/entry_name.{h,cpp}` | 文件夹与文件共用的名称校验（本轮从 folder.cpp 抽出） |
| `.../domain/file.{h,cpp}` | File / FileVersion 模型、MIME 清洗 |
| `.../storage/object_storage.{h,cpp}` | ObjectStorage 与 ObjectWriter 抽象、对象 Key 校验与生成 |
| `.../storage/local_object_storage.{h,cpp}` | 本地实现：`objects/<前两位>/<sha256>` + `tmp/<uuid>`，路径包含性检查 |
| `.../infrastructure/file_repository.{h,cpp}` | 文件与首版本同事务落库、按目录分页列表、版本查询 |
| `.../application/file_service.{h,cpp}` | 上传会话（流式写入 + 边算哈希）、下载准备、列表、权限检查 |
| `backend/src/api/file_controller.{h,cpp}` | 上传 / 列表 / 元数据 / 下载（Range）四个端点 |
| 前端 | 文件列表（与目录合并展示）、多文件上传与进度、下载、中文文件名 |

### 16.2 实测结果

```text
后端构建 -> 退出码 0，无警告无错误
后端测试 -> 100% tests passed, 0 failed out of 128（阶段三为 86，新增 42）
前端     -> tsc 无错误；vitest 5/5；vite build 成功
迁移     -> 0003 应用成功；重复执行跳过
```

端到端实测（真实 PostgreSQL + curl）：

| 场景 | 结果 |
|---|---|
| 小文件上传（≤1 MiB，走缓冲路径） | 200，**服务端 SHA-256 与本地一致** |
| **30 MB 大文件上传（走流式路径）** | **0.4 秒**完成，哈希一致 |
| 完整下载（两种大小） | 内容哈希与上传前一致 |
| `Range: bytes=0-4` | **206** + `content-range: bytes 0-4/38` + 正文 `hello` |
| `Range: bytes=99999-` | **416** + `content-range: bytes */38` |
| 中文文件名下载 | `filename="______.txt"; filename*=UTF-8''%E4%B8%AD%E6%96%87.txt` |
| 同名重复上传 | 409 |
| 非法文件名（`../../etc/passwd`） | 400 |
| 落盘布局 | `objects/2d/2df786…`、`objects/d8/d8bc67…`（内容寻址） |
| 临时目录残留 | **0** |

### 16.3 本阶段解决的技术问题

1. **Drogon 的请求流默认关闭**：不调用 `drogon::app().enableRequestStream(true)`，
   `createRequestStream()` 一律返回空，上传表现为 500。
2. **流式只对「超过内存上限」的请求体生效**：小请求体已被 Drogon 缓冲，
   `createRequestStream()` 同样返回空。因此上传接口必须**同时处理两条路径**，
   并把 `setClientMaxMemoryBodySize` 设为 1 MiB——这样大文件必然走流式，
   整个文件永远不会进内存。
3. **Windows 上 `std::filesystem::path::native()` 是 `wstring`**，
   不能用窄字符串做 `rfind`；路径比较统一改用 `generic_string()`。
4. **大文件下载必须用 `newFileResponse(path, offset, length, setContentRange=true)`**，
   它自带 `Content-Range`，起点越界时返回 416。

### 16.4 设计说明

- **对象 Key 与原始文件名彻底解耦**：内容寻址 `objects/<前两位>/<sha256>`。
  测试专门断言 `storage_key` 里不含原始文件名。
- **提交顺序**：先落对象、再写元数据。元数据失败时对象保留（可能是重复内容），
  由后续阶段的清理任务按引用关系处理——不会出现「数据库指向不存在的对象」。
- **请求体上限**：`setClientMaxBodySize(MAX_FILE_SIZE + 1 MiB)`；
  在会话里还会按 `MAX_FILE_SIZE` 逐块累计检查，超限立即中断并清理临时对象。

### 16.5 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 前端大文件下载 | blob 方式会把文件读进浏览器内存；与 HttpOnly Cookie 一起改造 |
| 上传限流与并发上限 | 阶段十二（`MAX_CONCURRENT_UPLOADS` 已配置但尚未生效） |
| 用户配额检查 | 阶段六 |
| 文件重命名 / 删除 | 阶段八（`PATCH` / `DELETE` 端点） |
| 版本与历史版本 | 阶段七 |
| 分块上传 | 阶段五 |

---

## 17. 阶段五实施记录（2026-09-12）

### 17.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0004_create_uploads.sql` | `upload_sessions` + `upload_chunks`；状态枚举 CHECK、分块大小 CHECK（1 MiB~64 MiB）、期望哈希格式 CHECK、`expires_at NOT NULL`、分块唯一键 `(upload_session_id, chunk_index)` |
| `.../domain/upload_session.{h,cpp}` | `UploadStatus` 八态与往返解析、`ComputeTotalChunks`、`ValidateChunkSize`、`UploadSession` / `UploadChunk` |
| `.../infrastructure/upload_session_repository.{h,cpp}` | 会话与分块的持久化：`ClaimForMerge`（compare-and-set 抢占）、`UpsertChunk`（幂等/冲突三态）、`FindChunk`、`AttachFileAndComplete`（完成与清分块同事务）、`CountInProgressByUser/Global` |
| `.../application/upload_service.{h,cpp}` | `CreateSession` / `GetStatus` / `BeginChunk` / `Complete` / `Cancel`；`ChunkUploadSession` 负责单个分块的流式写入、分块哈希校验与落库 |
| `backend/src/api/upload_controller.{h,cpp}` | 5 个上传端点，含缓冲/流式双路径与 `X-Chunk-Sha256` |
| `.../storage/object_storage.{h,cpp}`、`local_object_storage.{h,cpp}` | 分块写入器、`MergeChunksToTemp`（1 MiB 缓冲流式合并）、`DeleteChunks`、`ChunkExists`、`MakeChunkKey` |
| `.../common/sha256.{h,cpp}` | 新增 `Sha256OfFile`（流式算文件级哈希 + 大小） |
| 前端 | `services/sha256.ts`（增量 SHA-256，10 GiB 文件也只占一个分块内存）、`features/uploads/chunkPlan.ts`（分块规划/续传筛选/并发/退避）、`features/uploads/uploadManager.ts`（编排 + 自动重试 + 暂停取消 + 恢复）、`services/uploadTaskStore.ts`（IndexedDB 持久化，失败降级内存）、`components/UploadPanel.tsx`（拖拽、总/单文件进度、暂停/继续/取消/重试/重新选文件续传） |
| 测试 | `unit/upload_session_test.cpp`（12）、`integration/upload_flow_test.cpp`（15）、`frontend/tests/{sha256,chunkPlan,uploadManager}.test.ts`（39） |

### 17.2 实测结果

```text
后端构建 -> 退出码 0，无警告无错误
后端测试 -> 100% tests passed, 0 failed out of 155（阶段四为 128，新增 27）
前端     -> tsc 无错误；vitest 44/44（阶段四为 5，新增 39）；vite build 成功
迁移     -> 0004 应用成功；重复执行跳过
E2E      -> scripts 54/54 断言通过
```

端到端实测（真实 PostgreSQL + 真实分块上传脚本 `_tmp/lan-file-hub/e2e-phase5.ps1`）：

| 场景 | 结果 |
|---|---|
| 20 MB / 3 分块上传 | 200，服务端重算 SHA-256 与本地**完全一致**，对象落 `objects/66/669807e5…` |
| 断点续传 | 传完 0/1 后查状态 → 补传缺失的块 2 → 完成 |
| 重复分块（同内容同哈希） | 200 且 `already_present: true`，不重复写盘 |
| 重复分块（内容不同） | **409 CHUNK_CONFLICT**，已存分块未被破坏 |
| 分块索引越界 / 分块哈希不符 | 422 `CHUNK_INVALID` |
| 分块不齐时 complete | 422 `UPLOAD_INCOMPLETE`，会话仍可续传 |
| 重复 complete | 幂等，两次 `file_id` 相同 |
| **并发 complete（两个进程同时）** | **200 + 409**，同名文件只有一条记录 |
| 哈希不匹配 | 422 `HASH_MISMATCH`，会话 `FAILED`，**磁盘与数据库分块都清空** |
| 取消上传 | 200，重复取消仍 200，取消后分块上传返回 409 `UPLOAD_ABORTED` |
| 他人会话 | 查询/传分块/complete 一律 404 `UPLOAD_NOT_FOUND` |
| 并发上限 | 第 5 个会话 503 `STORAGE_BUSY`，取消一个后立刻可再建 |
| 分块大小越界（4 KiB / 128 MiB） | 400 `INVALID_ARGUMENT` |
| `storage/tmp` 残留 | **0** |

### 17.3 本阶段解决的技术问题

1. **`expires_at` 为空导致创建会话 503**：会话对象从未设置过期时间，SQL 却按
   `$11::timestamptz` 绑定，PostgreSQL 报 `invalid input syntax for type timestamp with time zone: ""`，
   后续所有带 `upload_id` 的查询再报一次空 uuid 错误，表现为一片 503。
   改为由数据库 `now() + make_interval(secs => $N)` 计算，C++ 侧不再格式化时间。
2. **失败路径不删磁盘分块**：`HASH_MISMATCH` 等分支只删了数据库记录，
   `storage/tmp/uploads/<id>/` 会一直占着空间。统一收尾到 `fail_upload()` lambda。
3. **重复分块无法判定内容冲突**：原先只看索引是否存在。新增 `FindChunk`，
   带 `X-Chunk-Sha256` 时立刻比对；若「记录在但文件丢失」则按未上传重收，可自愈。
4. **单用户并发上限硬编码**：改为配置项 `MAX_CONCURRENT_UPLOADS_PER_USER`（默认 4）。
5. **PowerShell 5.1 的坑（验收脚本层面，会复发）**：
   - `curl` 是 `Invoke-WebRequest` 的别名，**别名优先于自定义函数**，自定义函数不能叫 `Curl`；
   - 内嵌双引号的 JSON 传给原生 exe 会丢引号，请求体一律写文件后用 `-d "@file"`；
   - `JsonOf (命令).属性` 会把 `(命令).属性` 整体当参数传进去；
   - `(单元素管道结果).Count` 在 PS 5.1 返回空，需要 `@(...)` 包一层；
   - 含中文的 `.ps1` **必须带 UTF-8 BOM**，否则按 ANSI 解析直接语法错误（`edit` 工具会去掉 BOM）。

### 17.4 设计说明

- **PostgreSQL 是断点续传的唯一事实来源**，Redis 完全不参与；接口集成测试里专门用
  「全新仓储 + 全新服务实例」验证这一点。
- **合并顺序不可颠倒**：分块落盘 → 合并到 `tmp/<uuid>` → 服务端重算大小与 SHA-256 →
  校验客户端声明 → `Commit` 到内容寻址的最终 key → 事务里创建版本与文件记录。
- **并发完成靠一条 compare-and-set**（`ClaimForMerge`）抢占，抢不到的请求得到可解释的
  409，已经完成的则幂等返回同一文件；合并过程不持有数据库锁。
- **客户端整文件 SHA-256 用自实现的增量 SHA-256**：`crypto.subtle.digest` 是一次性 API，
  10 GiB 文件不能整块读进内存；分块级哈希仍然整块读，因为分块有 64 MiB 上限。
- **刷新后恢复的边界写清楚了**：任务元数据（含 `upload_id`）存 IndexedDB，
  但文件内容不存，所以恢复后需要用户重新选择同一个文件，之后只补缺失分块。

### 17.5 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 浏览器内的真实分块上传 | 前端逻辑用 Vitest + 假后端验证；Playwright 端到端在阶段十 |
| 刷新恢复的浏览器实测 | 同上，IndexedDB 在 jsdom 下不可用，已做降级为内存实现并记录 |
| 分块上传限流 | 阶段十二（当前只有并发会话数上限，没有速率限制） |
| 上传中磁盘满（`STORAGE_FULL`） | 分块写入会检查写入结果并报错，但**未做写入前的可用空间预检**，阶段十二补齐 |
| 卡在 `MERGING` 的会话 | 重启后不会被自动放回可重试状态，需要阶段十一的后台任务 |
| tmp 目录遗留对象 | 进程在合并中途被杀会留下无引用的 `tmp/<uuid>`，阶段十一的清理任务回收 |

---

## 18. 阶段六实施记录（2026-09-12）

### 18.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0005_dedup_and_quota.sql` | 去重索引 `files(owner_id, content_hash)` 与 `file_versions(content_hash)`、配额非负约束、按版本汇总回填 `used_storage_bytes` |
| `.../infrastructure/file_repository.{h,cpp}` | `CreateWithFirstVersion` 增加**事务内配额强制**（`SELECT ... FOR UPDATE` 锁用户行 → 校验 → 落库 → 累加统计）；新增 `FindOwnedObjectByHash`（去重预检查）、`CountObjectReferences`（引用计数）、`DeleteByIdForTesting`；`SumSizeByOwner` 改为按版本汇总口径 |
| `.../infrastructure/user_repository.{h,cpp}` | 新增 `AddUsedStorage`（带 GREATEST 兜底）、`RecalculateUsedStorage`（定期校准口径）、`SetQuotaForTesting` |
| `.../application/upload_service.{h,cpp}` | 新增 `UserRepository` 依赖与 `CheckQuota`；创建会话时配额预检查 + 去重预检查；`Complete` 增加**秒传分支**（0 分块 + 命中已有对象 → 直接引用建文件）；失败收尾统一 `fail_upload()` |
| `backend/src/api/upload_controller.cpp` | 创建响应回真实 `already_exists` 与 `existing_file`（只含自己的文件 id/名字/大小） |
| 前端 | `uploadManager` 支持去重命中时不传分块直接完成；`CreateUploadResponse` 类型补 `already_exists` / `existing_file` |
| 测试 | `integration/dedup_quota_test.cpp`（11 个）、前端 `uploadManager.test.ts` 增加 2 个去重用例 |
| 构建 | `frontend/tsconfig.json` 的 `types` 增加 `node`（测试里用 `node:crypto` 做参考实现），新增 devDependency `@types/node` |

### 18.2 实测结果

```text
后端构建 -> 退出码 0，无警告无错误
后端测试 -> 100% tests passed, 0 failed out of 166（阶段五为 155，新增 11）
前端     -> tsc 无错误；vitest 46/46（阶段五为 44）；vite build 成功
迁移     -> 0005 应用成功；重复执行跳过
E2E      -> _tmp/lan-file-hub/e2e-phase6.ps1 29/29 断言通过
```

端到端实测（真实 HTTP + PostgreSQL）：

| 场景 | 结果 |
|---|---|
| 12 MB 文件首次上传 | 200，配额计入 12,582,912 字节 |
| 同用户同内容再建会话 | `already_exists: true`，返回 `existing_file` 摘要 |
| 秒传（0 分块直接 complete） | 200，**哈希一致、与首次上传同一 `storage_key`、文件记录不同** |
| 配额按逻辑大小 | 第二次引用后用量翻倍（25,165,824） |
| 物理对象数量 | 磁盘上只有 1 份（跨用户也只 1 份） |
| 跨用户预检查 | `already_exists: false`，**不返回别人的文件信息** |
| 跨用户物理去重 | bob 上传同内容后共用同一对象，引用计数 3 |
| 配额在创建阶段 | 409 `QUOTA_EXCEEDED` |
| 配额在完成阶段（会话创建后配额被调小） | **409 `QUOTA_EXCEEDED`**，用量未变，分块已清理 |
| 配额恢复后 | 上传成功，用量按实际字节增加 |
| 统计口径 | `used_storage_bytes` == `SUM(file_versions.size_bytes)` |
| 同名冲突 | 409 `RESOURCE_CONFLICT`，用量不变 |
| 服务端日志 | 无 error/critical |

### 18.3 本阶段解决的技术问题

1. **0 字节文件被服务层误拦**：`CreateSession` 里有一句 `total_size == 0` 就报
   `INVALID_ARGUMENT` 的检查，与 `ComputeTotalChunks(0) == 1` 的设计直接矛盾，
   空文件根本传不上来。「有没有传 `size_bytes`」是控制器该管的事，服务层不能把 `0` 当缺失。
   （阶段六测试发现的阶段五遗留缺陷。）
2. **配额必须防并发**：先查用量再写库的写法下，两个并发上传都会读到旧用量而双双通过。
   改为在**同一个事务**里对用户行 `FOR UPDATE`，校验与累加都在锁内完成。
3. **`SumSizeByOwner` 口径不一致**：原来按 `files.size_bytes` 汇总，阶段七引入多版本后就会算少；
   统一改成按 `file_versions` 汇总，与 `used_storage_bytes`、迁移回填三处口径一致。
4. **去重与信息泄露的取舍**：跨用户物理去重很有价值，但「告诉我这个哈希在不在」等于给出
   文件存在性预言机。做法是物理层照常共享对象、预检查只查自己，并写了测试钉死。
5. **PowerShell 单元素数组会展开**：`SplitChunks` 返回 1 个路径时 `$arr[0]` 变成取字符串首字符
   （`'C'`），表现为莫名其妙的 `Resolve-Path: Cannot find path 'C'`。返回数组要用 `return ,$paths`。
   另外双引号里的 `$var` 会在 `.Replace()` 时被插值，替换模板必须用单引号。

### 18.4 设计说明

- **引用计数不落表**：`SELECT count(*) FROM file_versions WHERE content_hash = ?`。
  独立计数器在删除/恢复/版本切换时极易漂移，而版本表本身就是事实来源。
  阶段八的删除与阶段十一的清理任务按「引用数为 0 才删对象」执行，测试已覆盖。
- **秒传的判定边界**：用服务端数据库里的对象（哈希 + 大小）判定，不用客户端声明；
  客户端谎报哈希的后果仅仅是得到「自己已有文件」的副本，不构成越权读取。
- **配额预检查与权威检查分工**：预检查用声明大小（尽早止损），权威检查用服务端算出的真实字节数
  （在提交事务里）。两处都返回同一个 `QUOTA_EXCEEDED`，前端无需区分。

### 18.5 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 定期校准任务 | `RecalculateUsedStorage` 已实现并被测试调用，但**定时执行**属于阶段十一 |
| 对象清理任务 | 「引用为 0 才删对象」的策略与查询已就绪，实际删除动作随阶段八的永久删除一起做 |
| 管理员调整配额接口 | 阶段十三；当前只能改数据库 |
| 前端配额提示条 | 阶段十的个人设置页会展示用量/配额；当前只有超限时的错误提示 |
| 秒传的浏览器端到端验证 | 前端逻辑用 Vitest + 假后端覆盖；Playwright 在阶段十 |

---

## 19. 阶段七实施记录（2026-09-12）

### 19.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0006_file_versions.sql` | `upload_sessions.target_file_id`（本次上传是给哪个文件加版本）与 `expected_version_id`（If-Match），外加当前版本查询索引。**注意与 0004 的 `file_id` 区分**：那是「完成后生成的文件」 |
| `.../common/result.h` | `Error` 增加 `details`（string→string）与 `Error::WithDetails`，用于把「当前版本 id」这类结构化信息回给客户端 |
| `backend/src/api/error_response.cpp` | 错误响应把 `details` 填进 `error.details`（信封里本来就留了这个字段） |
| `.../infrastructure/file_repository.{h,cpp}` | `AddVersion`（事务内锁文件行 → 校验 If-Match → 版本号 max+1 → 摘旧 current → 插新版本 → 换 current + 同步 files 的 hash/size/key → 配额检查与累加）、`FindVersion`、`FindCurrentVersion` |
| `.../application/file_service.{h,cpp}` | `ListVersions`、`PrepareVersionDownload`、`RestoreVersion`（追加新版本、重复恢复幂等、对象缺失拒绝） |
| `.../application/upload_service.{h,cpp}` | `CreateUploadRequest` 增加 `target_file_id`/`expected_version_id`；新增版本路径（目录与文件名以文件为准、跳过同名检查）；抽出 `persist_content()` 统一「建文件 / 加版本」并复用到秒传路径 |
| `backend/src/api/file_controller.{h,cpp}` | 3 个版本端点 + `If-Match` 解析（支持裸 id、带引号 ETag、`*`）；历史版本下载支持 Range，响应带 `X-File-Version` 与 `.v<n>` 文件名 |
| `backend/src/api/upload_controller.cpp` | 接受 `file_id` / `expected_version_id` 与 `If-Match` 头；新增版本时不再强制要求 `folder_id`/`file_name` |
| 前端 | `types/version.ts`、`services/versionsApi.ts`、`features/versions/useVersions.ts`（含冲突文案与 `details` 解析）、`components/VersionHistoryModal.tsx`（列表 / 下载 / 二次确认恢复）、文件列表新增「版本历史」入口；`ApiError` 增加 `details` |
| 测试 | `integration/version_conflict_test.cpp`（12 个） |

### 19.2 实测结果

```text
后端构建 -> 退出码 0，无警告无错误
后端测试 -> 100% tests passed, 0 failed out of 178（阶段六为 166，新增 12）
前端     -> tsc 无错误；vitest 46/46；vite build 成功
迁移     -> 0006 应用成功；重复执行跳过
E2E      -> _tmp/lan-file-hub/e2e-phase7.ps1 48/48 断言通过
```

端到端实测（真实 HTTP + PostgreSQL）：

| 场景 | 结果 |
|---|---|
| 10 MB 首版 + 14 MB 新版本（带 `If-Match`） | 文件 id / 文件名不变，`current_version_id` 切换，大小与哈希更新 |
| 版本列表 | 2 条，新版本在前，`is_current` 标记正确，**响应不含 `storage_key`** |
| 历史版本下载 | 200，内容等于 v1 原始字节；`Range: bytes=0-4` → 206 + `Content-Range` + `X-File-Version: 1` |
| 旧对象 | v1 与 v2 的对象**同时存在于磁盘**（没有覆盖） |
| 过期 `If-Match` | **409 `FILE_VERSION_CONFLICT`**，`details.current_version_id` 是真实当前版本；版本数与内容都没被改动 |
| 用最新版本重试 | 200，成功生成 v3 |
| 恢复到 v1 | 200，当前内容回到 v1，**版本数变 4**，v2/v3 都还在 |
| 重复恢复 | 200 且版本数不变（幂等） |
| 再恢复到 v3 | 200，当前内容等于 v3（双向可回退） |
| 他人访问 | 列版本 / 下载历史版本 / 恢复 一律 404 |
| 配额 | `used_storage_bytes` == `SUM(file_versions.size_bytes)`，多版本都计入 |
| 不存在的版本 | 404 `RESOURCE_NOT_FOUND` |
| 服务端日志 | 无 error/critical |

### 19.3 本阶段解决的技术问题

1. **`upload_sessions.file_id` 语义冲突**：0004 的 `file_id` 表示「完成后生成的文件」，
   而「给哪个文件加版本」是另一个含义。迁移里若继续叫 `file_id`，
   `ADD COLUMN IF NOT EXISTS` 会**静默跳过**（列已存在），字段根本没建上。
   改名为 `target_file_id` 并写清注释。
2. **`Error` 缺少结构化返回通道**：版本冲突必须告诉客户端「当前版本是哪个」。
   统一响应信封里本来就有 `error.details`，于是给 `Error` 加了 `details` 字段并填充它，
   而不是把版本 id 拼进 message 或另开一个响应形状。
3. **版本号必须在锁内计算**：先查 `MAX(version_number)` 再插入的写法在并发下会撞唯一索引。
   改为在同一个事务里 `SELECT ... FROM files WHERE id = $1 FOR UPDATE` 之后再算，
   并发提交因此只有一个成功（测试用两个线程验证）。
4. **恢复的幂等语义**：如果「恢复」每次无条件追加版本，用户连点两次就会刷出两个一模一样的版本，
   配额也跟着涨。改为「当前内容已等于目标版本 → 不追加」，并用测试钉死。

### 19.4 设计说明

- **追加式版本模型**：没有「覆盖当前版本」这条路径。上传新内容与恢复历史版本都只是
  `file_versions` 追加一行 + 切换 `files.current_version_id`，因此历史永远可下载、可回退。
- **`If-Match` 可选**：不带 `If-Match` 时按「追加」处理（不会覆盖任何东西），
  带了才做乐观并发校验。这样既支持「我就是要覆盖」的语义，也支持严格的多端协作。
- **秒传与新版本共用 `persist_content()`**：去重命中时同样能作为「新版本」写入，
  避免两条路径各写一份建文件/建版本逻辑。
- **`details` 的安全边界**：只放客户端本来就能看到的标识（版本 id）；
  `Error::detail`（内部信息）依然只进日志。

### 19.5 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 前端版本页面的浏览器实测 | 组件逻辑已接好并有类型检查/构建验证；Playwright 端到端在阶段十 |
| 版本数量上限 | 当前不限制单个文件的版本数；是否要保留策略（如最多 N 个版本）留到阶段十一与清理策略一起定 |
| 版本与回收站的交互 | 文件进回收站后版本如何处理，阶段八统一处理 |
| 恢复历史版本时的审计记录 | `audit_logs` 在阶段八建表，届时补写管理员与恢复操作 |

---

## 20. 阶段八实施记录（2026-09-12）

### 20.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0007_trash_and_cleanup.sql` | `audit_logs`（操作者 / 动作 / 资源 / JSONB 明细）、`cleanup_tasks`（待删对象 + 尝试次数 + 退避时间 + 状态，部分唯一索引保证同一 Key 只有一条待处理任务）、回收站扫描索引 |
| `.../infrastructure/audit_repository.{h,cpp}` | 审计写入与查询、清理任务仓储（`Enqueue` 幂等、`ClaimDue` 用 `FOR UPDATE SKIP LOCKED` 领取、`MarkDone`、`MarkFailed` 带指数退避与次数上限） |
| `.../infrastructure/trash_repository.{h,cpp}` | 文件与文件夹 `UNION ALL` 成统一回收站清单（支持分页、排序、类型筛选，参数全部绑定）、恢复文件/文件夹子树、`IsActiveOwnedBy`、同名冲突检查、`PurgeFile`/`PurgeFolder`（事务内取对象 Key + 删记录 + 回收配额）、过期条目扫描 |
| `.../application/trash_service.{h,cpp}` | 列表 / 恢复（幂等、父目录兜底到根目录、同名冲突带 `details.conflict_name`）/ 永久删除（入队清理任务 + 审计）/ 软删除文件 / 过期清理，全部写审计 |
| `.../application/cleanup_service.{h,cpp}` | 后台任务：`std::jthread` + `stop_token`，轮询清理任务（删除前复查引用计数）、失败退避重试、定期回收站过期清理；`RunOnce()` 供测试同步驱动 |
| `.../infrastructure/file_repository.{h,cpp}` | `FindOwnerId`、`SoftDelete`；`FolderRepository::SoftDeleteSubtree` 现在会**把子树里的文件一并置为 TRASHED**（回收站因此是完整清单） |
| `backend/src/api/trash_controller.{h,cpp}`、`file_controller` 的 `DELETE /files/{id}` | 回收站三个端点 + 软删除入口 |
| `backend/src/main.cpp` | 装配 `TrashRepository` / `AuditLogRepository` / `CleanupTaskRepository` / `TrashService` / `CleanupService`，启动后台线程并在退出时有序停止 |
| 前端 | `types/trash.ts`、`services/trashApi.ts`、`features/trash/useTrash.ts`、`pages/TrashPage.tsx`（筛选、分页、恢复与永久删除二次确认、保留期提示），文件列表新增删除与「回收站」入口，新增 `/trash` 路由 |
| 测试 | `integration/trash_flow_test.cpp`（16 个） |

### 20.2 实测结果

```text
后端构建 -> 退出码 0，无警告无错误
后端测试 -> 100% tests passed, 0 failed out of 194（阶段七为 178，新增 16）
前端     -> tsc 无错误；vitest 46/46；vite build 成功
迁移     -> 0007 应用成功；重复执行跳过
E2E      -> _tmp/lan-file-hub/e2e-phase8.ps1 42/42 断言通过
```

端到端实测（真实 HTTP + PostgreSQL + 真实后台线程）：

| 场景 | 结果 |
|---|---|
| 软删除文件 | 200；目录列表里消失；元数据/下载/版本一律 404 |
| 磁盘对象 | 软删除后**仍在盘上**，恢复是廉价的 |
| 配额 | 软删除后仍占用（9,437,184 字节） |
| 回收站列表 | 1 项，含名字、类型、删除时间、保留天数（30） |
| 恢复 | 200 且回到原目录；重复恢复 200 且 `already_restored: true` |
| 同名冲突 | **409 `RESOURCE_CONFLICT`**，`details.conflict_name` 给出冲突者，状态未被改动 |
| 原目录不存在 | 恢复到根目录，响应 `moved_to_root: true` |
| 永久删除 | 200，报告释放字节数与入队对象数（`async: true`），配额立即回收 |
| 异步清理 | HTTP 请求里对象仍在、清理任务已建；后台线程 1 秒内删除对象并把任务置 `DONE` |
| 他人访问 | 回收站不可见、恢复/永久删除 404 |
| 审计 | 删除 / 恢复 / 永久删除各留痕，含 `actor_id` |
| 服务端日志 | 无 error/critical |
| 后台服务退出 | 启动与停止日志成对出现，无线程泄漏 |

### 20.3 本阶段解决的技术问题

1. **`folders` 与 `files` 的列名不一致**：回收站用 `UNION ALL` 合并两张表，
   我按 `files.original_name` 的写法写了 `p.original_name` 去查文件夹，
   PostgreSQL 直接报 `column p.original_name does not exist`。
   文件夹表里叫 `name`，两半都改成 `p.name`。
2. **`text = uuid` 类型错误**：子查询里为统一类型把 `id`/`owner_id` 都 `::text` 了，
   外层再用 `= $1::uuid` 比较就会报 `operator does not exist: text = uuid`。
   外层条件改成按文本绑定（顺便让非法 UUID 自然落到 404）。
3. **恢复的"幂等"不能变成"对谁都成功"**：最初的幂等分支写成"回收站里找不到就当已恢复"，
   结果**别人也能拿到 200**。新增 `IsActiveOwnedBy` 区分「确实已经恢复过」与「不属于你」，
   后者返回 404。
4. **删除文件夹时文件没进回收站**：只改文件夹状态的话，回收站里看不到其中的文件，
   也没法单独恢复某个文件。改为子树内文件一并置 `TRASHED`。
5. **模拟磁盘删除失败要动点脑筋**：`std::filesystem::remove` 对空目录也会成功，
   所以"放一个同名目录占位"并不足以制造失败；必须放一个**非空**目录才真的失败。
6. **PowerShell 断言必须区分"没检查到"与"真的没有"**：脚本里对象路径少写了 `storage\`
   前缀，`Test-Path` 永远为假，用例"通过"得毫无意义——路径修正后才暴露真实行为。

### 20.4 设计说明

- **软删除只改状态**：对象不动、配额不减，所以恢复不需要任何磁盘拷贝，
  也不会出现"恢复时对象已被回收"的窗口。
- **永久删除的两段式**：数据库事务 + 清理任务。HTTP 请求内不做磁盘删除，
  既满足"不能长时间阻塞请求"，也让磁盘失败可以重试而**不影响已提交的数据库状态**。
- **清理任务用部分唯一索引去重**：同一对象 Key 只留一条待处理任务，
  重复的删除请求不会堆任务；多条路径（手动 purge、过期清理）指向同一对象时也只删一次。
- **删除前复查引用计数**：内容寻址意味着一个对象可能被多个版本引用（甚至跨用户），
  计数不为 0 就跳过并把任务标记完成，避免"删了别人还在用的对象"。
- **审计写失败不回滚业务**：审计失败会记 error 日志并继续，避免因为日志表问题让用户操作失败；
  这一点是有意取舍，已在代码里注明。

### 20.5 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 前端回收站的浏览器实测 | 组件已接好并通过类型检查与构建；Playwright 端到端在阶段十 |
| 回收站按名称搜索 | 阶段十与全局搜索一起做 |
| 管理员视角的回收站（看全部用户） | 服务层已支持 owner 为空的管理员查询，接口层在阶段十三开放 |
| 审计日志查询接口 | 表与写入已完成，查询 API 属于阶段十三 |
| 清理任务的并发 worker 数 | 当前单线程轮询；多 worker 的领取逻辑已用 `SKIP LOCKED` 写对，阶段十一压测 |
| 磁盘空间预检 | 仍在阶段十二 |

---

## 21. 阶段九实施记录（2026-09-12）

### 21.1 交付物

| 文件 | 说明 |
|---|---|
| `migrations/0008_permissions_and_shares.sql` | `permissions`（资源×用户×级别，唯一索引保证一人一资源一条、多态 `resource_id`）、`share_links`（`token_hash` 唯一、过期/撤销/次数、`download_count`） |
| `.../infrastructure/permission_repository.{h,cpp}` | 授权 Upsert/删除/列表/直接查询、**按 materialized path 查整条路径的授权**、子树悬空授权清理；分享创建（过期时间由数据库 `now()` 计算）、按 Token 哈希查可用记录、撤销（幂等）、**原子消费下载次数**；`GenerateShareToken`（OpenSSL `RAND_bytes` 32 字节 → base64url） |
| `.../application/permission_checker.{h,cpp}` | `PermissionLevel`（NONE/READ/WRITE/OWNER）、`RequiredLevel`/`Satisfies`；`ResourceDescriptor` 扩展出 `resource_type/resource_id/folder_id`；新增 `GrantBasedPermissionChecker`（管理员 → 所有者 → 直接授权 → 祖先目录授权取最高）；`EffectiveLevel` 提升为基类虚函数 |
| `.../application/sharing_service.{h,cpp}` | 资源解析（先文件后目录）、权限列表/授予/撤销/有效级别、分享创建（明文 Token 只返回一次）、列表、撤销、公开查看与公开下载、管理员访问审计 |
| `backend/src/api/permission_controller.{h,cpp}`、`share_controller.{h,cpp}` | 4 个权限端点、3 个分享管理端点、2 个公开端点（公开端点不挂 AuthFilter，但服务端完整校验） |
| 权限检查接入点 | `FileService`（元数据/下载/上传初始化）、`FolderService`、`UploadService`（新建与加版本）、`TrashService`（软删除）全部改为传完整资源描述符 |
| `.../application/trash_service.{h,cpp}` | 永久删除时清理指向该资源的授权与分享链接（多态引用没有外键，必须显式删） |
| 前端 | `types/share.ts`、`services/sharingApi.ts`、`features/sharing/useSharing.ts`、`components/ShareDialog.tsx`（创建/复制/撤销、明文 Token 只显示一次、访问次数与到期时间）、`components/PermissionDialog.tsx`（授权列表/授权/撤销/规则说明）；文件与目录行都加了「分享」「权限」入口 |
| 测试 | `integration/sharing_flow_test.cpp`（21 个） |

### 21.2 实测结果

```text
后端构建 -> 退出码 0，无警告无错误
后端测试 -> 100% tests passed, 0 failed out of 215（阶段八为 194，新增 21）
前端     -> tsc 无错误；vitest 46/46；vite build 成功
迁移     -> 0008 应用成功；重复执行跳过
E2E      -> _tmp/lan-file-hub/e2e-phase9.ps1 54/54 断言通过
```

端到端实测（真实 HTTP + PostgreSQL）：

| 场景 | 结果 |
|---|---|
| 授权前协作者 | 有效级别 `NONE`，下载 404 |
| 给目录授 READ | 文件继承为 `READ`，下载 200 且内容一致 |
| READ | 不能上传（404）、不能改权限（404） |
| 升级为 WRITE | 可上传、可软删除；永久删除仍被拒（404，需要 OWNER） |
| 归属 | 协作者上传的文件在数据库里 `owner_id` = 目录所有者 |
| 权限列表 | 1 条，含被授权人用户名；撤销后回到 `NONE` |
| 创建分享 | Token 43 字符；数据库只有 64 位哈希；列表不含明文也不含哈希 |
| 公开访问 | 200，含文件名与大小，**不含 owner/对象 Key**；下载内容一致；计数 +1 |
| 下载次数上限 | 第 3 次被拒（404），不泄露原因 |
| 撤销 | 200 且重复撤销仍 200；撤销后公开访问 404 |
| 越权 | 无关用户列/建/撤销分享全部 404 |
| 目录分享 | 可查看、不可下载（403 且给出原因） |
| 审计 | 授权 2 条、创建分享 3 条（含操作者） |
| 服务端日志 | 无 error/critical |

### 21.3 本阶段解决的技术问题

1. **权限检查调用点只传了 `owner_id`**：阶段三的检查接口只有所有者一个字段，
   继承逻辑即使写好了也不会生效——所有调用点（元数据、下载、上传、软删除）都补齐了
   `resource_type/resource_id/folder_id`。这是集成测试先发现的："给目录授 READ 后文件仍下载不了"。
2. **上传会话归属搞错**：会话的 `user_id` 原来写成目录所有者，导致被授权的协作者能建会话
   却传不了分块（`BeginChunk` 的所有者校验直接 404）。改为**会话属于上传者**，
   文件归属目录所有者在 `Complete` 里单独解析：协作者既能续传自己的上传，
   文件也不会落到协作者名下、不占他们的配额。
3. **`EffectiveLevel` 必须是基类虚函数**：`SharingService` 持有 `ResourcePermissionChecker&`，
   直接调具体类接口编译不过；提升为基类虚函数（默认实现只认所有者）后，
   降级实现也能给出合理答案。
4. **过期时间不能自己拼字符串**：最初想用 `"@interval:3600"` 这种约定塞进 SQL，
   等于把业务逻辑藏进字符串。改为仓储接收 `expires_in_seconds`，由数据库
   `now() + make_interval` 计算，创建后回读整行，响应里的 `expires_at` 才是真实值。
5. **PowerShell 字符串替换改 C++ 源码踩坑两次**：双引号包裹的替换模板会把代码里的
   `"` 与反引号 n 当转义处理，一次写出字面量导致编译失败，一次直接语法错误。
   批量改 C++ 一律改用编辑工具。

### 21.4 设计说明

- **继承用 materialized path**：阶段三就为 `folders` 建了 `path_key`，
  一条 `$path LIKE path_key || '%'` 取出整条路径上的授权，不需要递归查询；
  只统计 ACTIVE 目录，避免用删除状态绕过。
- **取最高而不是最近**：第一版没有拒绝规则，语义因此可预测（docs/permissions.md §3.1 写死算法，
  集成测试逐条覆盖）。
- **动作分四档**：读 / 写 / 软删除 / 管理分别对应 READ / WRITE / WRITE / OWNER，
  永久删除与分享管理按 OWNER 把关，因为它们改变"别人能看到什么"。
- **下载次数用一条 UPDATE 完成检查与自增**：`WHERE download_count < max_downloads`
  配合 `RETURNING`，并发下载不可能同时通过；被拒绝的请求也不计数。
- **失效原因统一 404**：过期、撤销、超次、Token 不存在对外同一个结果，避免用错误码差异探测状态。
- **公开接口单独一层**：不挂认证过滤器，但走同一套服务端校验；响应只给展示必需的信息。

### 21.5 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 前端分享/权限页面的浏览器实测 | 组件已接入并通过类型检查与构建；Playwright 在阶段十 |
| 公开分享的独立落地页 | 公开接口已可用（curl 验证），`/s/{token}` 页面在阶段十 |
| 「分享给我的」列表界面 | 后端已能按 subject 查授权（`permissions_subject_idx`），界面在阶段十 |
| 按用户名选人（而非填 UUID） | 需要用户搜索能力，属于阶段十三的管理员/用户目录 |
| 管理员代理访问的完整审计面 | 已在资源读取与分享路径写入；更系统的审计在阶段十三收口 |
| 密码保护的分享 | 文档明确列为非 MVP 可选功能，未实现 |

---

## 22. 阶段十实施记录（2026-09-12，进行中）

阶段十是**完整前端界面**（13 个页面、20 项交互、页面状态与上传状态分离、Vitest + Playwright）。

### 22.1 后端侧已完成（本阶段新增）

| 项 | 说明 |
|---|---|
| `GET /api/v1/shared-with-me` | 「分享给我的」列表：按 `permissions` 的**直接授权**返回资源类型、名字、大小、权限、所有者与授权时间；只列 ACTIVE 资源，已删除的不出现 |
| `PermissionRepository::ListSharedWith` | 一条 SQL 连接 files/folders 取资源名，参数全部绑定 |
| 测试 | `SharingTest.SharedWithMeListsDirectlyGrantedResources`（授权前为空、授权后含类型与名字、别人的列表为空、资源永久删除后消失） |

后端验收（本阶段已跑通）：`clang-format` 0 违规、构建 0 错误 0 警告、**216/216 测试通过**。

### 22.2 前端侧（已实施并验证）

| 区域 | 文件 | 说明 |
|---|---|---|
| 应用外壳 | `src/app/AppLayout.tsx`、`src/app/navigation.ts` | 左侧导航（我的文件 / 最近文件 / 分享给我的 / 回收站 / 设置）+ 顶部用户信息与退出；导航项集中定义，测试覆盖 |
| 页面 | `src/pages/{SharedWithMePage,PublicSharePage,RecentPage,SettingsPage}.tsx` | 分享给我的（走新接口）、公开分享落地页 `/s/{token}`（无需登录）、最近文件、个人设置 |
| 组件 | `src/components/{FileDetailDrawer,FileGrid}.tsx` | 文件详情抽屉（元数据 + 版本/分享/权限入口）、网格视图 |
| 纯逻辑 | `src/features/files/{viewState,listView}.ts`、`src/features/sharing/publicShareState.ts` | 页面状态机（idle/loading/success/error/retrying/offline/cancelled）、排序与过滤、公开分享页状态映射——全部是纯函数，可单测 |
| 测试 | `tests/{viewState,listView,navigation,publicShareState}.test.ts` | 61 个新用例（前端总数 46 → 107） |
| 端到端脚手架 | `playwright.config.ts`、`tests/e2e/core-flow.spec.ts` | 登录 → 浏览 → 上传 → 下载 → 删除 → 回收站恢复 → 创建分享；**未执行**（见 22.3） |

前端验收（本轮由我独立重跑，不采信转述）：

```text
npx tsc --noEmit   -> 无类型错误（strict + noUncheckedIndexedAccess）
npx vitest run     -> Test Files 8 passed，Tests 107 passed
npx vite build     -> ✓ built
```

### 22.3 已确认的外部阻塞

| 项 | 说明 |
|---|---|
| Playwright 浏览器二进制 | 本机未安装，且**当前网络代理关闭**，`npx playwright install` 无法下载。用例已写好但**未在本机执行**——这是未验证项，不得写成已通过。代理恢复后需补跑并回填结果。 |

### 22.4 未验证 / 遗留

| 项 | 说明 |
|---|---|
| 浏览器内的真实交互 | 所有前端逻辑用 Vitest + jsdom 覆盖；真实点击路径由 Playwright 用例描述但未执行 |
| 全局搜索 | 当前搜索只在已加载的当前页过滤；服务端搜索属于后续阶段 |
| 列表服务端分页控件 | 回收站有分页，文件列表仍显示第一页（总数已提示） |
| 「最近文件」的数据来源 | 目前复用文件列表接口按修改时间排序；专门的最近访问记录属于后续阶段 |

---

## 23. 阶段十一实施记录（2026-09-12，进行中）

阶段十一是**后台任务框架与过期清理**。已有的 `CleanupService`（阶段八建立：对象异步删除 + 回收站过期清理）
在本阶段继续扩展。

### 23.1 本轮已完成：过期上传会话清理

| 项 | 说明 |
|---|---|
| `UploadSessionRepository::ExpireOverdue(limit)` | 一条 UPDATE 把 `INITIATED`/`UPLOADING` 且 `expires_at < now()` 的会话落成 `EXPIRED` 并返回 id（终态不重复处理，幂等） |
| `CleanupService` | 新增 `UploadSessionRepository` 依赖与 `sessions_expired` 统计；每轮扫描时**先落状态、再清残留分块**（数据库记录 + `storage/tmp/uploads/<id>/`）。顺序不能反：中途崩溃重跑只是多清一次 |
| 测试 | `TrashTest.ExpiredUploadSessionsAreSweptByCleanup`（传一个分块 → 把过期时间提前 → 跑一轮清理 → 断言会话 `EXPIRED`、分块记录清零、统计 +1） |
| 验收 | 构建 0 错误 0 警告；**217/217 测试通过** |

### 23.1b 本轮第二批：启动恢复

| 项 | 说明 |
|---|---|
| `CleanupTaskRepository::ResetPendingForStartup()` | 一条 UPDATE 把 `PENDING` 且 `next_attempt_at` 在未来的任务立即置为到期，并在日志里打出恢复数量 |
| `CleanupService::Start()` | 启动后台线程前先做启动恢复（失败不阻塞启动，只记 warn）——否则重启后要干等一个退避周期（最长约 17 分钟） |
| 测试 | `TrashTest.StartupRecoveryReschedulesPendingTasks`：入队 → 领取 → 标记失败（退避到未来）→ 断言领取不到 → 启动恢复 → 断言可立即重试且 attempts 递增 |

### 23.1c 本轮第三批：孤儿临时文件清理

| 项 | 说明 |
|---|---|
| `ObjectStorage::ListStaleTempKeys(min_age_seconds)` | 新接口：列出 `tmp/` 下**超过指定年龄**的文件 Key（本地实现递归遍历，跳过权限问题，逐个校验 Key 合法性） |
| `CleanupServiceOptions::stale_temp_seconds` | 默认 24 小时，**与会话 TTL 对齐**：活跃上传持续写入不会被误判，超时会话的残留由两条路径之一收走 |
| `CleanupService` 扫描阶段 | 每轮把陈旧临时文件写入 `cleanup_tasks`，复用阶段八就有的重试/退避/死信机制，不另造一套删除逻辑 |
| 测试 | `LocalObjectStorageTest.ListsOnlyStaleTempFiles`：48 小时前的遗留文件会被列出、刚写入的不会；阈值放宽到 0 时两者都被列出（证明过滤确实按年龄生效） |

设计取舍：**按年龄判定而不是查引用**。查引用需要反查"这个 tmp Key 属于哪个上传会话"，而崩溃残留的会话可能已经不在数据库里；
年龄阈值与 TTL 对齐后两者等价，且判定是纯本地操作，不需要额外查询。

### 23.1d 本轮第四批：对象与数据库不一致扫描

| 项 | 说明 |
|---|---|
| `ObjectStorage::ListObjectKeys()` | 新接口：列出 `objects/` 下全部对象 Key（本地实现递归遍历，Key 逐个校验；对象是永久数据，不带年龄过滤） |
| `FileRepository::ListDistinctContentHashes(limit)` | 列出所有被版本引用的内容哈希 |
| `CleanupService` 扫描阶段 | 两类问题**刻意区别对待**：<br>• 磁盘有对象但引用计数为 0 → 写进 `cleanup_tasks` 回收（内容寻址下没人用就是垃圾）<br>• 有版本引用但对象缺失 → **只写 error 日志告警，绝不自动删或改数据库**（可能是唯一副本，误删不可逆） |
| 统计 | `orphan_objects` / `missing_objects` 计入 `CleanupStats` |
| 测试 | `ConsistencyScanFindsOrphansButNeverDeletesReferencedFiles`：人造孤儿对象 → 被回收；**仍被引用的对象不动**；删掉磁盘对象后 → `missing_objects` 增加，且**数据库记录原样保留** |

本轮还修掉两个自己造的缺陷：

1. **测试场景无效**：`ResolveKey` 不会创建目录，而测试直接在 `objects/ff/` 下写文件——`ofstream` 静默失败，
   于是"孤儿对象"根本没被创建，断言"它被删掉了"却因为文件不存在而**假通过**。
   这类"因为没生效所以通过"的用例比失败更危险，现在加了 `out.good()` 断言。
2. **扫描上限语义写错**：上限约束的是"发现了多少个孤儿"，而不是"检查了多少个对象"，
   库里被引用对象多时一轮会扫全表。改为按**已检查数量**封顶。

### 23.1e 本轮第五批：存储统计定期校准

| 项 | 说明 |
|---|---|
| `FileRepository::RecalculateAllUsedStorage()` | 一条 SQL 用「所有文件版本大小之和」重算全部用户的 `used_storage_bytes`，**只更新有偏差的行**并返回修正数量。放在 `SumSizeByOwner` 旁边：同一个"按版本汇总"的关注点，也避免为一个查询去改 `CleanupService` 的构造签名（那会牵动 main 与两处测试夹具） |
| `CleanupServiceOptions::stats_recalibrate_every_ticks` | 默认 300 个轮询周期（约 10 分钟）校准一次 |
| 统计 | `stats_corrected_users` 计入 `CleanupStats`；修正数 > 0 时按 **warn** 记录，因为那意味着某条业务路径漏记了用量，值得排查 |
| 测试 | `StorageStatsRecalibrationFixesDrift`：上传 2 MiB → 人为把统计改错 4096 字节 → 校准后回到真实值、且与按版本汇总口径一致、修正数量被记录 → **再跑一轮修正数不再增长**（无偏差时不动任何行） |

> 顺带修掉自己写的一句空断言：`EXPECT_EQ(x, x)` 自比自，永远成立、毫无验证价值，
> 已换成"第二轮校准不再产生新修正"这个真命题。

### 23.1f 阶段十一收口：真实进程内验证

在**真实服务进程**里验证后台线程（不是在单测里调用 `RunOnce`）：

```text
[1] 启动日志出现「后台清理任务已启动 | poll_ms=2000 | batch=32 | max_attempts=5」   ✅
[2] 人为造一个孤儿对象 objects/ee/<hash> + 一条已到期的 cleanup_task            ✅
[3] 后台线程 0.7 秒内删除该对象（轮询间隔 2 秒）                                ✅
[4] 任务状态被置为 DONE                                                        ✅
[5] 日志无 error/critical                                                      ✅
[6] 进程结束后无残留进程                                                        ✅
```

> **踩坑记录**：第一遍检查时 [1]/[5]/[7] 全部报 False，看起来像"日志没打"。
> 实际原因是 **PowerShell 5.1 的 `Get-Content -Raw` 默认按 ANSI 解码**，
> 中文被弄坏导致正则匹配失败；改用 `-Encoding UTF8` 后匹配正常。
> 教训：**用中文关键字去 grep 日志时必须显式指定 UTF-8**，否则会把"编码问题"误判成"功能缺失"。
>
> [7]「优雅停止日志」在本次验证中未出现，原因是我用 `Stop-Process -Force` 强杀进程——
> Windows 上这等价于 TerminateProcess，进程没有机会执行 `run()` 之后的清理代码。
> 优雅退出路径（`run()` 返回后调用 `Stop()`，再关闭日志）在代码里是有序的，
> 且 `CleanupService` 的析构函数本身会 `Stop()`（每个测试用例都在走这条路径），
> 但**"控制台 Ctrl+C 后日志出现停止行"这一条没有在本机自动化验证**，属于未验证项。

### 23.2 任务书逐条核对

| # | 任务书条目 | 结论 |
|---|---|---|
| 1 | 后台 Worker 与任务队列 | ✅ `CleanupService`（`std::jthread` + `stop_token`）+ `cleanup_tasks` 表 |
| 2 | 过期会话与临时分块清理 | ✅ §23.1、§23.1c，均有测试 |
| 3 | 永久删除任务 | ✅ 阶段八：事务删记录 → 对象入队 → 异步删除 |
| 4 | 对象与数据库不一致扫描 | ✅ §23.1d，且区分"可删的孤儿"与"只告警的缺失对象" |
| 5 | 重试与指数退避 | ✅ 阶段八：1s→2s→4s…上限 5 次 |
| 6 | 死信 | ⚠️ **部分**：`FAILED` 状态与重试上限已就绪；**人工重试/丢弃入口是管理员接口，属阶段十三任务书**，此处显式移交，不算作已完成 |
| 7 | 优雅退出与启动恢复 | ✅ 启动恢复 §23.1b；优雅退出见下方未验证项 |



### 23.3 前端子代理提出的待办（已确认，转入后续阶段）

| # | 问题 | 结论 |
|---|---|---|
| 1 | `GET /shared-with-me` 未发分页参数 | 后端当前固定 `limit=200`，够用；真正的分页随阶段十三的管理员/用户目录能力一起做 |
| 2 | 「分享给我的」下载走 `GET /files/{id}/download` | **正确**：阶段九的权限模型里 READ 授权即可下载，不需要分享专用端点 |
| 3 | 公开分享失效的错误码 | 后端**已经是**统一 `404 RESOURCE_NOT_FOUND`（过期/撤销/超次/不存在都不区分），与子代理的建议一致；前端已删掉自造的 `SHARE_EXPIRED` |
| 4 | 「最近文件」页面 | 需要后端提供跨目录按时间聚合的接口；前端页面已如实标注"待接口"，不造假数据。列入阶段十三 |
| 5 | 列表排序/搜索只作用于当前页 | 文件列表接口尚无分页参数（回收站有）；服务端分页+搜索列入阶段十三 |

### 23.4 发现的既有文档歧义（待定，未改）

`docs/frontend.md` §4.1 页面状态与 §4.2 上传状态里有 **`retrying` 与 `cancelled` 两个同名值**，
但含义不同（一个是页面重试、一个是上传重试）。前端用 `isPageState`/`isUploadState`
两个判定函数把它们隔开，并在 `viewState.ts` 注释里写明。

处理建议（留给后续决定）：给上传状态加前缀（如 `upload_retrying`），
或在该文档里显式说明"两个状态机允许同名，语义以所在状态机为准"。当前**不改**，
因为改名字会同时动文档与前端类型，属于跨阶段改动。

---

## 24. 阶段十二实施记录（2026-09-12，完成）

阶段十二是**限流、下载控制与安全加固**。任务书 22 条里，相当一部分在阶段四～九已经落地
（请求体/单文件大小限制、用户配额、路径穿越防护、文件名与 Content-Disposition 净化、
Token 过期、分享随机性与过期校验、SQL 全参数化），本节只记录**本轮新增**的部分。

**收口状态**：22 条全部有结论（逐条见 24.2）；后端测试 **264/264 通过**；真实 HTTP 验收脚本
`e2e-phase12.ps1` **47/47 通过**（9 个场景 + 日志检查）。发现的三个真实缺陷（预认证接口可被超大
请求撑住、客户端错误被误报成 503、下载断开无法取消）都已修复并留了回归测试。

### 24.1 本轮完成：磁盘剩余空间检查（任务书第 8 条）

阶段八就记录过这个缺口："分块写入会检查写入结果并报错，但**未做写入前的可用空间预检**"。

| 项 | 说明 |
|---|---|
| `ObjectStorage::AvailableBytes()` | 新接口，直接问操作系统（`std::filesystem::space`），**不缓存**——缓存会让"磁盘快满"的判断滞后 |
| 创建上传会话时 | 用客户端声明的大小预检：`可用 < 声明大小 + 保留空间` → `507 STORAGE_FULL`，避免传完几十 GB 才失败 |
| 合并分块前 | **再查一次**：分块传完后磁盘可能已被写满，而且合并需要再占一份等量空间（分块与合并结果同时存在） |
| 单文件流式上传 | 事前不知道总大小，但至少守住保留空间下限，否则写到一半必然失败 |
| `MIN_FREE_DISK_BYTES` | 新配置项，默认 **1 GiB**；低于它就拒绝新上传，避免把盘吃到 0 影响数据库与日志 |
| 测试 | `LocalObjectStorageTest.ReportsAvailableDiskSpace`（真实查询到正数）；`DedupQuotaTest.RejectsUploadWhenDiskReserveIsNotMet`（把保留空间设成 1 PiB → 确定性触发 `STORAGE_FULL`；设回 0 → 同一请求成功，证明拦截确实来自磁盘检查而非别的路径） |

> 测试手法说明：不去真的塞满磁盘，而是把"必须保留的空间"设成不可能满足的值。
> 这样既确定、又不会在 CI 上留下一个快满的卷。

### 24.3 本轮完成：登录与上传限流（任务书第 1、2 条）

| 项 | 说明 |
|---|---|
| 实现 | `common/rate_limiter.h/.cpp`：进程内滑动窗口（`max_events` / `window` / `max_keys`）。不引入 Redis 依赖——限流是保护措施，不该让业务正确性依赖缓存可用性 |
| 被拒绝的尝试不记账 | 否则持续轰炸会把窗口一直顶住，变成"永久封禁"；单测 `RejectedAttemptsDoNotExtendTheBlock` 固定这个语义 |
| 桶数量有界 | 超过 `max_keys` 先清空已过期桶，再淘汰最久未访问的 1/8，避免攻击者用海量 key 把限流器自身打成内存热点 |
| 登录两个维度 | 账号维度 `ip|identifier`（默认 10 次/分钟）+ 来源 IP 维度（默认 60 次/分钟，为 NAT 后多用户留余量）；登录成功 `Reset(account_key)`，避免正常用户被自己的历史失败拖住 |
| 上传维度 | 按用户限制创建会话频率（默认 120 次/分钟），与阶段五的"并发会话上限"互补：一个限同时数量，一个限单位时间次数 |
| 响应 | `429 RATE_LIMITED` + `Retry-After`（秒）；限流先于口令校验，正确口令在窗口内同样被拒 |
| 配置 | `LOGIN_RATE_LIMIT_PER_MINUTE`、`LOGIN_IP_RATE_LIMIT_PER_MINUTE`、`UPLOAD_RATE_LIMIT_PER_MINUTE`（`.env.example` 已同步） |
| 单测 | `rate_limiter_test.cpp` 8 项（含窗口滑动、拒绝不记账、桶上限、`ClientIpFrom` 取 XFF 最左段） |

### 24.4 本轮完成：请求体上限与非法 ID 的错误码（任务书第 20、21 条）

阶段验收脚本一开始就抓到一个真实缺陷：`POST /api/v1/auth/login` **接受并完整处理了 16 MiB 的请求体**（HTTP 200）。
Drogon 的 `setClientMaxMemoryBodySize(1 MiB)` 只决定"超过就落临时文件"，并不构成上限；
而当时的 `setClientMaxBodySize` 是"最大文件大小 10 GiB + 1 MiB"，等于把预认证接口的内存与磁盘开销交给调用方决定。

| 项 | 处理 |
|---|---|
| 框架级上限 | 本项目**没有任何端点接收"整个文件"**——上传一律走分块（每块 ≤ `kMaxChunkSize` 64 MiB），合并发生在服务端。因此 `setClientMaxBodySize` 收紧到 **最大分块 + 1 MiB**，Drogon 在读取请求体之前就回 `413`（用原始 TCP 声明 11 GB `Content-Length` 验证过） |
| JSON 接口上限 | 新增 `api/json_body.h/.cpp`：JSON 请求体统一走 `ParseJsonBody`，上限 **256 KiB**；先看 `Content-Length`，再用实际体长兜底（chunked 编码没有 `Content-Length`）。超限 `413 PAYLOAD_TOO_LARGE` |
| 新错误码 | `PAYLOAD_TOO_LARGE`（HTTP 413）已加入 `docs/api.md` 错误码表与状态码映射 |
| 非法 UUID | 把非 UUID 文本传进 `folder_id` / `parent_id` 等字段时，PostgreSQL 抛 22P02（`invalid_text_representation`），原来被兜底成 `503 STORAGE_UNAVAILABLE`——**客户端错误被伪装成"服务不可用、可重试"**。现在 SQLSTATE 22xxx 统一映射为 `400 INVALID_ARGUMENT` |
| 去重 | 七个 repository 各自复制了一份 `DatabaseError`，已集中到 `infrastructure/database_error.h`（一处修复覆盖全部 112 个 `::uuid` 绑定点） |
| 测试 | 单测 `json_body_test.cpp` 9 项；集成测试 `FolderFlowTest.MalformedIdsAreClientErrorsNotServerErrors` |

**残余风险（记录，不在本阶段修）**：Drogon 在解析阶段按 `clientMaxBodySize` 拦截；
超过 `clientMaxMemoryBodySize` 的请求体会先落到 Drogon 自己选的临时文件（框架行为）。
JSON 接口的 256 KiB 上限在请求进入控制器之后才生效，所以单个请求最坏仍会占用"最大分块 + 1 MiB"的框架级开销，
这个上限已由上面的收紧固定住。

### 24.5 阶段十二验收：`e2e-phase12.ps1`（23/23 通过）

脚本在 `E:\DeepSeekProjects\_tmp\lan-file-hub\e2e-phase12.ps1`，按场景用不同阈值重启服务，全部走真实 HTTP：

| 场景 | 覆盖 | 结果 |
|---|---|---|
| A | 账号维度限流：3 次内 401、第 4 次 429 + `Retry-After=55`；被拒尝试不延后解锁；限流先于口令校验；同 IP 其他账号不受影响 | 8/8 |
| B | 登录成功后重置窗口：随后 3 次错误登录仍是 401，第 4 次再被限流 | 3/3 |
| C | IP 维度限流：同 IP 连续不同账号，第 3 个被 429 | 2/2 |
| D | 创建上传会话限流：第 1 次 200、第 2 次 429 + `Retry-After` | 4/4 |
| E | 2 MiB JSON → `413 PAYLOAD_TOO_LARGE`；服务未被打挂；11 GB `Content-Length` → 413；未认证创建会话 → 401 | 4/4 |
| F | 保留空间设为 160 GiB（本机可用 57.7 GiB）→ 创建会话 `507 STORAGE_FULL` | 1/1 |
| 日志 | 全部场景服务日志无 error/critical | 1/1 |

重跑方式：先 `.\scripts\build.ps1 -Preset debug`，确认 Docker 里的 PostgreSQL 与 Redis 在跑，
然后 `powershell -ExecutionPolicy Bypass -File E:\DeepSeekProjects\_tmp\lan-file-hub\e2e-phase12.ps1`。

### 24.6 本轮完成：下载限速（任务书第 4 条）

| 项 | 说明 |
|---|---|
| 配置 | `DOWNLOAD_RATE_LIMIT`（字节/秒），**默认 0 = 不限速**；`ByteRateLimiter(bytes_per_second, max_keys)` |
| 策略 | 字节级令牌桶，突发额度 1 秒；令牌不足时用 `runAfter` 定时器排队等待，**不 sleep**，不占工作线程 |
| 限速维度 | 已登录下载按用户（`download|<user_id>`，当前版本与历史版本共用一个桶，避免用历史版本绕过）；公开分享下载按来源 IP |
| 传输形式 | 启用限速时走异步 chunked 流（**没有 `Content-Length`**，Range 仍返回 206 + `Content-Range`）；默认不限速仍走零拷贝 `newFileResponse`，语义完全不变 |
| 取消传输 | 两层判据：连接存活状态 + 「连接 10 秒内没有字节写进内核」的停滞看门狗。**实测只信 `send()` 不够**：连接断开后它仍返回 true，服务端会把整个文件发完 |
| 测试 | 单测 `byte_rate_limiter_test.cpp` 9 项；真实 HTTP 见 24.5 场景 G/H |

**踩到的坑（记录以免重犯）**：`setClientMaxMemoryBodySize` 不是上限（见 24.4）；
trantor 的 `AsyncStream::send()` 在连接断开后仍返回 true，所以"客户端断开即取消"必须自己用连接状态与发送进度判断。

### 24.7 本轮完成：CORS 与安全响应头（任务书第 11、16 条）

复核发现 CORS **只有配置校验、没有任何响应头处理**：预检 `OPTIONS` 会落到未注册的 OPTIONS 路由上，跨域前端根本连不上。
现在 `api/security_headers.cpp` 统一处理：

| 项 | 说明 |
|---|---|
| 预检 | 路由**之前**应答 204 + `Allow-Methods` + `Allow-Headers`（含分块上传要用的 `X-Chunk-Sha256`）+ `Max-Age=600`；白名单外来源直接 403 |
| 来源判定 | 精确匹配（大小写不敏感、忽略尾部斜杠），配置层与代码层都拒绝 `*`；响应始终带 `Vary: Origin` |
| 暴露的响应头 | `Content-Disposition`、`Content-Range`、`Content-Length`、`Accept-Ranges`、`X-File-Version`、`Retry-After`、`X-Request-Id` |
| 安全头 | `X-Content-Type-Options: nosniff`、`X-Frame-Options: DENY`、`Referrer-Policy: no-referrer`、`X-XSS-Protection: 0`、`Permissions-Policy`、`Content-Security-Policy: default-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'` |
| HSTS | 仅在 `req->isOnSecureConnection()` 为真时下发：明文 HTTP 下发 HSTS 会把站点锁死 |
| 测试 | 单测 `security_headers_test.cpp` 5 项；E2E 场景 I 共 11 项 |

### 24.8 本轮完成：日志脱敏复核（任务书第 15 条）

复核发现三处问题，都已修掉（完整结论见 `docs/security.md` §7）：

| 发现 | 处理 |
|---|---|
| 脱敏是逐调用点 opt-in 的，漏一处就泄露 | spdlog **sink 层**加最后一道闸 `MakeRedactingSink`：写盘前用配置里的 secrets 替换（新增单测 4 项，含"调用点忘了脱敏也不会泄露"） |
| 新写的限速下载把**对象绝对路径**写进日志 | 改为记录对象 Key（内容寻址、不含用户路径），控制器传入 `storage_key` |
| 公开分享下载记录 `share_token_prefix`（Token 前 6 位） | 改为 `share_ref`＝Token 的 SHA-256 前 8 位，可关联不可反推 |
| 登录日志含 username/email/IP | **保留**：爆破排查必需的审计信息，且不含凭据（已在 `security.md` §7 显式记录该取舍） |

### 24.9 本轮完成：FileScanner 抽象与 NoopFileScanner（任务书第 17 条）

| 项 | 说明 |
|---|---|
| 接口 | `application/file_scanner.h`：`Scan(storage_key, content_hash)` 返回 `kClean` / `kInfected` / `kSkipped` |
| 本期实现 | `NoopFileScanner` 明确返回 **kSkipped，不是 kClean**——没有扫描能力就不能假装扫过 |
| 接入点 | `UploadService::ScanObject()`，在 `persist_content` 之前调用：`kInfected` → `422 FILE_REJECTED`，对象不被任何文件引用，交后台清理；扫描器报错 → **fail-closed**，不放行 |
| 测试 | 单测 `file_scanner_test.cpp` 3 项；集成测试 `UploadScanTest.InfectedContentIsRejectedAndNeverReferenced`（假扫描器判定带毒 → 上传失败、会话 FAILED、分块清理、对象零引用）与 `UploadScanUnavailableTest.ScannerFailureBlocksUploadInsteadOfSilentlyPassing` |
| 替换方式 | 在 `main.cpp` 换掉 `NoopFileScanner` 即可，业务层不用改；README「已知限制」继续写明没有真实查毒 |

### 24.2 任务书逐条盘点

| # | 条目 | 状态 |
|---|---|---|
| 5 | 请求体大小限制 | ✅ 阶段四 + 本轮收紧（24.4：框架上限"最大分块 + 1 MiB"，JSON 接口 256 KiB） |
| 6 | 单文件大小限制 | ✅ 阶段四/五（`MAX_FILE_SIZE`，会话内逐块累计检查） |
| 7 | 用户存储配额 | ✅ 阶段六（事务内强制，并发不可绕过） |
| 8 | 磁盘剩余空间检查 | ✅ 本轮（24.1） |
| 9 | 路径穿越防护 | ✅ 阶段三/四（Key 校验 + 解析后包含性检查 + 测试） |
| 10 | 文件名与 Content-Disposition | ✅ 阶段三/四（名称净化 + RFC 6266 编码） |
| 12 | Token 过期与错误处理 | ✅ 阶段二（`TOKEN_EXPIRED`，禁用用户复查） |
| 13 | 分享随机性与过期校验 | ✅ 阶段九（32 字节随机、统一 404） |
| 14 | SQL 参数化 | ✅ 全程（占位符由计数器生成，值一律绑定） |
| 3 | 分块上传并发限制 | ✅ 阶段五（全局 + 单用户并发会话上限） |
| 1、2 | 登录限流、上传限流 | ✅ 本轮（24.3，真实 HTTP 验证见 24.5） |
| 4 | 下载限速 | ✅ 本轮（24.6：字节级令牌桶、断开取消、默认不限速；场景 G/H 验证） |
| 11、15、16 | CORS 完善、日志脱敏复核、安全响应头 | ✅ 本轮（24.7、24.8；CORS 之前只有配置校验，没有任何响应头处理） |
| 17 | `FileScanner` 抽象与 `NoopFileScanner` | ✅ 本轮（24.9，已接进上传完成路径并有集成测试） |
| 18～21 | 非法请求/越权/限流/大请求测试 | ✅ 限流与"大请求被拒绝"见 24.4/24.5；非法 ID 错误码已归正；越权与非法请求测试散落各阶段，映射表见 `security.md` §10 |
| 22 | 更新安全文档 | ✅ 本轮（`security.md` §5–§10、`storage.md` 下载限速、`api.md` 错误码与请求体上限、README） |


## 25. 阶段十三实施记录（2026-09-12，完成）

阶段十三是**可观测性与管理员接口**。任务书 20 条全部有结论；后端测试 **284/284**、
前端 **111/111**、真实 HTTP 验收脚本 **77/77**（场景 A～J）。

### 25.1 指标与 `/metrics`（第 7、9～12 条）

| 项 | 说明 |
|---|---|
| 注册表 | `common/metrics.{h,cpp}`：计数器 / 仪表 / 直方图，Prometheus 文本格式（`text/plain; version=0.0.4`） |
| **标签基数有界**（硬要求） | 每个指标最多 200 条序列，超出丢弃并计入自暴露的 `lan_file_hub_metrics_dropped_series_total`；route 标签取 Drogon 的**路由模板**（实测 `route="/api/v1/files/{file_id}/download"`，原始路径计数为 0），未匹配记 `unmatched`，方法走白名单，错误码取封闭表 |
| HTTP 指标 | `http_requests_total{method,route,status}`、`http_request_duration_seconds`（13 个桶） |
| 业务指标 | `uploads_total{result}`、`upload_bytes_total`、`upload_chunk_retries_total`、`downloads_total{result,reason}`（原因映射到封闭集合）、`active_uploads`、`storage_used_bytes`、`storage_free_bytes`、`background_jobs_total{result}`、`errors_total{code}`、`health_status{dependency}` |
| 刷新时机 | 活跃上传每 tick；存储用量与可用空间启动后第一个 tick + 每 15 个 tick（实测启动 5 秒内可见） |
| 安全取舍 | `/metrics` 不做认证（Prometheus 常规做法），只暴露聚合数字；要隔离用反向代理或关闭 |

### 25.2 管理员 API（第 13～18 条）

| 端点 | 说明 |
|---|---|
| `GET /api/v1/admin/users` | 分页 + `search`（LIKE 通配符已转义）+ `sort_by` 白名单（未知字段返回 400，不静默回退）+ `status` 过滤 |
| `PATCH /api/v1/admin/users/{id}/status` | 启用/禁用（幂等）；**不能禁用自己**；禁用后该用户无法登录且旧 Token 立即失效（403 FORBIDDEN） |
| `PATCH /api/v1/admin/users/{id}/quota` | 改配额（0 拒绝） |
| `GET /api/v1/admin/storage` | 用户数/文件数/版本数/占用/可用/进行中会话 |
| `GET /api/v1/admin/audit-logs` | 按 actor/action/资源类型/资源 ID 过滤 + 分页 |

- 权限判定与审计留痕都在 `AdminService`（应用层），控制器只做参数与 JSON 映射；**响应不含 `password_hash`**（E2E 断言）。
- 管理员改状态/改配额写审计；**查询审计日志本身也留痕**（`admin_query_audit_logs`）。
- 测试：`AdminFlowTest` 11 项（普通用户在**每个**入口都被拒且状态不变、搜索通配符转义、禁用/启用与审计、不能禁用自己、配额、统计、审计自留痕、按 actor/资源过滤）。

### 25.3 前端系统管理页（第 19 条）

`pages/AdminPage.tsx`（用户管理 / 存储统计 / 审计日志三个标签页）+ `services/adminApi.ts` + `types/admin.ts`；
导航项 `ADMIN_NAV_ITEM` 只对管理员显示（`navItemsForRole`，纯函数已单测），路由 `/admin` 对非管理员显示 403 结果页。
前端隐藏只是界面问题，权限一律由后端判定。前端：Vitest 111/111、`tsc --noEmit` 通过、`npm run build` 成功。

### 25.4 结构化日志与 request_id（第 1～6 条）

- `request_id` 与 `X-Request-Id` 校验（长度 ≤ 64 + 字符白名单，非法则服务端生成 UUID）在阶段一已实现，本轮**复核并纳入验收**：合法值原样回带、超长与含空格的值被拒绝（服务端另生成 UUID）。
- 请求耗时、上传成功/失败/重试次数、下载次数与失败原因都由 25.1 的指标承担，并保留原有结构化日志行。
- 日志脱敏在阶段十二已加 sink 层最后一道闸（见 §24.8）。

### 25.5 遗留与外部事项

| # | 事项 | 处理 |
|---|---|---|
| 1 | **没有"创建第一个管理员"的入口** | E2E 里用 `psql` 直接提权完成验证；按任务书这属于**阶段十五**的"迁移与初始化管理员"，不阻塞本阶段 |
| 2 | `/metrics` 无认证 | 已记录取舍；生产用反向代理限制来源，或 `METRICS_ENABLED`（阶段十五随部署一起收口） |
| 3 | `background_jobs_total` 需要先有清理任务被处理才有序列 | Prometheus 计数器的正常行为，验收脚本已说明、不作为硬断言 |


## 26. 阶段十四实施记录（2026-09-12，完成）

阶段十四是**性能、并发与故障测试**。任务书 22 条全部有结论；实测数字与复现方式见
[`docs/performance.md`](performance.md)，README 增加了「测试环境与限制」一节。

### 26.1 微基准（第 19 条）

`backend/benchmarks/core_bench.cpp` + Google Benchmark（`vcpkg.json` 新增依赖），目标
`lan_file_hub_benchmarks`，固定 64 MiB 确定性数据集。release、3 次重复取中位数：

| 基准 | 中位耗时 | 吞吐 |
|---|---|---|
| SHA-256（上传完成时重算整文件） | 34.4 ms | 2.17 GiB/s |
| 对象存储写入（1 MiB 粒度 + 落盘） | 37.8 ms | 1.66 GiB/s |
| 对象存储读取（1 MiB 缓冲顺序读） | 82.0 ms | 780 MiB/s |

结论：单块 8 MiB 分块落盘约 5 ms、哈希约 4 ms，**磁盘与哈希都不是瓶颈**。
微基准目标在 sanitizer 构建下自动跳过（预编译库没有插桩，混链必然失败）。

### 26.2 HTTP 压测（第 6、7、8、9、14、20 条）

新增 `scripts/bench.ps1`（项目内脚本，**任何一项失败即非零退出码**），对已在运行的后端执行：
固定 64 MiB 数据集、8 并发、200 次 API 请求。实测（release 后端）：

| 指标 | 结果 |
|---|---|
| 上传吞吐 | 23.2 MiB/s（2.76 s / 64 MiB，8 个分块；受测试客户端每块起一个 curl 进程限制） |
| 下载吞吐 | 238.9 MiB/s（0.268 s / 64 MiB） |
| 并发下载 | 8/8 成功且 SHA-256 一致（1.13 s，7.1 请求/秒） |
| 并发上传 | 8/8 会话成功 |
| API 延迟（客户端观测） | P50 18.1 ms / P95 29.2 ms / P99 31.3 ms |
| API 延迟（服务端直方图，桶分辨率） | P50 ≈ 1 ms / P95 ≈ 25 ms / P99 ≈ 25 ms |
| 失败率 | 0/200 |
| 服务端内存 | 14.2 MB → 14.2 MB（上传 64 MiB 后）→ 14.5 MB，**流式路径没有把文件读进内存** |

脚本同时断言：重复分块的 `already_present` 幂等语义、客户端三次强断后服务仍健康。

### 26.3 端到端（第 5 条）

阶段十写下但**从未执行**的 Playwright 用例，本阶段真正跑起来：**2 passed（11.5 s）**
（核心流程 + 公开分享页），依赖 `npm i -D @playwright/test` 与 `npx playwright install chromium`。
首次执行暴露 4 个**用例自身**的问题（应用行为均正确，已修）：Ant Design 汉字按钮会插入空格、
注册表单漏填「确认密码」、文件名同时出现在上传面板与列表（断言需限定表格行）、分享弹窗有多个只读输入框。

### 26.4 故障注入（第 10～15 条）

新增 `FaultInjectionTest`（把依赖指向不可用地址，不去停容器，确定性可复现）：

| 场景 | 结论 |
|---|---|
| PostgreSQL 不可用 | 返回 `STORAGE_UNAVAILABLE`，不崩溃、不假装成功 |
| Redis 不可用 / 依赖恢复 | `/health/ready` 判未就绪、检查不抛异常；恢复后重新报 ok |
| 对象存储根目录缺失 | 判未就绪 |

其余故障场景由既有测试覆盖：磁盘不足（阶段十二验收场景 F）、客户端断线（场景 G）、
对象缺失（阶段十一一致性扫描）、服务重启恢复（`TrashTest.StartupRecoveryReschedulesPendingTasks`）。

### 26.5 Sanitizer（第 16～18 条）

- **AddressSanitizer 已构建并跑通：287/287 用例通过，全程无 ASan 报错**（`ctest --preset asan`，`ASAN_OPTIONS=detect_leaks=0`）。实现方式：`--preset asan` 使用独立 triplet（`cmake/triplets/x64-windows-asan.cmake`）
  把依赖装到 `vcpkg_installed_asan`，**不污染默认开发环境**（默认 triplet 与 `vcpkg_installed` 不变）。
- **ThreadSanitizer / UndefinedBehaviorSanitizer 未验证**：MSVC 不提供这两个 sanitizer，
  而依赖都是 MSVC 静态库，无法混用 clang 链接；要跑需要整套依赖用 clang 重编，本阶段未做。
  并发正确性目前由集成测试（行锁、幂等、唯一约束）与代码审查保证。

### 26.6 本阶段发现并修掉的问题

| 问题 | 处理 |
|---|---|
| **ASan 从未真正构建过**：sanitizer 选项只挂在严格警告接口上，测试/工具目标（用宽松接口）不带插桩，一链接就 `LNK2038 annotate_* 不匹配` | sanitizer 选项同时挂到两个接口；测试与基准都能正确编链 |
| **ASan 首次构建链接到旧依赖树**：`build/asan` 的 CMake 缓存里 `libpqxx_DIR` 指向默认 triplet，导致插桩目标与非插桩 `pqxx.lib` 混链 | 删除 `build/asan` 重新配置（依赖已装好，19 秒完成），链接行确认指向 `vcpkg_installed_asan` |
| **偶发测试**：`TrashTest.StartupRecoveryReschedulesPendingTasks` 曾在全量跑中失败——开发机上的服务端后台线程也在 `ClaimDue` 同一张 `cleanup_tasks` 表，和用例抢任务 | 断言改为按**自己的任务 identity**（先领干净别人的到期任务），不再依赖"一次领取到几条"；连跑 6 次通过 |
| Playwright 用例 4 处定位器问题 | 见 26.3，全部修正 |
| 压测脚本自身 3 个 bug：`Api()` 漏拼 `/api/v1`、路径表里混进根路径 `/health/live`、PowerShell 数值提升把 LCG 掩码算坏 | 全部修正后脚本退出码 0 |

### 26.7 未验证与限制

完整清单见 `docs/performance.md` §8 与 README「测试环境与限制」：全部测量在本机回环完成
（无真实局域网数据）、单实例、无长稳压测、P99 样本量有限、TSan/UBSan 未运行、
上传吞吐受测试客户端进程开销限制。


## 27. 阶段十五实施记录（2026-09-12）

阶段十五是**容器化、部署与数据备份**。任务书 21 条：19 条已落地并实测，2 条受限（见 27.5）。

### 27.1 镜像与编排（第 1～8 条）

| 项 | 落地情况 |
|---|---|
| 多阶段构建 | builder（Ubuntu 24.04 + vcpkg 源码编译）→ runtime（只带 `ca-certificates`/`curl`/`libpq5`），builder 不进最终镜像 |
| 镜像体积 | builder 阶段 `strip` 二进制；新增 `.dockerignore`（此前构建上下文会把本地 `build/`、`vcpkg_installed/`、`node_modules/` 一起送进 daemon） |
| 非 root | runtime `useradd --uid 10001 lanfilehub` + `USER lanfilehub`；对象目录 `chown` 给该用户 |
| 编排 | 5 个服务：`postgres`、`redis`、`migrate`、`backend`、`frontend`（另加 `init-admin` 走 profile） |
| 健康检查 | postgres `pg_isready`、redis `redis-cli ping`、backend 镜像内 `HEALTHCHECK /health/live`、frontend `wget` |
| 启动依赖 | backend `depends_on: postgres/redis(service_healthy) + migrate(service_completed_successfully)` |
| 数据卷 | `postgres_data`、`redis_data`、`storage_data`（对象存储）；数据库/Redis 端口只绑 `127.0.0.1` |
| 依赖裁剪 | `benchmark` 从必备依赖改成 vcpkg **可选 feature**（服务镜像不需要；它在 x64-linux 上编译失败过），本地 CMakePresets 默认启用该 feature |

### 27.2 迁移与管理员初始化（第 9、10 条）

- `scripts/migrate.ps1`（本地 `-InContainer` 走容器）；compose 的 `migrate` 服务做一次性迁移，
  成功退出后 backend 才启动——**应用进程不改 schema**。
- 镜像内同时带 `lan_file_hub_migrate` 与 `migrations/`，生产不必挂源码。
- 迁移工具新增 `--create-admin / --admin-email / --admin-password-env`：
  口令**只从环境变量读**（不进命令行历史、不进仓库）；未设置或过短直接退出码 1；
  已存在同名用户则提升为 ADMIN（不改口令），已是 ADMIN 时幂等跳过。
  实测：创建 `probeadmin` → 库里 `role=ADMIN`、`password_hash LIKE '$argon2id$%'`；重复执行提示"已是管理员"。

### 27.3 备份与恢复（第 11～13 条）

- `scripts/backup.ps1`：`pg_dump -Fc` + 对象存储归档 + `manifest.json`（大小/**SHA-256**/迁移版本/关键表行数）。
  对象存储按四级回退识别：显式卷 → `storage_data` 卷 → 运行中的 backend 容器 → **本地 `storage/` 目录**（本机开发）。
- `scripts/restore.ps1`：恢复前核对清单 SHA-256；**默认演练模式**恢复到临时库（不碰现有数据），
  `-Force` 才就地覆盖且**先自动备份当前状态**；对象存储先解到临时目录再替换。
- 实测：备份 → 演练恢复，临时库得到 11 张表 / 8 个迁移版本 / 行数与清单一致；
  归档内的标记文件被正确解出（`storage-staging/objects/zz/backup-marker.txt`）。

### 27.4 文档与生产配置（第 14～17、21 条）

`docs/deployment.md` 新增「阶段十五落地记录」（服务表、卷表、迁移与管理员命令、备份恢复用法与取舍、
nginx 反代与 HTTPS、生产安全清单、验证结果）；新增 `deploy/nginx.conf.example`（HTTP→HTTPS 跳转、
`client_max_body_size 128m`、`proxy_request_buffering off`、`X-Forwarded-*` 透传、`/metrics` 白名单）；
`.env.example` 补编排与初始化管理员的示例值（口令留空，仓库里不放真实口令）；README 补迁移/备份/恢复命令。

### 27.5 未验证与受限项（第 18～20 条）

| 项 | 状态 |
|---|---|
| `docker compose config` | ✅ 通过（5 服务 + 依赖条件） |
| 重启容器后数据仍在 | ✅ 实测：`docker compose restart postgres` 前后 `schema_migrations` 均 8 行 |
| 备份/恢复演练 | ✅ 见 27.3 |
| **删除并重建 backend 容器后文件仍在** | ⚠️ **未完成验证**：需要先构建后端镜像（在 Linux 容器内从源码编译整套 C++ 依赖）。构建过程中排掉三个环境问题：① 容器 DNS 解析不了外网 → 给 Dockerfile 加可选代理构建参数并经宿主代理构建；② `benchmark` 在 x64-linux 编译失败 → 改为可选 feature；③ `libpq` 需要 perl → builder 安装 `perl bison flex`。已在后台继续构建，**跑完之前这一条记为未验证** |
| 前端镜像构建 | 未执行（同样依赖容器内网络；前端构建本身在宿主机已通过 `npm run build`） |

## 28. 阶段十六实施记录（2026-09-12）

阶段十六是**发布准备**。任务书 28 条：25 条已落地，3 条受限（LICENSE、远程仓库、CI 实际执行）。

### 28.1 README 与文档（第 1～16、18～22、28 条）

| 项 | 落地情况 |
|---|---|
| 界面截图（第 17 条） | **10 张真实截图**（`docs/images/`）：登录、注册、我的文件、上传、回收站、分享给我的、系统管理（用户/审计）、公开分享、指标端点。由 `frontend/tests/e2e/screenshots.spec.ts` 用真实浏览器打开真实服务截取，**不是画的**；9 个截图用例通过 |
| 系统架构图（第 3 条） | README 新增分层 ASCII 图（HTTP 层 → 应用服务层 → 领域层 → PostgreSQL/Redis/ObjectStorage） |
| README 章节 | 简介、产品场景、核心功能、技术栈、架构、目录结构、快速启动（本地/Docker/前端）、迁移、默认管理员、API 示例、上传与断点续传、权限模型、异常恢复、测试方法、性能测试（指向 `docs/performance.md`，不编造数字）、安全说明、数据持久化、备份恢复、已知限制、路线图、License 章节 |
| 新增文件 | `CONTRIBUTING.md`、`SECURITY.md`（含私下报告渠道与"设计上的已知边界"）、`CHANGELOG.md`（0.1.0 + 未发布）、`docs/release-checklist.md`（6 大类检查项 + 最近一次执行记录） |
| CI（第 26 条） | 新增缺失的 `.github/workflows/frontend.yml`（类型检查 + lint + Vitest + 构建）；`backend.yml`、`docker.yml` 已存在并在本地跑过等价命令 |

### 28.2 仓库卫生（第 23～25 条）

- **首次 `git init`**（此前项目没有本地仓库），默认分支 `main`，首次提交 306 个文件、`.git` 约 5.6 MB。
- **发现并修掉两个忽略规则漏洞**：`/backups/`（含数据库转储与用户文件）与 `/vcpkg_installed_asan/`
  （阶段十四新增的 ASan 依赖树，曾误提交 1875 个文件）都没被忽略；`deploy/certs/`、`*.pem`、`*.key` 也一并加上。
- 敏感信息扫描：无硬编码密钥；命中的都来自测试夹具里的假值、未被跟踪的 `.env` 与 `.env.example` 的空值。
- TODO 扫描：没有未说明的 TODO/FIXME（命中的都是注释里的 `xxx` 占位说明）。
- 提交内最大文件 171 KB（`package-lock.json`）；截图合计约 300 KB，无大体积产物。

### 28.3 未完成项（需要用户决定或外部条件）

| # | 事项 | 原因 |
|---|---|---|
| 1 | **LICENSE** | 许可证类型未定（项目 `AGENTS.md` 待确认事项 #1）。按规则**不得擅自选择**，也不创建空许可证文件；未创建 |
| 2 | **GitHub 远程仓库创建与推送** | 全局 `AGENTS.md` 要求：对某账号**首次**创建远程仓库前必须确认；本地仓库与首次提交已就绪，随时可推 |
| 3 | **CI 实际执行** | 工作流已写好、等价命令已在本地全绿，但 GitHub Actions 只有推送后才能验证 |


## 29. 阶段十七实施记录（2026-09-12，完成）

阶段十七是**最终审查与验收**：以代码审查 + 发布验收的方式逐项检查 30 条，报告在
[`docs/final-review.md`](final-review.md)。结论摘要：

- **Critical 0、High 0（无未修复项）**；Medium 6 项、Low 5 项，均已给出文件位置、复现方式与建议。
- **25/30 项通过**，4 项部分通过（其中"镜像删容器重建"未跑完、"TSan/泄漏检测"受 MSVC 工具链限制），
  1 项待用户决定（LICENSE 类型）。
- 审查中**当场修掉**的问题：`docs/api.md` 缺 3 条路由（`/health/live`、`/health/ready`、
  `/api/v1/shared-with-me`），补齐后路由覆盖 **40/40**。

### 29.1 仓库已发布（阶段十六的两个待确认项已获批执行）

用户确认：不创建 LICENSE、可以推送 GitHub、CI 在推送中验证。执行结果：

| 项 | 结果 |
|---|---|
| 本地仓库 | `main` 分支，306 个跟踪文件，3 条提交 |
| 远程仓库 | `github.com/klaraljy/lan-file-hub`，**私有**，默认分支 `main`（`gh repo create --private`） |
| 推送 | `git push -u origin main` 成功，`main` 已跟踪 `origin/main` |
| CI（真实 GitHub Actions） | `docker` ✅ 成功、`frontend` ✅ 成功、`backend` 运行中（Linux 上编译整套 C++ 依赖） |

> 注：新仓库的**首次**推送不会触发工作流（GitHub 行为），用 `gh workflow run` 手动派发后三条都正常执行；
> 之后每次推送都会自动触发。

### 29.2 仍未验证（诚实清单）

1. 后端/前端镜像构建完成的"删容器重建后对象仍在"验证（本机构建进行中）；
2. TSan 与 LeakSanitizer（MSVC 不提供）；
3. 真实局域网链路性能与长稳压测；
4. 生产 HTTPS 端到端（只有 nginx 示例与配置说明）。