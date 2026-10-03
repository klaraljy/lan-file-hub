# 安卓端 App 设计记录（阶段 ③ + 2026-10-02 连接器 + 2026-10-03 自动找电脑 + 2026-10-03 更新/自愈/记住登录）

> 目标一（阶段 ③，2026-09-21）：**两台手机直接互传，不经过电脑**。
> 目标二（2026-10-02，用户要求）：**App 里连电脑** —— 记住用过的 IP、显示在不在线、
> 点一下在 App 内打开电脑的网页（"手机上网页和 App 都要有"）。
> 目标三（2026-10-03，用户真机反馈）：**打开 App 就自己找到电脑** + 显示当前网络 +
> 顶栏并成一行（详见 §7）。
> 目标四（2026-10-03 晚，用户批准的三件事，**构建号 5**）：① App 内原生「检查更新」
> ② 地址丢失自愈 ③ App 内默认记住登录（详见 §9）。
> 代码在 `android/`，构建脚本 `scripts/build-android.ps1`，用法见 `android/README.md`。

## 1. 形态与边界

| 项 | 决定 | 原因 |
|---|---|---|
| App 首页 | **「连接电脑」**（当前网络 + 自动查找 + 历史地址 + 在线状态 + App 内打开网页） | 用户 2026-10-02 选定："就在 App 内打开电脑的网页，简单一点"；2026-10-03 追加"自动检测" |
| 网页从哪来 | **电脑端那套网页**（App 只是内置浏览器） | 网页改一次 App 不用改；不做第二套界面 |
| 地址从哪来 | **自动扫当前网段找**（2026-10-03 起）；找不到时才要用户手输一次 | 电脑的地址由路由器分配、换网络还会变，用户不可能知道 |
| 当前网络 | 只显示 **WiFi / 手机热点 / 移动数据**，⛔ **不读 WiFi 名字** | 读 SSID 在 Android 10+ 要定位权限，不值得（用户 2026-10-03 明确选择） |
| 在线状态 | App 每 10 秒请求一次 `/health/live`（1.5 秒超时） | 换网络后旧地址会失效，要让用户一眼看出哪个能用 |
| 服务端在哪（快传） | **手机自己**（App 内置极简 HTTP 服务） | 用户要的就是"不经过电脑" |
| 接收方要装什么 | **什么都不用装**，浏览器扫码即可 | 门槛最低；iPhone 也能当接收方 |
| 平台 | **仅安卓** | iOS 不允许 App 长期跑本地服务 |
| 热点 | **用户手动开**，App 只检测地址 | Android 10+ 不允许 App 开关热点 |
| 规模 | 手机 → 手机 **≤ 1 GB**（上传上限留到 2 GB） | 用户口径；因此不做任何性能优化 |
| 账号体系 | 快传**没有**；连接器用**电脑端的账号**（在网页里登录） | 不在手机端再造一套账号 |
| 数据 | 快传文件在 `getExternalFilesDir("share")`；地址记录在 SharedPreferences | 免存储权限、卸载即清 |

## 2. 代码结构

