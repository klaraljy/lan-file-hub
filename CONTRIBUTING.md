# 贡献指南

感谢愿意花时间改进 lan-file-hub。这是一个**自用起步、按工业级标准做**的局域网文件服务项目，
所以对"能跑起来"和"能长期维护"同样看重。

## 开始之前

1. 读 `AGENTS.md`（项目规则与红线）与 `docs/architecture.md`（分层与并发模型）。
2. 读你要改的那部分对应的 `docs/` 文档；API 变更必须同步 `docs/api.md`。
3. 大改动先开 issue 说清动机与方案，避免做完才发现方向不一致。

## 环境

| 依赖 | 版本/位置 |
|---|---|
| 编译器 | MSVC 14.51（`D:\VS2026`）或 Linux 上的 GCC 13+ |
| 构建 | CMake ≥ 3.25 + Ninja + CMakePresets（`--preset debug/release/asan`） |
| 依赖管理 | vcpkg manifest 模式（`vcpkg.json`，triplet `x64-windows` / `x64-linux`） |
| 数据库 / 缓存 | PostgreSQL 16 与 Redis 7（`docker compose up -d postgres redis`） |
| 前端 | Node 20+、npm |

微基准需要额外开 feature：`vcpkg install --feature-flags=manifests --x-feature=benchmarks`
（本地 CMakePresets 已默认启用 `benchmarks`；服务镜像**不**启用，避免多编一个端口）。

## 构建与验证

```powershell
.\scripts\build.ps1 -Preset debug        # 构建
.\scripts\test.ps1  -Preset debug        # 单元 + 集成（需要 PostgreSQL/Redis）
.\scripts\format.ps1 -Check              # clang-format（提交前必须通过）
.\scripts\lint.ps1                       # clang-tidy
cd frontend; npm test; npm run lint; npx tsc --noEmit
```

- 集成测试打真实 PostgreSQL：设置 `LAN_FILE_HUB_REQUIRE_DB=1` 可让"跳过"变成失败（CI 用）。
- 性能相关改动请附 `.\scripts\bench.ps1` 的前后对比（脚本失败会返回非零退出码）。

## 代码约定

- C++ 遵循 Google C++ Style，以 `.clang-format` / `.clang-tidy` 为准；类型与函数 `PascalCase`，
  变量 `snake_case`，私有成员后缀 `_`，常量 `k` 前缀。
- 分层不能破：`HTTP Controller → Application Service → Domain → Repository / ObjectStorage / Redis`。
  Controller 不拼 SQL、不拼磁盘路径、不实现完整业务流程。
- 业务预期错误用 `Result<T>` / `Status`，不用异常；基础设施错误转统一错误类型。
- 禁止裸 `new` / `delete`；资源一律 RAII；后台线程必须可停止。
- 新增业务功能**必须**同步新增测试；新增 API 同步更新 `docs/api.md`。
- 不提交密钥、口令、Token、数据库转储与用户文件；`.env` 已被忽略，示例值只放 `.env.example`。

## 提交

- 提交信息：`feat:` / `fix:` / `test:` / `docs:` / `refactor:` / `build:` / `chore:` + 英文简述，
  一次提交只做一件事。
- 提交前自查：`git status` / `git diff` / `git diff --cached`，确认暂存内容属于本次改动、
  没有夹带无关修改或敏感文件。
- 不要为了通过风格检查做无关重构；也不要为了让测试通过而删除或跳过测试。

## 报告问题

- 功能缺陷：请附**可复现步骤**、期望行为、实际行为，以及后端日志里的 `request_id`
  （响应体与 `X-Request-Id` 头里都有，便于对账）。
- 安全问题：**不要**开公开 issue，按 `SECURITY.md` 的私下渠道报告。
