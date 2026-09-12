# 发布检查清单（阶段十六第 28 条）

每次发布前逐项打勾，并把**实际输出**附在下面。未验证的项必须标注"未验证"，
不得因为"应该没问题"就勾上。

## 1. 构建与测试

- [ ] `.\scripts\build.ps1 -Preset release` 成功
- [ ] `.\scripts\build.ps1 -Preset debug` 成功
- [ ] `.\scripts\test.ps1 -Preset debug`（`LAN_FILE_HUB_REQUIRE_DB=1`）全绿
- [ ] `.\scripts\format.ps1 -Check` 通过
- [ ] `.\scripts\lint.ps1` 通过（或已记录豁免项）
- [ ] `cmake --preset asan && cmake --build --preset asan && ctest --preset asan` 全绿
- [ ] 前端：`npm test`、`npm run lint`、`npx tsc --noEmit`、`npm run build` 全绿
- [ ] Playwright：`$env:PLAYWRIGHT_E2E='1'; npx playwright test` 通过

## 2. 性能与故障

- [ ] `build\release\bin\lan_file_hub_benchmarks.exe` 记录结果
- [ ] `.\scripts\bench.ps1` 退出码 0，并把数字填进 `docs/performance.md`
- [ ] 故障注入用例通过（PostgreSQL/Redis/存储不可用、磁盘不足、断线、对象缺失）
- [ ] 未验证项（TSan/UBSan、真实局域网、长稳）在文档里标注

## 3. 部署与数据

- [ ] `docker compose config` 通过
- [ ] 全新环境可启动：`docker compose up -d --build`
- [ ] 重启 `postgres` / `redis` 容器后数据仍在
- [x] 删除并重建 `backend` 容器后对象文件仍在（2026-09-12 实测通过）
- [ ] `.\scripts\migrate.ps1` 可重复执行（幂等）
- [ ] `init-admin` 能创建管理员，且口令只从环境变量读
- [ ] `.\scripts\backup.ps1` → `.\scripts\restore.ps1` 演练恢复成功且行数一致

## 4. 安全

- [ ] 仓库内没有密钥/口令/Token（`git grep -iE 'password|secret|token'` 逐条确认）
- [ ] `.env` 未被提交，`.env.example` 只有示例值
- [ ] 生产检查清单（`docs/deployment.md`）逐项确认
- [ ] 管理员接口非管理员返回 403，响应不含 `password_hash`
- [ ] `/metrics` 在部署里有限制来源

## 5. 文档

- [ ] `README.md` 的每个命令都在当前结构下真实可执行
- [ ] `docs/` 无失效内部链接
- [ ] `docs/implementation-status.md` 记录了本阶段结论
- [ ] `CHANGELOG.md` 已更新
- [ ] 截图来自真实运行（`docs/images/`，由 `frontend/tests/e2e/screenshots.spec.ts` 生成）
- [ ] LICENSE：**待用户确定类型后创建**（未确定前不创建空许可证文件）

## 6. 产物

- [ ] 版本号已更新（`vcpkg.json` / 前端 `package.json` / CHANGELOG）
- [ ] Release 说明包含：变更摘要、升级步骤、已知限制、校验和
- [ ] 大体积产物（镜像/二进制）不直接提交进仓库

## 最近一次执行记录

**日期**：2026-09-12（阶段十五收口时）

| 项 | 结果 |
|---|---|
| 后端 debug 构建 + 测试 | ✅ 287/287 通过 |
| 格式检查 | ✅ 148 文件通过 |
| ASan 构建 + 测试 | ✅ 287/287 通过 |
| 前端 | ✅ Vitest 111/111、`tsc --noEmit`、`lint`、`build` 全通过 |
| Playwright 端到端 | ✅ 2/2 通过；截图 9 个用例通过，10 张图 |
| 微基准 / HTTP 压测 | ✅ 见 `docs/performance.md` |
| compose 校验 / 重启持久化 / 备份恢复演练 | ✅ 见 `docs/deployment.md`「阶段十五落地记录」 |
| 后端镜像构建 + 删容器重建验证 | ✅ `lan-file-hub-backend:local` 构建成功；重建容器后文件 SHA-256 一致 |
| GitHub 仓库创建与推送 | ⏳ 需要用户确认（首次在该账号创建远程仓库） |

**未完成项**：

1. **前端镜像构建**：前端产物在宿主机已通过 `npm run build`；容器镜像（nginx 托管 dist）未单独构建验证。
   （后端镜像已构建成功并完成删容器重建验证：见上表。）
2. **LICENSE**：类型未定（项目 `AGENTS.md` 待确认事项 #1），未创建许可证文件。
3. **CI 实际执行**：工作流已写好并在本地逐条跑过等价命令，但 GitHub Actions 只有在推送后才能验证。