```text
android/app/src/main/java/com/lanfilehub/share/
├── ConnectActivity.java   # 首页（2026-10-02）：当前网络 + 自动查找 + 历史地址列表 + 在线状态 + 进快传/相册备份 + 检查更新（2026-10-03）
├── AppUpdate.java         # App 内「检查更新」（2026-10-03）：读 /app/release、比构建号、给下载地址（不 import android.*，电脑上可单测）
├── BackupActivity.java    # 「相册备份」页（2026-10-03）：登录 + 选相册 + 只在 WiFi + 进度
├── BackupController.java  # 备份执行器：后台线程逐张上传、只在 WiFi、传过的不重传、可随时停
├── BackupService.java     # 前台服务：备份时的常驻进度通知（几百张要跑很久）
├── BackupStore.java       # 备份的本地设置与"已备份清单"（SharedPreferences，⛔ 只存令牌不存口令）
├── BackupPlanner.java     # 纯逻辑：该传哪些、服务器上叫什么名字（可在电脑上单测）
├── MediaScanner.java      # 读相册（MediaStore.Images）与照片内容流
├── ApiClient.java         # 后端接口的最小客户端（登录/根目录/建文件夹/分块上传），只用系统 API
├── Sha256Hex.java         # SHA-256 十六进制（上传时声明哈希用）
├── WebViewActivity.java   # App 内浏览器：打开电脑网页（开 JS/DOM storage/Cookie，登录态能保持）
├── ServerDiscovery.java   # 自动找电脑（2026-10-03）：扫本网段 /health/live，认 "service":"lan-file-hub"
├── NetworkState.java      # 当前网络类型（2026-10-03）：WiFi / 手机热点 / 网线 / 移动数据（不读 SSID）
├── ServerStore.java       # 地址的本地记录（SharedPreferences）+ 用户输入解析（可只输 IP）
├── ServerProbe.java       # 探活：GET /health/live，1.5 秒超时，失败即"离线"
├── ServerAdapter.java     # 列表渲染：名字 / 地址 / 在线状态（颜色区分）
├── MainActivity.java      # 「快传」页：加文件 / 开始停止分享 / 显示地址与二维码 / 文件列表
├── ShareHttpServer.java   # 纯 Java 的极简 HTTP/1.1 子集：列文件 / 下载(Range) / 上传 / health，含目录穿越防护
├── ShareController.java   # 进程内单例：持有服务器与分享目录，界面和前台服务共用
├── ShareService.java      # 前台服务：只负责让进程活着 + 常驻通知显示地址
└── NetUtil.java           # 找出局域网/热点里的 IPv4（优先 wlan/ap/eth）
```

界面刻意**不引入 AndroidX**（只用系统 View/Activity）：依赖少、构建快、老手机也不挑。
HTTP 服务刻意**不 import 任何 `android.*`**：这样它能在电脑上用 JUnit 起真服务验证。

## 3. 内置 HTTP 服务的接口

```text
GET  /                  文件列表 + 上传表单（中文、手机友好、按钮大）
GET  /f/<文件名>         下载，支持 Range（断点续传）
POST /upload?name=xxx   上传（请求体就是文件内容，页面用 XHR 发，带进度条）
GET  /health            探活，返回 ok
```

安全要点：

- 文件名只允许"纯文件名"：含 `/`、`\`、`..`、以 `.` 开头、空名、超过 200 字符一律拒绝；
  读写前再用**规范化路径**确认落在分享目录内（防目录穿越）。
- `Content-Disposition` 同时给 ASCII 回退名与 `filename*=UTF-8''`（中文文件名不会乱码）。
- 上传先写 `.part` 再改名（避免半截文件被当成完整文件）；空间不足直接拒绝（507），不写坏文件。
- 上传必须有 `Content-Length`（浏览器 fetch 一定会带），否则 411。

## 4. 验证记录

| 验证项 | 怎么验的 | 结果 |
|---|---|---|
| 编译 | `.\scripts\build-android.ps1` | 见下方"实测"一节 |
| 下载 / 断点续传 / 上传 / 目录穿越 | **电脑上跑 JUnit**（`app/src/test/.../ShareHttpServerTest.java`，起真服务发真请求） | 见下方"实测" |
| APK 有效性 | `aapt2 dump badging`（build-tools 35.0.0） | 见下方"实测" |
| 真机安装与使用 | 需要一台安卓手机 | **未验证**（本机 `adb devices` 为空） |
| 手机热点下的真实速度 | 需要两台手机 | **未验证** |
| iOS 接收 | 需要 iPhone | **未验证**（设计上浏览器即可，但没有实机） |

### 实测

**构建**（2026-09-21，`.\scripts\build-android.ps1`）：

```text
BUILD SUCCESSFUL in 53s
✅ 完成：E:\DeepSeekProjects\lan-file-hub\android\lan-file-hub-share.apk
   体积：0.3 MB（310.8 KB）
