# 局域网快传（Android）

把**你的安卓手机**变成一台临时的文件服务器：朋友**不装 App、不注册**，
连上同一个 WiFi / 热点后用浏览器**扫二维码**就能下载；也能把文件**传回给你**。

> 这是「五阶段改造」的第 ③ 步：两台手机直接互传，**不经过电脑**（见项目 `AGENTS.md` §15）。

## 三步用起来

1. 打开 App → 点「**选择要分享的文件**」，把要发的文件加进来（App 会复制一份到自己的目录里）。
2. 点「**开始分享**」→ 屏幕上出现**地址**和**二维码**。
3. 让对方连**同一个 WiFi**（或者你开的热点），用**相机 / 微信扫一扫**那个二维码 →
   浏览器打开后点「下载」即可；对方也能在这个页面上**上传文件给你**。

用完点「停止分享」。想清掉分享目录里的文件，点「清空已分享的文件」。

## 构建（在电脑上）

```powershell
# 跑内置 HTTP 服务的单元测试 + 出 debug APK
.\scripts\build-android.ps1

# 只出包（跳过测试）
.\scripts\build-android.ps1 -SkipTests

# release 包（未签名，需要自己配签名才能装）
.\scripts\build-android.ps1 -Release
```

产物：**`android\lan-file-hub-share.apk`**（debug 包已用调试密钥签名，可以直接装到手机上）。

工具位置（都已在本机装好）：JDK 21、Android SDK（`D:\Codex Tools\Android`，platform 35 + build-tools 35.0.0）、
Gradle 8.9（`D:\Codex Tools\gradle\gradle-8.9`）、`GRADLE_USER_HOME=D:\Codex Tools\gradle\home`。

## 为什么是这个技术形态

- **只有安卓能做服务端**：iOS 不允许 App 长期跑本地服务，所以 iPhone 只能当**接收方**（浏览器扫码）。
- **热点必须你手动开**：Android 10+ 不允许 App 开关热点（系统限制），App 只负责读出地址并生成二维码。
- **不照搬电脑端后端**：电脑端依赖 PostgreSQL/Redis，手机上装不了；这里只做
  「分享目录 + 上传下载 + Range 断点续传」，不做账号/权限/版本/回收站。
- **用 Java 而不是 Kotlin**：本机 Gradle 缓存里已有 AGP 8.5.2 但没有 Kotlin 插件，
  用 Java 能少下载几百 MB、构建更快；这个 App 只有一个 Activity + 一个 HTTP 服务，Java 完全够。
- **HTTP 服务写成纯 Java 类**：`ShareHttpServer` 不 import 任何 `android.*`，
  所以能在**电脑上用 JUnit 起真服务**，把下载、断点续传（Range）、上传、目录穿越防护验证一遍
  —— 没有真机也能验核心逻辑（Android 那层只负责启停服务、显示二维码）。

## 已知限制（先说清楚）

- 传输期间要靠**前台服务**（有常驻通知）保命；手动「强行停止」App 会中断分享。
- 只在**内网**可用：同一个 WiFi 或热点；手机用流量打不开。
- 单个上传上限 2 GB；手机剩余空间不足时会明确提示（不会写坏文件）。
- debug 包的图标与界面是"能看懂就行"的程度，没做美化。
- **没有在真机上跑过**（验证记录见 `docs/android.md`）。
