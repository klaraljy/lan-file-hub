# 交接：安卓端更新与记忆（2026-10-03）

> 用户已批准这三件事，**下一轮直接开工**，不用再确认。做完出 **构建号 5**。
>
> ✅ **2026-10-03 晚已完成**：三件事全部落地并出包 **构建号 5**（381 KB，`version_code=5` 已在运行中的
> 便携包后端读到）。实现细节与验证记录见 `docs/android.md` §9；**真机三项仍待用户复验**（见 §5）。

## 1. 目标与优先级（按顺序做）

1. **App 内原生「检查更新」**（不依赖网页）
   - 位置：`ConnectActivity`（连接页）底部或工具栏，按钮文案「检查更新」。
   - 逻辑：读自己的 `versionCode` → 请求 `<服务器>/api/v1/app/release`（**公开接口，无需登录**，返回 `version_code/version_name/size_bytes/built_at/download_path`）→
     - 有新版：按钮变「更新到构建号 N」→ 点一下用 **DownloadManager** 下 `<服务器>/api/v1/app/download`（`Content-Disposition: filename="lan-file-hub.apk"`，无需登录）→ Toast「下完在通知栏点一下安装」；
     - 已最新：Toast「已是最新（构建号 N）」。
   - ⛔ **静默安装做不到**（侧载应用没有 `INSTALL_PACKAGES`）：最后一跳必然是系统安装器的「安装」确认。**要跟用户说清、界面也要写清**。
   - 服务器地址用 `ServerStore` 里最近使用/在线的那一条；没有地址时提示先「找电脑」。
2. **地址丢失自愈**
   - 现状：`SharedPreferences`（`lan_file_hub_servers`）在 App 私有目录，**被卸载/系统清数据就会没**；App 打开进「连接」页已有"同一网络只自动扫一次"。
   - 要做的：地址列表为空 / 没有任何在线条目时，**自动扫一次**并自动加入发现到的电脑（`ServerDiscovery`），界面同时给一句「已自动找回上次那台电脑」；失败才提示手动「添加地址」。
3. **App 内默认记住登录**
   - 网页侧：`frontend/src/pages/LoginPage.tsx` 的「记住我」`initialValue={true}`（已默认勾上，令牌进 `localStorage`，后端 30 天）。
   - 要做的：**App 内强制勾上且不可取消**（识别方式：UA 里带 `LanFileHubApp/<versionCode>`，见 `features/app/appUpdate.ts` 的 `installedBuildFromUserAgent`），文案写「在这台手机上保持登录 30 天」。

## 2. 红线

- **签名与 versionCode**：所有包必须用本机同一 debug 签名（`scripts/build-android.ps1`，versionCode 自动 +1）。**签名不同 = 系统只能卸载重装 = 账号与地址全丢**（用户 2026-10-03 就踩到过）。改签名相关配置前必须确认。
- 不要承诺"静默安装"；不要为了静默去申请设备管理员/MDM（用户没同意，且会破坏便携定位）。
- ⛔ 后台任务同一时刻只允许一个（项目 AGENTS §16）；`.ps1` 必须 UTF-8 **带 BOM**；验证脚本要用文件跑（`pwsh -File`），不要长内联命令。
- 集成测试必须跑**独立测试库**（`lan_file_hub_test`，便携包 PG 55432）；⛔ 别对用户真实库跑。
- 跑后端测试时**不要设 `$env:DATABASE_URL`** 去跑单元测试（`LoadConfigTest` 会因环境变量优先而假失败，见 AGENTS §11.1）。

## 3. 现状与数字（2026-10-03 收尾时）

- 已上线（便携包运行中，`http://10.200.18.61:9180`，健康 200）：
  - 前端：目录树、上传独立成页、失败可见性、视频/音频预览、文件夹「下载」（**逐个下、不打包**，先列内容）、手机怎么连卡片、按钮统一规范（§2.3）。
  - 后端：`GET /api/v1/folders/{folder_id}/files`（含子目录的扁平文件清单，供"一同下载"）。
- 测试：前端 **240/240**；后端 `FolderFlowTest` **15/15**（含 2 条新增：子目录相对路径、越权 404）；全量后端曾跑到 **364/366**（2 条为上述环境变量假失败，去掉后单元测试 12/12 通过）。
- APK：构建号 **4**，372 KB，已发布到网页下载位置；`/api/v1/app/release` 读到 `version_code=4`。
- 本地提交（**未推送**）：`c53b849`（文件夹逐个下载）、`c085181`（按钮规范）、`5aef850`/`bac7c38`（安卓下载 + 校园网提示 + 构建号）、`d1f8119`（上传独立成页）等。
- 网络：电脑在校园网 **JXAU**，`10.200.18.61`（/16，网关 `10.200.255.254`）；防火墙已放行该 exe（Public, TCP+UDP）。校园网**能连**，只是手机自动发现扫不到不同 /24。

## 4. 下一步第一件事（新对话照做）

1. 读本文件 + 项目 `AGENTS.md` + `docs/android.md` 的「2026-10-03 补两件事」小节；
2. 按 §1 的 1→2→3 改 `android/app/src/main/java/com/lanfilehub/share/{ConnectActivity,WebViewActivity,ServerStore}.java` 与 `frontend/src/pages/LoginPage.tsx`；
3. `pwsh -File .\scripts\build-android.ps1`（会自增到构建号 5、跑 JVM 单测、同步到网页下载位置）；
4. 用 `adb install -r` 或手机浏览器覆盖安装验证：**更新后账号与地址保留**（签名一致时原地覆盖）；
5. 前端若改了登录页，`npm run test` + `npm run build`，把 `frontend\dist` 同步到 `portable\app\web`（静态资源不用重启后端）；
6. 本地提交（**不要推送**，用户代理关着）。

## 5. 验证状态（不要谎报）

（2026-10-03 晚更新：本节原文是"三件事都未实现"，现已实现，把实测数字补在后面。）

- 「App 内检查更新」**已实现**（`AppUpdate.java` + `ConnectActivity` 底部按钮）；
  电脑上验过：`AppUpdateTest` **20/20 通过**（版本比较、解析真实响应形状/缺字段/乱码/带花括号的版本名、
  以及"真起一个回环 HTTP 服务 → 读接口 → 判版本 → 给出人话"整条链路：有新版 / 已最新 / 服务器没包 /
  HTTP 500 / 连不上 / 没连过电脑）；`aapt2 dump badging` 读到 `versionCode='5'`；
  **手机上真正点一次「检查更新」→ 下载 → 安装**未验证（本机 `adb devices` 为空）。
- 「地址丢失自愈」**已实现**（地址簿空或全部离线时自动扫，找到就写明"已自动找回上次那台电脑"）；
  **未在真机上制造"清数据"场景验证**。
- 「App 内强制记住登录」**已实现**（`LoginPage` 在 App 内锁定勾选框并显示"在这台手机上保持登录 30 天"）；
  判定逻辑 `rememberLockedInApp` 有单测；前端 **243/243 通过**、`npm run build` 通过，
  `frontend\dist` 已同步到 `portable\app\web`（服务端 index.html 已指向新 bundle）。
- 手机端"覆盖安装不丢数据"**待用户真机复验**（构建号 5 装上后看地址与登录是否还在）。
  签名仍是本机同一 debug 密钥（`CN=Android Debug`，SHA-256 `e73cc5f5…`），可原地覆盖。
- 后端侧不用改也不用重启：运行中的便携包已读到 `version_code=5`、`size_bytes=389841`；
  `GET /api/v1/app/download` 下到的包与磁盘上的 APK **SHA-256 一致**（`4bae249d…`）。