```

**内置 HTTP 服务单元测试**（在电脑 JVM 上起真服务、发真请求）：**15/15 通过，0 失败**：

```text
总体：tests=15 failures=0 errors=0
用例：healthEndpointAnswersOk / indexListsSharedFiles / downloadsWholeFile /
      supportsRangeForResumableDownload / supportsSuffixRange / unsatisfiableRangeReturns416 /
      missingFileReturns404 / chineseFileNameWorks / refusesPathTraversal /
      uploadsFileFromBrowser / uploadRejectsBadName / uploadWithoutLengthIsRejected /
      rangeParsingHandlesEdgeCases / sizeFormattingIsReadable / fileListIsSortedByNewestFirst
```

**APK 校验**（`aapt2 dump badging` + `apksigner verify`）：

```text
package name='com.lanfilehub.share' versionCode='1' versionName='1.0'
minSdkVersion:'26'  targetSdkVersion:'35'  compileSdkVersion='35'
application-label:'局域网文件柜'
launchable-activity: com.lanfilehub.share.ConnectActivity   ← 2026-10-02 起首页是「连接电脑」
uses-permission: INTERNET / ACCESS_NETWORK_STATE / ACCESS_WIFI_STATE /
                 FOREGROUND_SERVICE / FOREGROUND_SERVICE_DATA_SYNC / POST_NOTIFICATIONS
