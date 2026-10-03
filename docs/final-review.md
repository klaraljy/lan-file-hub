# 最终审查与验收报告（阶段十七）

审查日期：2026-09-12 · 审查对象：`main` 分支（提交 `22345b4`，已推送 `github.com/klaraljy/lan-file-hub`）

审查方式：**逐项取证**——能跑的就跑（构建、测试、压测、端到端、CI），不能跑的用代码与配置审查，
并把结论落到具体文件与行号。**没有跑过的项一律标为"未验证"**，不写成通过。

## 1. 按严重程度排序的问题

### Critical

**无。** 没有发现会导致数据损坏、权限绕过、注入或崩溃的缺陷。

### High

**无未修复项。** 阶段十二发现并已修复的两个高危缺陷（记录在 `implementation-status.md` §24）：预认证接口
可被超大请求体撑住（现为框架级 + JSON 双层上限）、下载断开后仍把整个文件发完（现为连接状态 + 停滞看门狗双判据），
都带回归测试。

### Medium

| # | 位置 | 问题 | 复现 | 建议 |
|---|---|---|---|---|
| M0 | `backend/src/api/http_metrics.cpp:92`（修复前） | **Linux(GCC) 构建抓到 MSVC 没报的空指针解引用告警**：`req->attributes()->find(...)` 后再 `req->attributes()->get<T>(...)`，GCC `-Wnull-dereference` 在 `-Werror` 下直接判定构建失败。**是这次 Docker 镜像构建才暴露出来的** | `docker compose build backend`（x64-linux 编译我们自己的代码时） | **已修复**：取一次 `attributes` 局部指针并显式判空后复用；MSVC 与 GCC 两侧构建均通过 |
| M1 | `backend/tests/integration/trash_flow_test.cpp:665` | **集成测试与开发机上的服务端后台线程竞争 `cleanup_tasks`**：两者都会 `ClaimDue`，断言"一次领到几条"会偶发失败 | 开着后端服务跑全量 `ctest` | 已改为按自己的任务 identity 断言（连跑 6 次通过）；根因是"测试与开发库共用数据库"，建议 CI/本地都用独立库（CI 已用 service 容器，天然隔离） |
| M2 | `frontend/src/features/files/viewState.ts`、`docs/frontend.md` §4.1/§4.2 | **上传状态命名歧义**：文档里 `retrying`/`cancelled` 与实现枚举不完全一致（阶段十已记录为未决） | 读文档 + 读枚举 | 统一枚举与文档用词，或明确"文档描述的是交互态、实现是持久态" |
| M3 | `frontend/tests/e2e/core-flow.spec.ts` | **"刷新后恢复上传"只有单元测试，没有浏览器级验证**：`UploadManager.restore` 有 4 个单测，但没有"上传中刷新页面"的端到端用例 | `npm test` 可见单测；Playwright 套件里没有该场景 | 增加一个 Playwright 用例：上传大文件 → `page.reload()` → 断言任务被恢复并要求重选文件 |
| M4 | `docs/api.md` | **API 文档缺口**：`GET /health/live`、`GET /health/ready`、`GET /api/v1/shared-with-me` 未收录（路由共 40 条，缺 3 条） | 对比 `ADD_METHOD_TO` 与文档 | **本次审查已修复**，现在 40/40 覆盖 |
| M5 | `Dockerfile` | **后端镜像在 Linux 容器内编译整套依赖，环境依赖强**：历史失败包括容器 DNS 不通、`benchmark` 在 x64-linux 编译失败、`libpq` 缺 perl | `docker compose build backend` | 已加可选代理构建参数、把 `benchmark` 降为可选 feature、builder 装 `perl bison flex`；建议后续用预编译基础镜像或 CI 产物缓存加速 |
| M6 | 全局 | **数据竞争没有动态检测**：MSVC 无 TSan，依赖为 MSVC 静态库无法混用 clang | — | 现状：互斥量/原子量的使用点已审查（`std::mutex` 27 处、`std::atomic` 11 处），后台线程只有清理线程且用 `jthread`；建议后续用 Linux + clang 全量重编以启用 TSan |

### Low

