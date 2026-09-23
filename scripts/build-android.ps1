# 一键构建「局域网快传」安卓 App（五阶段改造 ③）
#
#   .\scripts\build-android.ps1                 # 跑单元测试 + 构建 debug APK（可直接安装）
#   .\scripts\build-android.ps1 -SkipTests      # 跳过测试，只出包
#   .\scripts\build-android.ps1 -Release        # 出 release 包（未签名，需自行签名后才能装）
#   .\scripts\build-android.ps1 -UseProxy       # 依赖下载走本机代理 127.0.0.1:65532
#
# 产物：android\lan-file-hub-share.apk（debug 包已用调试密钥签名，能直接装到手机上）
#
# 依赖的工具都在 D:\Codex Tools 下：JDK 21、Android SDK、Gradle 8.9（首次构建会下载 AGP/zxing）。
# 依赖下载**默认直连**（实测可用）；只有直连不通时才加 -UseProxy。
param(
  [switch]$Release,
  [switch]$SkipTests,
  [switch]$UseProxy
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$AndroidDir = Join-Path $ProjectRoot 'android'
$OutputApk = Join-Path $AndroidDir 'lan-file-hub-share.apk'

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

# ---- local.properties（Gradle 要求写死 SDK 路径；这个文件不入库）----
$localProperties = Join-Path $AndroidDir 'local.properties'
$sdkEscaped = $env:ANDROID_HOME -replace '\\', '\\' -replace ':', '\:'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($localProperties, ("sdk.dir=" + $sdkEscaped + "`n"), $utf8NoBom)

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
  Write-Host ''
} finally {
  Pop-Location
}
