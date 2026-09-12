# 一键启动 lan-file-hub（阶段十六产物）
#
#   .\scripts\start.ps1                 # 起依赖 + 迁移 + 后端 + 前端，然后打开浏览器
#   .\scripts\start.ps1 -Release        # 后端用 release 构建（更快；没构建过会自动回退到 debug）
#   .\scripts\start.ps1 -NoBrowser      # 不自动开浏览器
#   .\scripts\start.ps1 -NoFrontend     # 只起后端（前端用别的方式跑）
#
# 桌面上有对应的「启动 lan-file-hub.cmd」，双击即可。
# 停止：.\scripts\stop.ps1
param(
  [switch]$Release,
  [switch]$NoBrowser,
  [switch]$NoFrontend,
  # 默认不阻塞等待 Docker 引擎（冷启动可能 1–3 分钟）；需要脚本内等待就加上
  [switch]$WaitForDocker
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BackendUrl = 'http://127.0.0.1:8080'
$FrontendUrl = 'http://127.0.0.1:5173'

# 快速 TCP 探测：Test-NetConnection 在端口未监听时要等 20-30 秒（会把启动脚本拖成十分钟）
function Test-Port([int]$Port, [int]$TimeoutMs = 500) {
  $client = New-Object System.Net.Sockets.TcpClient
  try {
    $task = $client.ConnectAsync('127.0.0.1', $Port)
    if ($task.Wait($TimeoutMs)) { return $client.Connected }
    return $false
  } catch {
    return $false
  } finally {
    $client.Close()
  }
}
function Step([string]$Text) { Write-Host ('==> ' + $Text) -ForegroundColor Cyan }
function Ok([string]$Text) { Write-Host ('    ' + $Text) -ForegroundColor Green }
function Warn([string]$Text) { Write-Host ('    ' + $Text) -ForegroundColor Yellow }

Set-Location $ProjectRoot
Write-Host ''
Write-Host '  lan-file-hub 一键启动' -ForegroundColor White
Write-Host ('  项目目录: ' + $ProjectRoot) -ForegroundColor DarkGray
Write-Host ''

# ---- 1) 依赖服务（PostgreSQL / Redis）----
Step '检查依赖服务（Docker: postgres + redis）'

function Test-DockerEngine {
  # 有界探测：docker 命令在引擎没起时可能卡住，所以放进带超时的后台任务
  $job = Start-Job { docker version --format '{{.Server.Version}}' 2>$null }
  $done = Wait-Job $job -Timeout 10
  $ver = ''
  if ($done) { $ver = (Receive-Job $job 2>$null | Out-String).Trim() }
  Remove-Job $job -Force -ErrorAction SilentlyContinue
  return ($done -and $ver -match '^\d')
}

if (-not (Test-DockerEngine)) {
  Warn 'Docker 引擎没在运行，正在尝试启动 Docker Desktop…'
  $desktopExe = @(
    'D:\Codex Tools\Docker\Docker Desktop.exe',
    "$env:ProgramFiles\Docker\Docker\Docker Desktop.exe"
  ) | Where-Object { Test-Path $_ } | Select-Object -First 1
  if ($desktopExe) {
    # 只负责拉起，不阻塞等待：Docker Desktop 冷启动可能要 1–3 分钟，
    # 让脚本干等既浪费时间也看不出进度。
    Start-Process $desktopExe | Out-Null
    if ($WaitForDocker) {
      Write-Host -NoNewline '    等待引擎就绪'
      $ready = $false
      for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Seconds 3
        Write-Host -NoNewline '.'
        if (Test-DockerEngine) { $ready = $true; break }
      }
      Write-Host ''
      if (-not $ready) { Warn '等待超时：请稍后重试'; exit 1 }
      Ok 'Docker 引擎已就绪'
    } else {
      Warn 'Docker Desktop 正在启动（首次通常 1–3 分钟）。'
      Warn '等它的状态栏变绿后，回到菜单再按一次 1 即可（想让它自己等就加 -WaitForDocker）。'
      exit 0
    }
  } else {
    Warn '找不到 Docker Desktop.exe：请手动启动 Docker Desktop 后重试'
    exit 1
  }
}
docker compose up -d postgres redis 2>&1 | Out-Null
for ($i = 0; $i -lt 30; $i++) {
  Start-Sleep -Seconds 1
  $pg = (docker inspect -f '{{.State.Health.Status}}' lan-file-hub-postgres 2>$null | Out-String).Trim()
  $redis = (docker inspect -f '{{.State.Health.Status}}' lan-file-hub-redis 2>$null | Out-String).Trim()
  if ($pg -eq 'healthy' -and $redis -eq 'healthy') { break }
}
Ok ('postgres=' + $pg + '  redis=' + $redis)
if ($pg -ne 'healthy' -or $redis -ne 'healthy') { Warn '依赖服务未就绪，后面的启动可能失败' }