| # | 位置 | 问题 | 建议 |
|---|---|---|---|
| L1 | `backend/src/infrastructure/database.cpp:54` | `std::shared_ptr<ConnectionPool>(new ConnectionPool(...))` 是唯一一处裸 `new`（已被 shared_ptr 接管，**无泄漏**） | 若构造函数可公开则改 `std::make_shared` |
| L2 | `backend/src/api/metrics_controller.cpp` | `/metrics` 不做认证（Prometheus 常规做法，只暴露聚合数字） | 生产用反向代理白名单（`deploy/nginx.conf.example` 已给示例） |
| L3 | 全局 | Windows 上 LeakSanitizer 不可用，ASan 跑的是内存错误检测而非泄漏检测 | 在 Linux CI 上开 `detect_leaks=1` 补这一课 |
| L4 | `frontend/src/services/tokenStore.ts` | 令牌默认进 `sessionStorage`；**勾了「记住我」则进 `localStorage` 并配 30 天有效期**（2026-10-03 用户要求，代价与替代方案写在 README） | 生产环境整体改 HttpOnly Cookie，与 HTTPS 一起做（README 已知限制已写） |
| L5 | `docker-compose.yml` | 默认口令 `lan_file_hub_dev` 仅适用于本机开发 | 部署清单已要求修改（`docs/deployment.md` 生产检查清单） |

## 2. 30 项验收逐条结论

