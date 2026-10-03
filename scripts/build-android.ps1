# 一键构建「局域网快传」安卓 App（五阶段改造 ③）
#
#   .\scripts\build-android.ps1                 # 跑单元测试 + 构建 debug APK（可直接安装）
#   .\scripts\build-android.ps1 -SkipTests      # 跳过测试，只出包
#   .\scripts\build-android.ps1 -Release        # 出 release 包（未签名，需自行签名后才能装）
#   .\scripts\build-android.ps1 -UseProxy       # 依赖下载走本机代理 127.0.0.1:65532
#   .\scripts\build-android.ps1 -PublishOnly    # 不重新构建，只把现有 APK 同步到下载位置
#   .\scripts\build-android.ps1 -SkipPublish    # 只出包，不同步到网页下载位置
#
# 产物：android\lan-file-hub-share.apk（debug 包已用调试密钥签名，能直接装到手机上）
#
# 用户 2026-10-03 要求"改完就更新的"：构建完之后**自动同步**到网页的下载位置，
# 并写出同名 .json 侧车（版本/构建时间/SHA-256），网页「我的」里的卡片读的就是它 ——
# 重新构建一次，页面刷新一下，大小和时间就变了，**不需要重启后端**。
#
# 依赖的工具都在 D:\Codex Tools 下：JDK 21、Android SDK、Gradle 8.9（首次构建会下载 AGP/zxing）。
# 依赖下载**默认直连**（实测可用）；只有直连不通时才加 -UseProxy。
param(
  [switch]$Release,
  [switch]$SkipTests,
  [switch]$UseProxy,
  [switch]$PublishOnly,
  [switch]$SkipPublish
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$AndroidDir = Join-Path $ProjectRoot 'android'
$OutputApk = Join-Path $AndroidDir 'lan-file-hub-share.apk'

# ---- 安装包信息的侧车文件 + 同步到网页下载位置 -------------------------------
# 侧车 JSON 是**给后端读的**（backend/src/application/app_release.cpp）：
# 版本号在 Gradle 配置里，后端不该去解析 build.gradle，所以构建脚本顺手写一份。
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Get-GradleVersion([string]$Key, [string]$Default) {
  # 从 app\build.gradle 里读 versionName / versionCode（不写死在这里）
  $gradleFile = Join-Path $AndroidDir 'app\build.gradle'
  if (-not (Test-Path $gradleFile)) { return $Default }
  $match = Select-String -Path $gradleFile -Pattern ($Key + "\s+'([^']+)'") | Select-Object -First 1
  if ($match) { return $match.Matches[0].Groups[1].Value }
  $match = Select-String -Path $gradleFile -Pattern ($Key + '\s+(\d+)') | Select-Object -First 1
  if ($match) { return $match.Matches[0].Groups[1].Value }
  return $Default
}

function Publish-Apk {
  param([string]$ApkPath)
  if (-not (Test-Path $ApkPath)) {
    Write-Host ('  找不到安装包，跳过同步：' + $ApkPath) -ForegroundColor Yellow
    return
  }

  $info = Get-Item $ApkPath
  $sha = (Get-FileHash -Path $ApkPath -Algorithm SHA256).Hash.ToLower()
  $versionName = Get-GradleVersion 'versionName' '1.0'
  $versionCode = Get-GradleVersion 'versionCode' '1'
  $builtAt = $info.LastWriteTime.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss') + 'Z'

  # 侧车：与 APK 同名 + .json，放在一起（后端按 <apk>.json 找）。
  # file_name 写"这份侧车旁边那个文件的名字"，所以发布位置那份要重写成发布名。
  function Write-Manifest([string]$ManifestPath, [string]$FileName) {
    $manifest = [ordered]@{
      file_name    = $FileName
      version_name = $versionName
      version_code = [int]$versionCode
      size_bytes   = $info.Length
      sha256       = $sha
      built_at     = $builtAt
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText($ManifestPath, $manifest, $Utf8NoBom)
  }

  Write-Manifest ($ApkPath + '.json') $info.Name

  Write-Host ''
  Write-Host '  📦 安装包信息（网页「我的」里显示的就是这些）' -ForegroundColor Cyan
  Write-Host ('     版本：' + $versionName + '（versionCode ' + $versionCode + '）') -ForegroundColor Gray
  Write-Host ('     大小：' + [Math]::Round($info.Length / 1KB, 0) + ' KB') -ForegroundColor Gray
  Write-Host ('     构建：' + $builtAt) -ForegroundColor Gray
  Write-Host ('     校验：' + $sha.Substring(0, 16) + '…') -ForegroundColor DarkGray

  # 同步到便携包的下载目录（后端默认就是从这里读：WEB_ROOT/downloads/lan-file-hub.apk）
  $downloadDir = Join-Path $ProjectRoot 'portable\app\web\downloads'
  $publishedName = 'lan-file-hub.apk'
  if (-not $SkipPublish) {
    if (Test-Path (Join-Path $ProjectRoot 'portable\app\web')) {
      if (-not (Test-Path $downloadDir)) { New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null }
      Copy-Item $ApkPath (Join-Path $downloadDir $publishedName) -Force
      Write-Manifest (Join-Path $downloadDir ($publishedName + '.json')) $publishedName
      Write-Host ('  ✅ 已同步到网页下载位置：' + (Join-Path $downloadDir $publishedName)) -ForegroundColor Green

      # 旧版本是手工丢在 web 根目录的（2026-10-03 之前），现在统一走 downloads\ 这一处
      $legacy = Join-Path $ProjectRoot 'portable\app\web\lan-file-hub.apk'
      if (Test-Path $legacy) {
        Remove-Item $legacy -Force
        Write-Host '     已清理旧的 web\lan-file-hub.apk（改由 downloads\ 统一提供）' -ForegroundColor DarkGray
      }
    } else {
      Write-Host '  便携包目录不存在，跳过同步（只更新了 android\ 下的产物）' -ForegroundColor DarkGray
    }
  }
}

# ---- 工具位置（脚本里集中写，不散落到别处）----
if (-not $env:JAVA_HOME -and (Test-Path 'D:\Codex Tools\jdk-21')) {
  $env:JAVA_HOME = 'D:\Codex Tools\jdk-21'
}
if (-not $env:ANDROID_HOME -and (Test-Path 'D:\Codex Tools\Android')) {
  $env:ANDROID_HOME = 'D:\Codex Tools\Android'
}
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:GRADLE_USER_HOME = 'D:\Codex Tools\gradle\home'

$gradleBat = Get-ChildItem 'D:\Codex Tools\gradle\gradle-*\bin\gradle.bat' -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1
if (-not $gradleBat) {
  Write-Host '找不到 Gradle：请把 gradle-8.9 解压到 D:\Codex Tools\gradle\' -ForegroundColor Red
  exit 1
}

Write-Host ''
Write-Host '  局域网快传 · 安卓构建' -ForegroundColor White
Write-Host ('  Gradle : ' + $gradleBat.FullName) -ForegroundColor DarkGray
Write-Host ('  JDK    : ' + $env:JAVA_HOME) -ForegroundColor DarkGray
Write-Host ('  SDK    : ' + $env:ANDROID_HOME) -ForegroundColor DarkGray
Write-Host ''

# ---- 只同步（不重新构建）：换机器 / 只想把现有 APK 放到下载位置时用 -------------
if ($PublishOnly) {
  Write-Host ''
  Write-Host '  只同步安装包（-PublishOnly：不重新构建）' -ForegroundColor White
  Publish-Apk -ApkPath $OutputApk
  Write-Host ''
  exit 0
}

# ---- local.properties（Gradle 要求写死 SDK 路径；这个文件不入库）----
$localProperties = Join-Path $AndroidDir 'local.properties'
$sdkEscaped = $env:ANDROID_HOME -replace '\\', '\\' -replace ':', '\:'
[System.IO.File]::WriteAllText($localProperties, ("sdk.dir=" + $sdkEscaped + "`n"), $Utf8NoBom)

Push-Location $AndroidDir
try {
  # 需要走代理时才把代理参数传给 Gradle（写死在 gradle.properties 里会导致"代理一关就构建失败"）
  $gradleArgs = @('--no-daemon')
  if ($UseProxy) {
    $gradleArgs += @('-Dhttp.proxyHost=127.0.0.1', '-Dhttp.proxyPort=65532',
                     '-Dhttps.proxyHost=127.0.0.1', '-Dhttps.proxyPort=65532')
    Write-Host '  （依赖下载走本机代理）' -ForegroundColor DarkGray
  }

  if (-not $SkipTests) {
    Write-Host '==> 跑内置 HTTP 服务的单元测试' -ForegroundColor Cyan
    & $gradleBat @gradleArgs -q testDebugUnitTest
    if ($LASTEXITCODE -ne 0) { Write-Host '单元测试失败' -ForegroundColor Red; exit 1 }
    Write-Host '    测试通过' -ForegroundColor Green
  }

  $task = if ($Release) { 'assembleRelease' } else { 'assembleDebug' }
  Write-Host ('==> 构建 APK（' + $task + '）') -ForegroundColor Cyan
  & $gradleBat @gradleArgs $task
  if ($LASTEXITCODE -ne 0) { Write-Host '构建失败' -ForegroundColor Red; exit 1 }

  $variant = if ($Release) { 'release' } else { 'debug' }
  $built = Get-ChildItem (Join-Path $AndroidDir "app\build\outputs\apk\$variant\*.apk") -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $built) { Write-Host '没有找到 APK 产物' -ForegroundColor Red; exit 1 }

  Copy-Item $built.FullName $OutputApk -Force
  Write-Host ''
  Write-Host ('  ✅ 完成：' + $OutputApk) -ForegroundColor Green
  Write-Host ('     体积：' + [Math]::Round((Get-Item $OutputApk).Length / 1MB, 2) + ' MB') -ForegroundColor Green
  Write-Host '     安装：把 APK 传到手机点开安装，或用 adb install -r' -ForegroundColor DarkGray
  # 关键一步（用户 2026-10-03"改完就更新的"）：写侧车 + 同步到网页下载位置
  Publish-Apk -ApkPath $OutputApk
  Write-Host ''
  Write-Host '     网页上：登录后进「我的」→「配套手机应用」，刷新一下就能看到新的大小与时间' -ForegroundColor DarkGray
  Write-Host ''
} finally {
  Pop-Location
}
