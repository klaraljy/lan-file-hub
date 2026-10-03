# 安卓端 App 设计记录（阶段 ③ + 2026-10-02 连接器）

> 目标一（阶段 ③，2026-09-21）：**两台手机直接互传，不经过电脑**。
> 目标二（2026-10-02，用户要求）：**App 里连电脑** —— 记住用过的 IP、显示在不在线、
> 点一下在 App 内打开电脑的网页（"手机上网页和 App 都要有"）。
> 代码在 `android/`，构建脚本 `scripts/build-android.ps1`，用法见 `android/README.md`。

## 1. 形态与边界

| 项 | 决定 | 原因 |
|---|---|---|
| App 首页 | **「连接电脑」**（历史地址 + 在线状态 + App 内打开网页） | 用户 2026-10-02 选定："就在 App 内打开电脑的网页，简单一点" |
| 网页从哪来 | **电脑端那套网页**（App 只是内置浏览器） | 网页改一次 App 不用改；不做第二套界面 |
| 地址从哪来 | 用户手输一次（可只输 IP，端口默认 9180），之后**记在手机本地** | 用户要"里面有历史的 IP，我们可以选择连接" |
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
├── ConnectActivity.java   # 首页（2026-10-02）：历史地址列表 + 在线状态 + 添加/删除 + 进快传
├── WebViewActivity.java   # App 内浏览器：打开电脑网页（开 JS/DOM storage/Cookie，登录态能保持）
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


## 5. 已知限制

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