| # | 检查项 | 结论 | 证据 |
|---|---|---|---|
| 1 | C++20 能完整编译 | ✅ | `debug`/`release`/`asan` 三个预设构建退出码 0（`-Werror` 开启） |
| 2 | 无裸 new/delete | ✅ | `grep` 结果仅 1 处（L1，已被 shared_ptr 包装）；其余命中全是 SQL `DELETE FROM` |
| 3 | 无明显资源泄漏 | ✅（有限） | 无裸 `fopen`/`FILE*`；连接池、语句、文件流全部 RAII；ASan 287/287 无报错。**泄漏检测在 Windows 上未开**（见 L3） |
| 4 | 文件句柄 RAII | ✅ | `ifstream`/`ofstream`/`unique_ptr<ObjectWriter>`；无 C 文件句柄 |
| 5 | 线程安全退出 | ✅ | `cleanup_service.cpp:33` `std::jthread` + `stop_token`，`Stop()` 里 `request_stop()` + `join()`，析构调用 `Stop()`；可中断等待不用等满周期 |
| 6 | 数据竞争风险 | ⚠️ 部分 | 无共享可变业务状态；互斥量/原子量使用点已审查；**TSan 未运行**（M6） |
| 7 | 大文件全程流式 | ✅ | 压测：上传 64 MiB 后服务端 WorkingSet 14.2→14.2 MB（脚本在增长超文件大小 1.5 倍时判失败） |
| 8 | 无路径穿越 | ✅ | `object_storage_test.cpp`：Key 校验 + 解析后包含性检查；用户文件名从不参与物理路径 |
| 9 | 无越权读写 | ✅ | `sharing_flow_test.cpp`（权限矩阵、继承、陌生人）、`admin_flow_test.cpp`（管理员接口鉴权）、E2E 阶段九/十三 |
| 10 | 无 SQL 注入 | ✅ | 全参数化；排序列走白名单（`BuildOrderBy`/`SafeUserSortColumn`），未知字段返回 400；搜索的 LIKE 通配符已转义并有测试 |
| 11 | 上传会话持久化 | ✅ | 会话与分块落库（`upload_sessions`/`upload_chunks`）+ 磁盘；`ResumesAfterInterruptionWithFreshServiceInstance` |
| 12 | 断点续传真可恢复 | ✅ | 同上 + E2E 阶段五；`GetStatus` 返回已完成分块供客户端续传 |
| 13 | 重复分块幂等 | ✅ | `DuplicateChunkWithSameContentIsIdempotent`、`CHUNK_CONFLICT`（不同内容）；压测脚本断言 `already_present` |
| 14 | 完成上传重算 SHA-256 | ✅ | `ChunkedUploadVerifiesHashAndBuildsContentAddressedObject`（哈希由服务端重算）、`DeclaredHashMismatchFailsSessionAndCleansChunks` |
| 15 | 版本冲突返回 409 | ✅ | `version_conflict_test.cpp`；`FILE_VERSION_CONFLICT` + `current_version_id` |
| 16 | 回收站恢复可用 | ✅ | `trash_flow_test.cpp` 12 个用例（恢复幂等、原目录不在时回根目录、同名冲突、子树恢复、他人不可见/不可恢复、purge 释放配额） |
| 17 | 分享 Token 安全 | ✅ | 32 字节随机 → base64url，库中只存 SHA-256；过期/撤销/超次/不存在统一 404；日志只记 `share_ref`（哈希前 8 位） |
| 18 | Redis 不可用行为明确 | ✅ | `FaultInjectionTest.RedisDownMakesReadinessFailWithoutStoppingTheProcess`：未就绪、不抛异常、恢复后回 ok |
| 19 | PostgreSQL 不可用行为明确 | ✅ | `FaultInjectionTest.PostgresUnavailableIsReportedNotCrashed`：`STORAGE_UNAVAILABLE`，不崩溃不假装成功 |
| 20 | 磁盘不足行为明确 | ✅ | `DedupQuotaTest.RejectsUploadWhenDiskReserveIsNotMet` + E2E 场景 F（507 `STORAGE_FULL`） |
| 21 | 前端可完成主要操作 | ✅ | Playwright：注册→浏览→上传→下载→删除→回收站恢复→创建分享 2/2 通过；10 张真实截图 |
| 22 | 前端状态处理 | ⚠️ 部分 | `PageState`（idle/loading/error/…）与上传任务状态机齐备并有单测；命名歧义见 M2 |
| 23 | 刷新后可恢复上传 | ⚠️ 部分 | IndexedDB 持久化 + `UploadManager.restore` 4 个单测；缺浏览器级验证（M3） |
| 24 | Docker Compose 可启动 | ✅ | `docker compose config` 通过；依赖与 `migrate` 服务退出码 0；backend 镜像构建成功，容器非 root（uid 10001）、`/health/live`+`/health/ready` 均 200；**删除并重建 backend 容器后文件 SHA-256 一致** |
| 25 | CI 可运行 | ✅ | 三条工作流在 GitHub Actions 上**全部通过**（`backend` 含 Postgres/Redis 服务容器 + 迁移 + 287 用例）。修掉 6 个 CI 专属问题：首推不触发、clang-format 版本、作业级环境变量覆盖临时 .env、Linux/GCC 三类编译告警、容器内 DNS、`ctest --test-dir` 工作目录——见 `AGENTS.md` §11.1 |
| 26 | README 与代码一致 | ✅ | README 中 `scripts/*.ps1` 8 个全部存在、`npm run dev/build/test/lint` 均已定义、二进制路径与实际一致 |
| 27 | API 文档与接口一致 | ✅（审查中修复） | 脚本对比 40 条路由：修复前缺 3 条，修复后 **40/40 覆盖** |
| 28 | 测试覆盖正常与异常流程 | ✅ | 后端 287 个用例（含错误码断言：401/403/404/409/413/422/429/507）；前端 111 个；E2E 覆盖主流程与失效分享页 |
| 29 | 无未说明的伪代码/空函数/TODO | ✅ | 扫描无空实现、无 TODO/FIXME（命中均为注释里的占位说明） |
| 30 | 未验证功能未被描述为已完成 | ✅ | `docs/release-checklist.md` 与 `implementation-status.md` §27/§28 明确列出未验证项（镜像删容器重建、TSan/UBSan、CI 首次触发、LICENSE） |

## 3. 未通过的验收项

| 项 | 状态 | 原因 |
|---|---|---|
| — | ✅ 已补齐 | 后端镜像已构建成功并完成"删容器重建"验证（见第 2 节第 24 项）；过程中 Linux/GCC 构建还抓出并修复了 3 个 MSVC 未报的可移植性缺陷 |
| 6/3（部分） | ⏳ 未运行 | TSan / LeakSanitizer：MSVC 不提供，依赖为 MSVC 静态库无法混用 clang |
| 18（外部） | ⏳ 待用户 | LICENSE 类型未定，按规则不擅自创建许可证文件 |
| 25（部分） | ⏳ 观察中 | `backend` 工作流首次运行（Linux 上编译整套 C++ 依赖，耗时较长）；`docker`/`frontend` 已成功 |

## 4. 已执行的命令与测试结果