android:usesCleartextTraffic="true"   ← Android 9+ 必须；局域网全是明文 HTTP
签名：C=US, O=Android, CN=Android Debug（调试密钥，可直接安装）
```

**连接器（2026-10-02 新增）的验证**：

| 验证项 | 怎么验的 | 结果 |
|---|---|---|
| 地址解析（只输 IP / 带端口 / 整条 URL / https / 乱输） | `ServerStoreTest`（电脑 JVM 跑，6 条） | **6/6 通过** |
| 快传服务回归 | `ShareHttpServerTest`（起真服务发真请求，15 条） | **15/15 通过** |
| 构建与产物 | `.\scripts\build-android.ps1` | BUILD SUCCESSFUL，APK 337.7 KB（2026-10-03 08:30） |
| 清单与启动页 | `aapt2 dump badging` / `dump xmltree` | 启动页 = `ConnectActivity`；`usesCleartextTraffic=true`；`WebViewActivity` 已注册 |
| **App 里连电脑、看在线、打开网页** | 需要一台安卓手机 | **未验证**（本机 `adb devices` 为空） |

**没能验证的（如实说明）**：`adb devices` 为空 —— **没有真机可装**，所以
「装到手机上、开热点、对方扫码下载」这条真实链路**没有跑过**；手机热点下的实际速度也没测。


## 5. 出包与"网页下载入口"的联动（2026-10-03 新增）
用户要求：网页「我的」里要有一个**永远指向最新 APK** 的入口，而且"**改完就更新的**" ——
重新构建一次 App、刷新网页，卡片上的大小与时间就必须跟着变，不需要重启后端、也不许手工拷文件。

**谁负责什么**

| 角色 | 做什么 |
|---|---|
| `scripts\build-android.ps1` | 出包后写侧车 `android\lan-file-hub-share.apk.json`（版本/大小/构建时间/SHA-256），并把 APK + 侧车**自动同步**到 `portable\app\web\downloads\lan-file-hub.apk`（旧的手工副本 `portable\app\web\lan-file-hub.apk` 会被清理掉） |
| 后端 `GET /api/v1/app/release` | **每次请求现看磁盘**：大小 / 时间 / SHA-256 都来自真实文件（SHA-256 只在这个包变化时算一次）；版本号与构建时间取自侧车，**且要求侧车与包的字节数一致**（对不上就整体忽略，免得出现"新包 + 旧版本号/旧时间"）。文件没换就复用缓存 |
| 后端 `GET /api/v1/app/download` | 流式发安装包，带 `Content-Disposition: attachment` 与 `application/octet-stream` |
| 网页「我的」→「配套手机应用」 | 显示 `版本 · 大小 · 构建时间` + 「下载安装」按钮；电脑上还给出二维码（复用 `ShareLinkBox`），手机扫一下直接下载 |

**两条命令**

```powershell
.\scripts\build-android.ps1              # 出包 + 自动同步（改完 App 就跑这个）
.\scripts\build-android.ps1 -PublishOnly # 不重新构建，只把现有 APK 同步过去（换机器/补文件时用）
.\scripts\build-android.ps1 -SkipPublish # 只出包，不同步（不想动便携包时用）
```

**验收口径**：重新构建一次 → 刷新网页「我的」→ 卡片上的**大小与时间变了** → 点「下载安装」
（或扫二维码）拿到的就是新包。数字来自磁盘，所以构建脚本换包之后**不用重启后端**。

## 6. 打开 App 就自动找到电脑（2026-10-03，用户真机反馈）

用户原话：「首先我点开这个 app 应该能够自动检测我这个程序有没有连上手机的热点或者局域网」。

**做什么**

| 行为 | 实现 | 为什么这样做 |
|---|---|---|
| 顶部显示**当前网络** | `NetworkState`：WiFi / 手机热点 / 网线 / 移动数据（"流量"标黄） | 用户要求"一眼看出连上没有"；⛔ 不读 SSID，避免申请定位权限（用户明确选择） |
| 热点怎么认 | 活动网络是 WiFi **且自己的地址是热点网关**（`192.168.43.1` 等）→ 判为"我在开热点" | Android 10+ 不允许 App 开关热点，只能看地址；逻辑是纯函数，有单测 |
| **自动找电脑** | `ServerDiscovery`：先探已知地址，全都不在线时**并发扫本网段** `/health/live`（32 线程，600ms 超时，典型 2~5 秒），响应里带 `"service":"lan-file-hub"` 才算自家电脑 | 电脑地址由路由器分配、换网络就变，用户不可能知道；后端早就在 `/health/live` 里带了这个字段，**后端不用改** |
| **App 内下载**（2026-10-03 补） | WebView **默认没有下载能力**，点下载链接毫无反应 —— 必须 `setDownloadListener` 交给系统 `DownloadManager`（`WebViewActivity.startSystemDownload`）：文件名从 `Content-Disposition` 取（纯逻辑在 `DownloadNaming`，**必须净化路径分隔符**，有单测），带 Cookie，落「下载」目录，通知栏点开即安装/打开 | 用户反馈"app 里面的 apk 下载直接改成更新"时发现：App 里连**普通文件下载**都是坏的（点了没反应） |
| **网页要知道已装构建号** | UA 末尾追加 `LanFileHubApp/<versionCode>`（`WebViewActivity.UA_MARKER`）；网页据此把「下载安装」显示成「**更新到构建号 N**」或「**已是最新**」（`features/app/appUpdate.ts`） | 已经装了还说"下载安装"很奇怪；没有这个标记网页分不清"手机浏览器"和"App 内" |
| 找到之后 | 自动记进地址簿（标注「自动发现」）并显示「发现 N 台电脑 —— 点一下就能用」 | 不用用户手输 |
| 扫不到时 | 按网络类型给**人话解释**：流量 → "连不上，请连同一个 WiFi/热点"；热点 → 检查电脑是否开机、是否连了这台手机；**大网段（掩码比 /24 宽，校园网常见）→ 说清"只扫了自己那一段，不代表连不上"，让用户照电脑「设置 → 手机怎么连」的地址点「添加地址」**（`NetUtil.prefixLength`/`isWideSubnet`）；普通 /24 → 才提示可能是**客户端隔离** → 改用手机热点 | AGENTS §14.4/§14.6：不能让用户面对一句"没找到"；2026-10-03 用户实测：校园网（10.200.x/16）**能连**，只是自动发现扫不到 —— 那会儿的文案一律说"客户端隔离"，会把人骗去开热点 |
| 手动入口 | 「找电脑」按钮（自己点就再扫一次，不受"同一网络只自动扫一次"限制） | 换网络后想立刻重扫 |
| 顶栏并成一行 | `activity_web.xml`：左边固定「局域网文件柜」+ 中间小字 `IP:端口`（点它=刷新）+ 右边三个 44dp 图标按钮（⟳ / 🌐 / ✕） | 用户反馈"最顶部 IP 地址、刷新、关闭、跳转浏览器太突兀"，要求并进同一行 |

**验证**

| 验证项 | 怎么验的 | 结果 |
|---|---|---|
| 候选地址计算（/24、/25、/16 截断、坏地址） | `ServerDiscoveryTest`（电脑 JVM） | **5 条全过** |
| 认出自家后端 / 热点网关判定 / 流量不能连 | `ServerDiscoveryTest` + `NetworkStateTest` | **9 条全过** |
| 出包与同步 | `.\scripts\build-android.ps1` | BUILD SUCCESSFUL，354001 字节（2026-10-03 11:15） |
| 网页下载卡片跟着变 | `GET /api/v1/app/release` | size 346945 → **354001**、built_at 变新，**后端没重启** |
| **真机**（自动找到电脑、顶栏观感） | 需要用户手机 | **未验证**（本机没有真机） |

## 7. 相册备份（2026-10-03，用户批准的功能）

用户口径：手机相册里的照片要能**一键（或自动）备份到电脑**上，而且**只在 WiFi/热点下传**。

| 项 | 做法 | 为什么 |
|---|---|---|
| 入口 | 「连接电脑」页底部按钮「相册备份（把手机照片存到电脑）」 | 与「快传」并列，首页就能看见 |
| 登录 | App 内直接输账号口令 → `POST /auth/login`（`remember=true`）→ **只存令牌**到 SharedPreferences | 上传要带令牌；口令永不落盘（与网页端同规矩） |
| 传到哪 | 电脑上的「**手机相册**」文件夹（不存在就建；先查后建，避免 409） | 用户在网页里一眼能找到 |
| 传哪些 | 用户勾选的相册（MediaStore 的 `BUCKET_DISPLAY_NAME`）；只传**图片**（不传视频） | 视频体积大且当前上传是"整张读进内存"，不冒这个险 |
| 去重 | 本地清单，键 = 相册名 + 文件名 + 字节数 + 拍摄时间；每 5 张批量落盘 | 不同相册会有同名照片；清单只增不减，崩溃最多重传 5 张 |
| 文件名 | `日期_相册_原名`（如 `20261003_Camera_IMG_0001.jpg`） | 否则一堆 `IMG_0001 (7).jpg` 谁也认不出 |
| 仅 WiFi | 默认开；开始前与**每张之前**都检查，流量/切网就停下并说明原因 | 用户口径"只在 WiFi 下备份"，也是省流量 |
| 中断恢复 | 停下/被杀之后下次接着传（已传的在清单里） | 几百张照片不可能一次跑完 |
| 权限 | `READ_MEDIA_IMAGES`（Android 13+）/ `READ_EXTERNAL_STORAGE`（≤12）；**不申请定位** | 读相册是功能必需；定位与功能无关（用户明确选择不要） |
| 进度 | 前台服务 + 常驻通知（`BackupService`），文案形如「正在备份 3/128：IMG_0001.jpg」 | 备份久，用户必须能看见在做什么 |
| **开工前先确认**（2026-10-03 用户要求） | 点「开始备份」→ 后台算出"待传 N 张、约 X MB"（`BackupPlanner.summarize` + `BackupController.preview`）→ 弹确认框「将上传 N 张照片（约 X MB）…」→ 用户按「开始备份」才真的传 | 用户原话：「这个应该是可选择性传输的吧，你不会点一下就全备份了吧」。预览与实际备份**用的是同一个收集方法**（`collectPending`），不许预览一套、实际传另一套 |
| **构建号自增**（2026-10-03 用户要求"加固覆盖安装"） | `scripts\build-android.ps1` 每次出包把 `versionCode` +1（`-NoVersionBump` 可关）；App 首页底部显示「App 版本 1.0（构建号 2）」，网页「我的」的下载卡片显示同一个号 | 覆盖安装是否生效、手机上是不是最新，两处一对就知道。同包名 + 同签名时覆盖安装**保留数据**（地址簿、令牌、已备份清单） |
| 设备名（已知，用户 2026-10-03 知情） | App 是**系统 WebView 壳**（Chromium 内核）+ React 网页；UA 里仍有 `Chrome/`，所以它在「广场 / 我的设备」里显示成 **Chrome · Android**，与手机浏览器同名但**是另一条设备记录**（device_id 存在各自浏览器存储里） | 用户明确说"这个不必要"改，所以**保持现状**；以后若要区分，判 UA 里的 `; wv` 即可 |

**验证**

| 验证项 | 怎么验的 | 结果 |
|---|---|---|
| 该传哪些 / 去重 / 文件名净化 | `BackupPlannerTest`（电脑 JVM，6 条） | **全过**（含"不同相册同名照片算两张"） |
| 服务器侧那串调用（登录 → 根目录 → 先查后建「手机相册」→ 分块上传 → 完成） | `_tmp\lan-file-hub\verify-backup-api.ps1` 真跑一遍 | **全过**：建文件夹 0→1 个、上传成功、同名第二次自动变 `(1)` |
| 出包 | `.\scripts\build-android.ps1` | BUILD SUCCESSFUL，429 KB（2026-10-03 11:55） |
| **真机**（相册权限、MediaStore 读取、前台服务、几百张照片的实际速度） | 需要用户手机 | **未验证**（本机没有真机，`adb devices` 为空） |

## 8. 已知限制

- **连接器是"内置浏览器 + 地址簿"**：文件列表/上传/下载全部由电脑端网页提供，所以
  **电脑没开、或不在同一个网络时，App 里什么也打不开**（列表会显示"离线"）。
- WebView 里下载**大文件**（电脑 → 手机 ≤ 5 GB）的实际表现**没有真机验证**；
  必要时可在网页里用「下载」按钮走浏览器原生下载，或点工具条的「浏览器」用系统浏览器打开。
- 网页里的登录态靠 Cookie 保存；用户清理 App 数据后需要重新登录。
- 分享期间依赖前台服务保命；用户"强行停止"App 会中断传输。
- 只在内网可达（同一 WiFi/热点），且**手机用流量打不开**。
- 手机作为服务器时，**屏幕常亮与否不影响传输**，但系统深度省电模式可能冻结进程（未做白名单引导）。
- iOS 端只能当接收方。
- release 包未配置签名，需要自己签名才能安装。

## 9. 构建号 5：App 内检查更新 + 地址自愈 + 默认记住登录（2026-10-03 晚，用户批准）

用户批准的三件事（顺序即优先级），做完出**构建号 5**。

### 9.1 App 内原生「检查更新」（不依赖网页）

| 项 | 做法 |
|---|---|
| 入口 | 「连接电脑」页底部「检查更新」按钮（+ 一行小字说明最后要自己点「安装」） |
| 读什么 | `AppUpdate.check(最近用过的那台电脑, 本机构建号)` → `GET /api/v1/app/release`（**公开、无需登录**） |
| 有新版 | 按钮变「**更新到构建号 N**」→ 再点一下用系统 `DownloadManager` 下 `/api/v1/app/download`，落「下载」目录、通知栏点开进系统安装器 |
| 已最新 | 提示「已是最新（构建号 N）」，按钮灰掉并写明同一句话 |
| 没连过电脑 | 提示先点「找电脑」；连不上 → 一句人话（"请确认电脑上的程序开着…"），**不弹 HTTP 码、不崩** |
| 换电脑就作废 | 记着"上次问的是哪台"；换了电脑（换网络就会换）自动退回「检查更新」，不拿旧答案骗人 |

⛔ **静默安装做不到，也没去申请**：侧载应用没有 `INSTALL_PACKAGES`，申请设备管理员/MDM 会破坏
"便携、不自作主张"的定位（用户没同意）。所以最后一跳永远是系统安装器的「安装」按钮 ——
按钮下的说明、下载提示、"最后一步"提示都把这句话写清楚了（AGENTS §14.1：不能让用户以为点完就自动装好）。

为什么 `AppUpdate` 自己手写 JSON 取值而不用 `org.json`：Android 的 `org.json` 在 JVM 单测里是**空壳**
（`unitTests.returnDefaultValues = true`，解析出来全是空值），手写一小段就能让"解析 + 判版本 + 拼地址"
整条链路在电脑上跑真 HTTP 服务验一遍（与 `ShareHttpServer`、`BackupPlanner` 同一个路子）。

### 9.2 地址丢失自愈

现状：地址簿存在 App 私有的 `SharedPreferences`（`lan_file_hub_servers`），**卸载重装 / 系统清理
App 数据就没了**（用户 2026-10-03 就踩到过：覆盖安装签名的坑）。

做法：地址列表为空、或**所有已知地址都不在线**时自动扫一遍当前网段（原来就有），
**并把这次扫描标记为"找回"** —— 找到了就在状态行明确说：

> 这台手机上的电脑地址没了（卸载重装、或系统清理过 App 数据）—— 已自动找回上次那台电脑：10.200.18.61:9180

找不到时仍按网络类型给人话解释（流量 / 热点 / 大网段 / 客户端隔离，见 §6），并提示「手动输入地址」。
手动点「找电脑」不算"找回"（用户知道自己在干什么，不需要这句解释）。

### 9.3 App 内默认记住登录

- 网页：`LoginPage` 的「记住我」本来就默认勾上（令牌进 `localStorage`，后端 30 天）；
- App 内：**强制勾上且不可取消**（`Checkbox disabled`），文案写明「在这台手机上保持登录 30 天」，
  提交时无条件按 `remember=true` 走 —— 手机 App 每天重新输口令是产品缺陷（AGENTS §14.1）；
- 判定方式：UA 里的 `LanFileHubApp/<versionCode>`（`rememberLockedInApp`，纯函数 + 单测）；
  ⛔ 认不出来（普通浏览器、电脑）时**不许锁**，公用电脑上用户要能取消勾选。

### 9.4 验证

| 验证项 | 怎么验的 | 结果 |
|---|---|---|
| 版本比较（有新版 / 已最新 / 读不到服务器版本号） | `AppUpdateTest`（电脑 JVM） | 见下方"实测" |
| `/app/release` 响应的解析（真实形状、缺字段、乱码、带花括号的版本名） | `AppUpdateTest` + 起真 HTTP 服务 | 见下方"实测" |
| 整条链路：读接口 → 判版本 → 给出人话（有新版 / 已最新 / 服务器没包 / 连不上 / 没连过电脑） | `AppUpdateTest`（JDK 自带 HTTP 服务，回环随机端口） | 见下方"实测" |
| 「App 内强制记住登录」的判定 | `frontend/tests/appUpdate.test.ts`（`rememberLockedInApp`） | 见下方"实测" |
| 出包与同步 | `.\scripts\build-android.ps1` | 见下方"实测" |
| **真机**（点「检查更新」、下载、装、地址自愈、30 天免登录） | 需要用户手机 | **未验证**（本机 `adb devices` 为空） |