# ---- 2) 数据库迁移（幂等）----
Step '执行数据库迁移（已应用的会跳过）'
$preset = if ($Release) { 'release' } else { 'debug' }
$migrateExe = Join-Path $ProjectRoot ("build\$preset\bin\lan_file_hub_migrate.exe")
if (-not (Test-Path $migrateExe)) { $migrateExe = Join-Path $ProjectRoot 'build\debug\bin\lan_file_hub_migrate.exe' }
if (Test-Path $migrateExe) {
  & $migrateExe --migrations (Join-Path $ProjectRoot 'migrations') --env-file (Join-Path $ProjectRoot '.env') 2>&1 |
    Select-Object -Last 2 | ForEach-Object { Ok $_ }
} else {
  Warn '找不到迁移工具：先跑 .\scripts\build.ps1 -Preset debug'
}

# ---- 3) 后端 ----
Step '启动后端'
$running = Get-Process -Name 'lan_file_hub_server' -ErrorAction SilentlyContinue
if ($running) {
  Ok ('后端已在运行（pid=' + ($running | Select-Object -First 1).Id + '），跳过')
} else {
  $serverExe = Join-Path $ProjectRoot ("build\$preset\bin\lan_file_hub_server.exe")
  if (-not (Test-Path $serverExe)) { $serverExe = Join-Path $ProjectRoot 'build\debug\bin\lan_file_hub_server.exe' }
  if (-not (Test-Path $serverExe)) {
    Warn '找不到后端程序：先跑 .\scripts\build.ps1 -Preset debug'
    exit 1
  }
  Start-Process -FilePath $serverExe -WorkingDirectory $ProjectRoot -WindowStyle Minimized `
    -RedirectStandardOutput (Join-Path $env:TEMP 'lfh-backend.log') `
    -RedirectStandardError (Join-Path $env:TEMP 'lfh-backend.err')
  for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Milliseconds 500
    try { if ((Invoke-WebRequest -Uri ($BackendUrl + '/health/live') -UseBasicParsing -TimeoutSec 2).StatusCode -eq 200) { break } } catch { }
  }
  Ok ('后端: ' + $BackendUrl + '（日志: %TEMP%\lfh-backend.log）')
}

$ready = ''
try { $ready = (Invoke-WebRequest -Uri ($BackendUrl + '/health/ready') -UseBasicParsing -TimeoutSec 5).StatusCode } catch { $ready = '不可用' }
if ($ready -eq 200) { Ok '就绪检查通过（数据库/Redis/存储都可用）' } else { Warn ('就绪检查: ' + $ready + '（打开页面可能报错，看后端日志）') }

# ---- 4) 前端 ----
if (-not $NoFrontend) {
  Step '启动前端开发服务器'
  $viteUp = $false
  $viteUp = Test-Port 5173
  if ($viteUp) {
    Ok '前端已在运行（5173），跳过'
  } elseif (-not (Test-Path (Join-Path $ProjectRoot 'frontend\node_modules'))) {
    Warn '前端依赖没装：cd frontend; npm install'
  } else {
    # 必须重定向 stdout/stderr：否则 vite 会一直持有调用方的输出管道，
    # 外层（终端/脚本调用方）会一直等它退出——表现为"启动脚本卡死"。
    Start-Process -FilePath 'cmd.exe' -WindowStyle Minimized `
      -WorkingDirectory (Join-Path $ProjectRoot 'frontend') `
      -RedirectStandardOutput (Join-Path $env:TEMP 'lfh-frontend.log') `
      -RedirectStandardError (Join-Path $env:TEMP 'lfh-frontend.err') `
      -ArgumentList '/c', 'npm run dev'
    for ($i = 0; $i -lt 30; $i++) {
      Start-Sleep -Seconds 1
      if (Test-Port 5173) { break }
    }
    Ok ('前端: ' + $FrontendUrl + '（日志: %TEMP%\lfh-frontend.log）')
  }
}

# ---- 5) 打开浏览器 ----
$open = if ($NoFrontend) { $BackendUrl } else { $FrontendUrl }
if (-not $NoBrowser) {
  Step ('打开浏览器: ' + $open)
  Start-Process $open | Out-Null
}

Write-Host ''
Write-Host '  ✅ 启动完成' -ForegroundColor Green
Write-Host ('     前端: ' + $FrontendUrl + '    后端: ' + $BackendUrl) -ForegroundColor White
Write-Host ('     指标: ' + $BackendUrl + '/metrics     健康: ' + $BackendUrl + '/health/ready') -ForegroundColor DarkGray
Write-Host '     停止: .\scripts\stop.ps1' -ForegroundColor DarkGray
Write-Host ''
