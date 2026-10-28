# 规划与精简说明

> 状态：草案 ｜ 最后更新：2026-09-12

本文记录本次对原始 `AGENTS.md`（2551 行 / 53KB）做的分流与精简：为什么做、改了什么、
每部分内容搬到了哪里、哪些冲突被裁决、开工前还缺什么。

**原始文件没有被修改，仍在 `E:\DeepSeek项目分析\lan-file-hub\Project Requirements\AGENTS.md`。**
本目录的文档是从它重组而来，不是替代品——重组过程中没有新增需求。

---

## 1. 为什么要精简

原文件同时兼任七种文档：产品文档、架构设计、数据字典、API 规格、编码规范、任务清单、验收标准。
问题不在于它长，而在于**定位错了**：

- `AGENTS.md` 每次会话都会全文注入上下文。全局那份 `E:\dsh\home\AGENTS.md` 只有 11.7KB，原文件 53KB。
- DSH 的指令预算约 64KB，**超限时优先丢弃最宽泛的文件（即全局 AGENTS.md）**。
  也就是说，53KB 的项目文件会把 Git 流程、临时目录约定、工具位置、密钥规则、BOM 规则挤掉——
  这些恰恰是最不能丢的。这是最实际的危害。

所以做法不是删内容，而是**分流**：`AGENTS.md` 只留"每次都必须知道"的规则与命令，
字段级模型、接口细节、协议流程、测试清单下沉到 `docs/`，按需查阅。

## 2. 精简结果

| | 原文件 | 现在 |
|---|---|---|
| 根 `AGENTS.md` | 53 130 字节 / 2551 行 | 12 587 字节 / 137 行（**减少 76%**） |
| 设计细节 | 混在同一份文件里 | `docs/` 下 12 个文档（含本文）+ `docs/phases/` 18 个阶段任务书 |
| 阶段定义 | 文件内 §39 自有一套九阶段，与任务书冲突 | 只保留指针，编号以 `docs/phases/` 为唯一权威 |

## 3. 原文件的病灶（逐条）

1. **整节重复**：§26 配置管理完整出现两遍（1589–1616 与 1618–1655 行），后一版多一个
   `CORS_ALLOWED_ORIGINS`。重组时以后一版为准。
2. **元叙述混进正文**：第 1 行"我先查看当前工作区的项目结构…"是生成时的开场白，不是规范。
3. **与全局规则重复**：§19.5（Google C++ Style）、§40（Git 检查流程）、§25 部分密钥条款、
   `.gitignore` 原则——全局已有。写两份必然漂移，且白花上下文。
4. **阶段编号打架**：§39 定义"阶段一～九"，而实际任务书是"阶段零～十七"，内容也对不上。
   两套编号同时存在，执行者不知道听谁的。
5. **未定项写成"或"**：Drogon 或 Boost.Asio、Conan 或 vcpkg、libpqxx 或选定客户端、
   JWT 或 Session、Ant Design 或统一组件库、Axios 或 fetch、Argon2id 或 bcrypt——
   阶段一开工即卡。本次已裁决了其中大部分，剩余见 §10。
6. **缺全局硬性要求**：没有"待确认事项"、没有可重复执行的验证命令、脚本排的是 `dev.sh`/`test.sh`
   而实际是 Windows 环境。
7. **环境事实未反映**：`cmake` 与 clang 系不在 PATH、本机没有 vcpkg/conan、WSL 无发行版、
   Docker 守护进程需先启动。原文件按"什么都有"写的。
8. **没有状态落脚点**：阶段零要求产出 `docs/implementation-status.md`，但原文件的结构里没给它位置。

## 4. 内容去向映射

重组后每一节都有明确归属，没有内容被丢弃：

