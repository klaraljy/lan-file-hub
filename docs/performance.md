# 性能与故障测试

阶段十四的实测记录。**所有数字都是本机真实跑出来的**，命令、参数与数据集都写在下面，可复现；
没能验证的项目在文末「未验证与限制」里如实列出，不编造。

## 1. 测试环境

| 项 | 值 |
|---|---|
| 机器 | 12 逻辑核 @ 2096 MHz，L3 4 MiB×2，Windows 11 (10.0.22631) |
| 编译器 | MSVC 14.51（`D:\VS2026`），Ninja，CMake 3.31.6 |
| 构建类型 | 性能数据用 `release`；正确性测试用 `debug`；`asan` 见 §7 |
| 数据库 / 缓存 | PostgreSQL 与 Redis 跑在 Docker Desktop（`lan-file-hub-postgres` / `lan-file-hub-redis`） |
| 存储 | 本地文件系统（`STORAGE_ROOT`），E 盘 SSD |
| 数据集 | **固定 LCG 生成**：同样的参数产出同样的字节，跨机器/跨提交可比 |
| 网络 | 全部本机回环（127.0.0.1），不含真实局域网链路 |

复现命令：

```powershell
.\scripts\build.ps1 -Preset release
.\build\release\bin\lan_file_hub_server.exe           # 后端（另开窗口）
.\scripts\bench.ps1 -FileMiB 64 -ApiRequests 200 -Concurrency 8
```

## 2. 微基准（Google Benchmark）

64 MiB 固定数据集，`--benchmark_min_time=1s --benchmark_repetitions=3 --benchmark_report_aggregates_only=true`，
release 构建，取 3 次重复的**中位数**：

| 基准 | 中位耗时 | 吞吐 | 说明 |
|---|---|---|---|
| `BM_Sha256_64MiB` | 34.4 ms | **2.17 GiB/s** | 上传完成时要重算整个文件的 SHA-256，这是主要 CPU 成本 |
| `BM_ObjectStorageWrite_64MiB` | 37.8 ms | **1.66 GiB/s** | 按 1 MiB 写入（与分块落盘粒度接近），含 `Finish()` 落盘 |
| `BM_ObjectStorageRead_64MiB` | 82.0 ms | **780 MiB/s** | 1 MiB 缓冲顺序读，不含网络发送 |

读比写慢是正常的（写有页缓存兜底，读要走完整路径）。**这两个数字说明磁盘不是瓶颈**：
单块 8 MiB 分块落盘约 5 ms，哈希约 4 ms。

目标 `lan_file_hub_benchmarks` 只在装了 Google Benchmark 且**不是** sanitizer 构建时生成
（sanitizer 构建下预编译库没有插桩，混链会报 `LNK2038 annotate_* 不匹配`）。

## 3. HTTP 压测（`scripts/bench.ps1`）

release 后端 + 64 MiB 文件 + 8 并发 + 200 次 API 请求（`-WriteJson` 会落一份 JSON）：

| 指标 | 结果 | 说明 |
|---|---|---|
| 上传吞吐 | **23.2 MiB/s**（2.76 s / 64 MiB，8 个 8 MiB 分块） | 客户端每个分块起一个 `curl` 进程，进程启动开销占了相当比例；服务端侧受 §2 的数字约束（哈希 2.17 GiB/s、落盘 1.66 GiB/s），说明瓶颈在测试客户端而非服务端 |
| 下载吞吐 | **238.9 MiB/s**（0.268 s / 64 MiB） | 走零拷贝 `sendfile` 路径，回环链路 |
| 并发下载 | 8 路全部成功且 SHA-256 一致，1.13 s（**7.1 请求/秒**） | 同时验证内容不被并发读坏 |
| 并发上传 | 8 路会话全部成功 | 独立会话，互不干扰 |
| API 延迟（客户端观测） | **P50 18.1 ms / P95 29.2 ms / P99 31.3 ms** | 200 次请求，覆盖 `/auth/me`、文件列表、`/folders/root`、回收站 |
| API 延迟（服务端直方图，桶分辨率） | P50 ≈ 1 ms / P95 ≈ 25 ms / P99 ≈ 25 ms | 从 `/metrics` 的桶里估算，只能看量级；与客户端观测的差值主要是 HTTP 往返与客户端进程开销 |
| 失败率 | **0/200** | 任何失败都会让脚本以非零退出码结束 |
| 服务端内存（WorkingSet） | 14.2 MB → 14.2 MB（上传 64 MiB 后）→ 14.5 MB | **证明流式路径没有把文件读进内存**；脚本会在增长超过文件大小 1.5 倍时判失败 |

脚本同时覆盖：重复分块（断言 `already_present` 幂等语义）、客户端中途断线（3 次强断后服务仍健康、后续下载正常）。

## 4. 端到端（Playwright）

阶段十写下但**从未执行**的用例，本阶段真正跑通：

```powershell
cd frontend
npm i -D @playwright/test; npx playwright install chromium
# PostgreSQL/Redis 在跑；后端在 8080；npm run dev 在 5173
$env:PLAYWRIGHT_E2E='1'; npx playwright test
```

