# 安卓端「局域网快传」设计记录（阶段 ③）

> 目标：**两台手机直接互传，不经过电脑**（用户 2026-09-21 明确要的能力，见 `AGENTS.md` §15）。
> 代码在 `android/`，构建脚本 `scripts/build-android.ps1`，用法见 `android/README.md`。

## 1. 形态与边界

| 项 | 决定 | 原因 |
|---|---|---|
| 服务端在哪 | **手机自己**（App 内置极简 HTTP 服务） | 用户要的就是"不经过电脑" |
| 接收方要装什么 | **什么都不用装**，浏览器扫码即可 | 门槛最低；iPhone 也能当接收方 |
| 平台 | **仅安卓** | iOS 不允许 App 长期跑本地服务 |
| 热点 | **用户手动开**，App 只检测地址 | Android 10+ 不允许 App 开关热点 |
| 规模 | 手机 → 手机 **≤ 1 GB**（上传上限留到 2 GB） | 用户口径；因此不做任何性能优化 |
| 账号体系 | **没有**（不登录、不注册、无权限） | 只是"临时把文件给对方"，账号体系纯属负担 |
| 数据 | App 外部私有目录 `getExternalFilesDir("share")` | 免存储权限、卸载即清 |

## 2. 代码结构

```text
android/app/src/main/java/com/lanfilehub/share/
├── ShareHttpServer.java   # 纯 Java 的极简 HTTP/1.1 子集：列文件 / 下载(Range) / 上传 / health，含目录穿越防护
├── ShareController.java   # 进程内单例：持有服务器与分享目录，界面和前台服务共用
├── ShareService.java      # 前台服务：只负责让进程活着 + 常驻通知显示地址
├── NetUtil.java           # 找出局域网/热点里的 IPv4（优先 wlan/ap/eth）
└── MainActivity.java      # 界面：加文件 / 开始停止分享 / 显示地址与二维码 / 文件列表
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
application-label:'局域网快传'
launchable-activity: com.lanfilehub.share.MainActivity
uses-permission: INTERNET / ACCESS_NETWORK_STATE / ACCESS_WIFI_STATE /
                 FOREGROUND_SERVICE / FOREGROUND_SERVICE_DATA_SYNC / POST_NOTIFICATIONS
签名：C=US, O=Android, CN=Android Debug（调试密钥，可直接安装）
```

**没能验证的（如实说明）**：`adb devices` 为空 —— **没有真机可装**，所以
「装到手机上、开热点、对方扫码下载」这条真实链路**没有跑过**；手机热点下的实际速度也没测。


## 5. 已知限制

- 分享期间依赖前台服务保命；用户"强行停止"App 会中断传输。
- 只在内网可达（同一 WiFi/热点），且**手机用流量打不开**。
- 手机作为服务器时，**屏幕常亮与否不影响传输**，但系统深度省电模式可能冻结进程（未做白名单引导）。
- iOS 端只能当接收方。
- release 包未配置签名，需要自己签名才能安装。