| 原 § | 标题 | 现在在哪 |
|---|---|---|
| 1 | 项目名称 | `AGENTS.md` §1、`docs/brief.md` |
| 2 | 产品价值 | `docs/brief.md` |
| 3 | 产品边界 | `AGENTS.md` §1、`docs/brief.md` |
| 4 | 产品形态 | `docs/architecture.md` |
| 5 | 技术栈 | `AGENTS.md` §2（决策）、`docs/architecture.md`（完整清单） |
| 6 | 推荐项目结构 | `AGENTS.md` §3、`docs/architecture.md` |
| 7 | 后端分层约束 | `AGENTS.md` §5（红线）、`docs/architecture.md`（各层职责） |
| 8 | 核心领域模型 | `docs/data-model.md`；§8.5/§8.6 另见 `docs/permissions.md` |
| 9 | 数据库设计要求 | `docs/data-model.md` |
| 10 | 文件存储设计 | `docs/storage.md` |
| 11 | 分块上传协议 | `docs/storage.md` |
| 12 | 下载设计 | `docs/storage.md`；限速策略见 `docs/security.md` |
| 13 | 文件夹功能 | `docs/data-model.md`（命名与移动规则）；端点见 `docs/api.md` |
| 14 | 权限模型 | `docs/permissions.md` |
| 15 | 版本和并发修改 | `docs/data-model.md`；409 与 `If-Match` 见 `docs/api.md` |
| 16 | 回收站和删除恢复 | `docs/recovery.md` |
| 17 | Redis 使用边界 | `docs/architecture.md` |
| 18 | 并发模型 | `docs/architecture.md` |
| 19 | C++ 实现规范 | `AGENTS.md` §5（红线）、`docs/architecture.md`（完整条款） |
| 20 | API 约定 | `docs/api.md`；红线见 `AGENTS.md` §7 |
| 21 | 主要 API | `docs/api.md` |
| 22 | 前端页面 | `docs/frontend.md`；§22.5 分享弹窗见 `docs/permissions.md` |
| 23 | 前端状态管理 | `docs/frontend.md` |
| 24 | 异常流程和恢复策略 | `docs/recovery.md`；§24.6 幂等见 `docs/data-model.md` |
| 25 | 安全要求 | `docs/security.md`；红线见 `AGENTS.md` §8 |
| 26 | 配置管理 | `docs/deployment.md`（取含 `CORS_ALLOWED_ORIGINS` 的那一版） |
| 27 | 日志、监控和可观测性 | `docs/architecture.md` |
| 28 | 任务和后台清理机制 | `docs/recovery.md` |
| 29 | 存储统计和配额 | `docs/data-model.md` |
| 30 | 数据库事务边界 | §30.1/§30.2 → `docs/data-model.md`；§30.3 对象状态机 → `docs/storage.md` |
| 31 | 文件去重细节 | `docs/storage.md` |
| 32 | 文件上传的并发控制 | `docs/architecture.md` |
| 33 | API 分页和排序 | `docs/api.md` |
| 34 | 前端上传算法 | `docs/frontend.md` |
| 35 | REST API 认证方案 | `docs/api.md` |
| 36 | 管理员权限 | `docs/architecture.md` |
| 37 | 测试策略 | `docs/testing.md` |
| 38 | 验收标准 | `docs/testing.md` |
| 39 | 开发阶段 | **废弃**：以 `docs/phases/` 的 18 个任务书为准 |
| 40 | Git 提交规范 | `AGENTS.md` §12、`docs/deployment.md` |
| 41 | README 必须包含的内容 | `docs/deployment.md` |
| 42 | 备份和恢复 | `docs/recovery.md` |
| 43 | 默认实现决策 | `AGENTS.md` §2 |
| 44 | AI 编码约束 | `AGENTS.md` §5、§10、§11 |
| — | 第一版建议冻结的 MVP | `AGENTS.md` §10、`docs/phases/README.md` |

## 5. 冲突裁决记录

| # | 冲突 | 裁决 | 理由 |
|---|---|---|---|
| 1 | 原 §39「阶段一～九」vs 任务书「阶段零～十七」 | 以 18 个任务书为唯一权威，§39 的九阶段表废弃 | 任务书是实际要执行的内容，且粒度更细、带验收清单 |
| 2 | 「Drogon 或 Boost.Asio」 | **Drogon** | Windows + MSVC 下支持最好；文件传输与并发上传逻辑仍必须独立于 HTTP 框架 |
| 3 | 「Conan 或 vcpkg」 | **vcpkg（manifest 模式）** | 本机两者都没装；vcpkg 与 CMake/VS 集成更直接 |
| 4 | 编译器「未指定」 | **MSVC 14.51（`D:\VS2026`）** | 本机已有完整工具集与自带 CMake/Ninja；MinGW g++ 13.1 装 Drogon 生态很痛 |
| 5 | 「Ant Design 或统一组件库」 | **Ant Design** | 管理后台场景成熟 |
| 6 | 脚本形态「dev.sh / test.sh」 | **PowerShell `.ps1`** | 开发环境是 Windows，`.sh` 无法直接执行 |
| 7 | 项目规则 vs 全局规则重复 | Google C++ Style、Git 流程、临时目录、工具位置只在全局保留 | 避免两份漂移；项目级只写项目特有的东西 |
| 8 | 「密码用 Argon2id 或 bcrypt」 | **Argon2id** | 原分析首选，vcpkg 有 libargon2 |
| 9 | 「nlohmann/json 或 Protobuf」 | **nlohmann/json** | 原分析首选；本版没有需要 Protobuf 的场景 |
| 10 | 「Axios 或 fetch」 | **Axios** | 分块上传需要 `onUploadProgress`，只有 XHR 支持，Axios 原生提供 |
| 11 | 「Access Token + Refresh Token 或短期 JWT」 | **短期 JWT 单 Token** | 减少实现复杂度；双 Token 不在本版范围 |