结果：**2 passed（11.5 s）**——「注册 → 浏览目录 → 上传 → 下载 → 删除 → 回收站恢复 → 创建分享」与「公开分享页不需要登录即可打开」。

首次执行暴露了 4 个用例自身的问题（应用行为都是对的，已修）：
1. Ant Design 会给**两个汉字**的按钮插入空格（`注册` → `注 册`），按钮名匹配必须容忍；
2. 注册表单有「确认密码」字段，用例漏填；
3. 文件名会同时出现在上传任务面板与列表里，断言必须限定到**表格行**；
4. 分享弹窗里有多个只读输入框，断言要逐个取值而不是直接定位单个元素。

## 5. 故障注入

`FaultInjectionTest`（不靠停容器，而是把依赖指向不可用地址，确定性且可复现）：

| 场景 | 期望 | 结果 |
|---|---|---|
| PostgreSQL 不可用 | 返回 `STORAGE_UNAVAILABLE`，不崩溃、不假装成功 | ✅ |
| Redis 不可用 | `/health/ready` 判未就绪；检查本身不抛异常；依赖恢复后重新报 ok | ✅ |
| 对象存储根目录缺失 | 判未就绪 | ✅ |
| 磁盘空间不足 | `507 STORAGE_FULL`（保留空间不可满足） | ✅ 阶段十二验收场景 F |
| 客户端中途断线 | 停止传输；限速下载有中断留痕 | ✅ 阶段十二验收场景 G |
| 对象文件缺失 | 一致性扫描区分「可删的孤儿」与「只告警的缺失对象」 | ✅ 阶段十一 §23.1d |
| 服务重启恢复 | 启动时重置 PENDING 清理任务并重新入队 | ✅ 阶段十一 §23.1f |

## 6. 并发与一致性

| 场景 | 结论 | 位置 |
|---|---|---|
| 并发完成同一上传 | 只创建一个文件（事务 + 行锁） | `UploadFlowTest.ConcurrentCompleteCreatesExactlyOneFile` |
| 并发上传绕过配额 | 不能绕过（事务内强制） | `DedupQuotaTest.ConcurrentUploadsCannotBothBypassQuota` |
| 重复分块 | 同内容幂等、不同内容 `CHUNK_CONFLICT` | `UploadFlowTest.*` |
| 并发下载 | 8 路并发内容一致（HTTP 层） | `scripts/bench.ps1` |
| 大文件流式 | 内存不随文件增长（64 MiB 上传后 WorkingSet 不变） | `scripts/bench.ps1` + `FileFlowTest.StreamingInChunksProducesSameHashAsOneShot` |

## 7. Sanitizer

| 构建 | 状态 | 说明 |
|---|---|---|
| AddressSanitizer | **已构建并跑通：287/287 通过** | MSVC 的 ASan 要求**所有参与链接的目标都带插桩**，否则报 `LNK2038 annotate_* 不匹配`。因此 `asan` 预设用独立 triplet（`cmake/triplets/x64-windows-asan.cmake`）把依赖装到 `vcpkg_installed_asan`（27 个端口，首次配置约 28 分钟），**不污染默认开发环境**；微基准目标在 sanitizer 构建下自动跳过。运行方式：`cmake --preset asan && cmake --build --preset asan && $env:ASAN_OPTIONS="detect_leaks=0"; ctest --preset asan`（Windows 上 LeakSanitizer 支持有限，故关掉泄漏检测） |
| ThreadSanitizer | **未验证** | MSVC 没有 TSan（只有 clang/gcc 有），而本项目依赖（Drogon/OpenSSL/libpqxx）都是 MSVC 版静态库，无法混用 clang 链接。要跑 TSan 需要整套依赖用 clang 重编，本阶段未做 |
| UndefinedBehaviorSanitizer | **未验证** | 同上：MSVC 无 UBSan（`/fsanitize=undefined` 不被识别），需要 clang 工具链 |

ASan 实测结果：**287/287 用例通过，全程无 ASan 报错**（`implementation-status.md` §26.5）。

## 8. 未验证与限制

- **TSan / UBSan 未运行**：原因见 §7，属工具链限制，不是「测过没问题」。并发正确性目前靠集成测试（行锁、幂等、唯一约束）与代码审查保证。
- **没有真实局域网链路数据**：全部测量在回环上完成，真实千兆/无线网络下的吞吐会受链路与磁盘限制，本表不代表局域网表现。
- **上传吞吐受测试客户端限制**：每个分块一个 `curl` 进程，测的是「PowerShell 脚本 + curl」的组合；服务端侧上限参考 §2 的微基准。
- **单机单实例**：没有多实例/负载均衡场景，也没有长稳（数小时）压测与内存泄漏曲线。
- **P99 样本量有限**：200 次请求的 P99 只有约 2 个样本，只适合看量级，不能当作统计结论。
- Redis 目前只用于健康检查与临时状态，没有在压测链路上，因此没有 Redis 相关吞吐数字。