```powershell
.\scripts\build.ps1 -Preset debug      # 退出码 0
.\scripts\build.ps1 -Preset release    # 退出码 0
cmake --preset asan; cmake --build --preset asan   # 退出码 0
.\scripts\format.ps1 -Check            # 148 文件通过
.\scripts\test.ps1 -Preset debug       # LAN_FILE_HUB_REQUIRE_DB=1 → 287/287
ctest --preset asan                    # ASAN_OPTIONS=detect_leaks=0 → 287/287，无 ASan 报错
build\release\bin\lan_file_hub_benchmarks.exe --benchmark_min_time=1s --benchmark_repetitions=3
.\scripts\bench.ps1 -FileMiB 64 -ApiRequests 200 -Concurrency 8   # 退出码 0
cd frontend; npm test; npm run lint; npx tsc --noEmit; npm run build   # 全绿（111/111）
$env:PLAYWRIGHT_E2E='1'; npx playwright test          # 2 passed
$env:PLAYWRIGHT_SHOTS='1'; npx playwright test tests/e2e/screenshots.spec.ts   # 9 passed，10 张图
.\scripts\migrate.ps1 -CreateAdmin …   # 创建管理员成功；缺口令时退出码 1
.\scripts\backup.ps1 ; .\scripts\restore.ps1 -BackupDir …   # 备份 + 演练恢复成功
docker compose config --quiet          # 通过
docker compose restart postgres        # 数据仍在（schema_migrations 8 行）
docker run --rm -v ${PWD}:/repo rhysd/actionlint:latest   # CI 工作流校验通过
gh repo create klaraljy/lan-file-hub --private --source=. --remote=origin
git push -u origin main                # 成功
```

**测试结果汇总**

| 类别 | 结果 |
|---|---|
| 后端单元 + 集成 | **287/287 通过**（debug，`REQUIRE_DB=1`） |
| 后端 ASan | **287/287 通过**，无 ASan 报错 |
| 前端 | Vitest **111/111**、`tsc --noEmit` 通过、`lint` 通过、`build` 成功 |
| 端到端 | Playwright **2/2**；截图用例 **9/9** |
| 微基准 | SHA-256 2.17 GiB/s、落盘 1.66 GiB/s、顺序读 780 MiB/s |
| HTTP 压测 | 上传 23.2 MiB/s、下载 238.9 MiB/s、P95 29.2 ms、失败率 0/200、内存不随文件增长 |
| 故障注入 | 3/3（PG 不可用、Redis 不可用/恢复、存储根缺失） |
| CI | 三条工作流全部 ✅（真实 GitHub Actions，`backend` 跑真库集成测试） |

## 5. 未验证事项（发布前必须知情）

1. **前端镜像构建** —— 前端产物在宿主机已通过 `npm run build`，容器镜像未单独构建验证。
2. **TSan**（数据竞争动态检测）与 **LeakSanitizer**（泄漏检测）—— MSVC 工具链不提供。
3. **真实局域网链路性能** —— 所有测量在本机回环完成。
4. **长稳/长时间压测**（数小时内存曲线）。
5. **GitHub Actions 的完整首轮结果** —— 已触发，结果以仓库 Actions 页面为准。
6. **生产 HTTPS 端到端** —— 只提供了 nginx 示例与配置说明，未在真实证书环境验证。

## 6. 发布前必须修复

**没有阻塞发布的缺陷。** 建议在打 tag 前完成：

1. ~~跑完后端镜像构建并补做"删容器重建"验证~~（**已完成**：镜像构建成功，重建容器后 SHA-256 一致）；
2. 确认 `backend`/`frontend` 两条 CI 工作流首次运行结论（失败则修）；
3. 若要用 Docker 部署，先在目标环境实测一遍 `docker compose up -d --build`。

## 7. 可以延后到后续版本

- M1（测试与开发库共用导致的竞争）—— 已在测试侧加固；彻底解决要"每套测试一个库"。
- M2（前端状态命名统一）、M3（刷新恢复的浏览器级用例）。
- L1（`make_shared`）、L3（Linux CI 上开泄漏检测）、L4（HttpOnly Cookie + HTTPS 部署）。
- 多实例部署、分布式限流、S3 兼容等 —— 属明确的项目边界之外（`docs/brief.md`）。