## 6. 精简后的结构

```text
lan-file-hub/
├── AGENTS.md                 # 每次必守的规则与命令（12.6KB）
└── docs/
    ├── brief.md              # 产品定位、目标用户、边界、里程碑
    ├── architecture.md       # 系统组成、分层、并发、可观测性、C++ 实现规范
    ├── data-model.md         # 表结构、字段、状态枚举、事务边界、配额
    ├── api.md                # 端点、错误码、分页、认证
    ├── storage.md            # 对象存储、路径安全、分块与下载协议、去重
    ├── permissions.md        # 权限级别、检查点、继承、分享链接
    ├── security.md           # 安全要求与必须覆盖的攻击场景
    ├── recovery.md           # 故障处置、后台任务、备份恢复
    ├── frontend.md           # 页面、状态机、上传算法与重试
    ├── testing.md            # 测试分层与验收标准
    ├── deployment.md         # 配置、容器、数据卷、CI、发布产物
    ├── plan.md               # 本文
    ├── implementation-status.md  # 阶段零产出：实施状态与审计结论
    └── phases/               # 阶段任务书（编号唯一权威）
        ├── README.md         # 阶段索引与 MVP 冻结边界
        ├── 00-audit-and-plan.md … 17-final-review.md
```

`implementation-status.md` 已由阶段零产出；`performance.md`（阶段十四）与 `release-checklist.md`
（阶段十六）仍待对应阶段产出。文档中若出现指向它们的链接，属于对阶段产出物的引用，不是断链。

## 7. 环境实测记录（2026-09-12）

这是本次为写文档做的一次探测，**不是**阶段零要求的正式审计——正式结论由阶段零产出
`docs/implementation-status.md`。

| 工具 | 状态 | 位置 / 备注 |
|---|---|---|
| MSVC 14.51 | 可用 | `D:\VS2026`；含自带 CMake 与 Ninja |
| CMake 3.31.6 | 可用，**不在 PATH** | `D:\Codex Tools\CMake\cmake-3.31.6-windows-x86_64\bin\cmake.exe` |
| Ninja | 可用，已在 PATH | `D:\Codex Tools\Ninja\ninja.exe` |
| LLVM / clang-tidy | 可用，**不在 PATH** | `D:\Codex Tools\LLVM\bin\clang-tidy.exe` |
| clang-format | 可用（19.1.0），**不在 PATH** | `D:\Codex Tools\LLVM\bin\clang-format.exe`。注意 `D:\Codex Tools\clang-format\bin\` 下那个 exe **是坏的**（退出码 1、无输出），阶段零已发现并修正引用 |
| MinGW g++ 13.1 | 可用，已在 PATH | 不作为主工具链 |
| Node / npm | 可用 | `F:\Program Files\nodejs` |
| Git | 可用 | `E:\Git\cmd\git.exe` |
| Docker CLI | 已安装，**守护进程当前未运行** | 需先启动 Docker Desktop |
| gh | 已安装，不在 PATH | `D:\Codex Tools\gh` |
| vcpkg / conan | **未安装** | 阶段一前置项 |
| WSL | **无已安装发行版** | 若将来需要 Linux 构建环境需另装 |
| 项目目录 | 空，无 `.git` | 本次只生成文档，未做 Git 初始化 |

## 8. 风险

| # | 风险 | 说明与应对 |
|---|---|---|
| 1 | vcpkg 未装，Drogon 及其依赖（OpenSSL、zlib、jsoncpp 等）首次构建耗时长 | 阶段一第一件事；用 manifest 模式固定版本，避免"装到一半发现缺依赖" |
| 2 | 原文件曾是唯一副本 | 已通过 `docs/` 分流保全；**原目录不要删除**，等阶段零～一验证无误后再由用户决定 |
| 3 | 阶段十一～十七容易被无限推迟 | MVP 冻结边界已写进 `AGENTS.md` §10 与 `docs/phases/README.md`，发布前必做安全与恢复测试 |
| 4 | 性能验收指标（P95 < 300ms 等）未定义测试环境 | 阶段十四必须给出测试环境与可复现数据集，否则指标无意义；不得伪造数据 |
| 5 | Windows 下的路径、换行、脚本差异 | 脚本统一 PowerShell；CI 若用 Linux 需注意路径大小写与换行差异 |
| 6 | 多份文档可能随实现漂移 | 规则以 `AGENTS.md` 为准、细节以 `docs/` 为准；阶段任务书要求"新增 API 同步更新 `docs/api.md`" |

## 9. 建议实施顺序

严格按 `docs/phases/` 的编号推进：**阶段零 → 一 → 二 → … → 十**构成可演示的 MVP，
十一～十七在发布前完成。阶段零/一必须先解决 §10 的第 1、2 项前置条件，否则后面全部卡住。

## 10. 待确认事项

工具相关的一切（vcpkg、Docker、编译器、格式化工具、依赖服务）按全局 `AGENTS.md` 第 2、3 节执行：
额外工具统一放 `D:\Codex Tools`，安装前先列目录确认没有现成的。**这类事项不作为待确认项**——
已确认的选型见 §5 的裁决记录。

| # | 事项 | 影响 | 结论 |
|---|---|---|---|
| 1 | 许可证类型 | 仓库产物，不影响开发；只在别人要合法使用/分发时才需要 | **已定：MIT**（2026-10-28 仓库公开时确认）。Apache-2.0 含专利授权、更稳妥；这里选 MIT 是因为定位是个人/小团队自用与学习复用 |

**已确认、不再讨论的事项**：

- **项目定位（用户）**：自己先用起来，但按工业级标准做，目标是**公司可以采用**。
  含义是权限、审计、故障恢复、可运维性属于交付标准而不是加分项；
  `docs/phases/` 里阶段十一～十七的可靠性、安全与部署工作**发布前必做**。
- **与 AI 无关（用户）**：不引入 AI 能力，不做语义搜索/智能分类，不依赖模型服务、向量库或推理框架。
  原分析的「AI 文件分类和语义搜索」保持在非目标里。
- **时间预期（用户）**：不设。按阶段稳步推进。
- **上传时机（用户）**：全程本地开发，Git 远程仓库与推送留到项目末尾。
- **工具与依赖安装（用户）**：按全局规则执行，统一放 `D:\Codex Tools`，不逐项确认。

### 保真说明

以下几点是重组时发现的原文内部不一致，处理方式记录在此，便于回溯：

- 原 §26 配置管理重复两遍，取含 `CORS_ALLOWED_ORIGINS` 的版本。
- 原 §37.3 标题写「18 项」，正文实际只有 17 条；`docs/testing.md` 按正文 17 条保留，**没有凑数**。
- 原 §22.3（写 11 项，实际 12 项）、§22.4（写 10 项，实际 11 项）、阶段十约束（写 7 条，实际 8 条）：
  文档按原文实际条目全收。
- `docs/data-model.md` 里由枚举推导的状态转换表、以及从 `AGENTS.md` §2 引入的配额默认值，
  属于整理性补充而非原文条款，实现阶段如与任务书冲突，**以任务书为准**。
- `docs/api.md` 的错误码「含义与触发场景」是按原文上下文推断的——原文只给了 21 个错误码的名字，
  没有含义表。原文也没有完整的「错误码 → HTTP 状态」映射，文档只写了原文明确的那几项
  （409 ↔ `FILE_VERSION_CONFLICT`、429 ↔ 限流、以及不可直接重试的 401/403/404/409/413/422），
  **没有替它补齐**，补齐属于阶段二定义统一错误码时的工作。
- `docs/phases/` 下的任务书是用户原文**逐字搬入**的，里面保留了原文的「或」
  （例如阶段二的「使用 Argon2id 或 bcrypt」）。这些「或」以 `AGENTS.md` §2 的已定选型为准，
  任务书文本不改，以便随时回溯用户原始意图。
- 各文档由不同执行者按同一套规则并行重组，若发现两处对同一件事的表述不一致，
  以「规则看 `AGENTS.md`、表结构与枚举看 `docs/data-model.md`、端点看 `docs/api.md`」为准，
  并把不一致记回本文件。
