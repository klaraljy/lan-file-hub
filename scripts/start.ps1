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
# 后端端口以 .env 的 HTTP_PORT 为准：本机 8080 可能落在 Windows 保留端口段里
# （Docker Desktop 预留 7920-8419），脚本里硬编码端口会让"改了 .env 也没用"。
. (Join-Path $PSScriptRoot '_env.ps1')
$BackendPort = Get-BackendPort
$BackendUrl = Get-BackendUrl
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

# 谁在监听这个端口（拿不到就返回 0）。netstat 比 Get-NetTCPConnection 快得多。
function Get-PortOwnerPid([int]$Port) {
  $lines = netstat -ano | Select-String (':' + $Port + '\s')
  foreach ($line in $lines) {
    $parts = ($line.ToString().Trim() -split '\s+')
    if ($parts.Count -ge 5 -and $parts[3] -eq 'LISTENING') { return [int]$parts[4] }
  }
  return 0
}

# 后端健康探活。端口没人听时连接会被立刻拒绝，不会等满超时。
function Test-BackendHealth([int]$TimeoutSec = 2) {
  try {
    return ((Invoke-WebRequest -Uri ($BackendUrl + '/health/live') -UseBasicParsing -TimeoutSec $TimeoutSec).StatusCode -eq 200)
  } catch {
    return $false
  }
}

# 打印后端日志末尾，并给出可执行的原因判断
function Show-BackendFailure {
  $log = Join-Path $env:TEMP 'lfh-backend.log'
  $errLog = Join-Path $env:TEMP 'lfh-backend.err'
  foreach ($file in @($log, $errLog)) {
    if (Test-Path $file) {
      Get-Content $file -Encoding UTF8 -Tail 5 -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host ('      ' + $_) -ForegroundColor DarkGray }
    }
  }
  if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'Bind address failed|EADDRINUSE|已被占用|被系统保留' -Quiet -ErrorAction SilentlyContinue)) {
    Warn '原因：监听端口被占用。先 .\scripts\stop.ps1 清掉旧实例，或改 .env 里的 HTTP_PORT。'
  } else {
    Warn ('完整日志: ' + $log + '（后端程序自身报错、数据库连不上都会写在这里）')
  }
}

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
$pg = ''
$redis = ''
Write-Host -NoNewline '    等待依赖就绪（最多 20 秒）'
for ($i = 0; $i -lt 20; $i++) {
  Start-Sleep -Seconds 1
  $pg = (docker inspect -f '{{.State.Health.Status}}' lan-file-hub-postgres 2>$null | Out-String).Trim()
  $redis = (docker inspect -f '{{.State.Health.Status}}' lan-file-hub-redis 2>$null | Out-String).Trim()
  if ($pg -eq 'healthy' -and $redis -eq 'healthy') { break }
  Write-Host -NoNewline '.'
}
Write-Host ''
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
# 判断"后端是否在运行"只能看**健康探活**，不能看进程名：Drogon 绑定端口失败时不会退出，
# 会留下一个"进程活着、端口没人听"的僵尸进程，只看进程名就会误判成"已在运行"而跳过启动。
Step '启动后端'
$serverExe = Join-Path $ProjectRoot ("build\$preset\bin\lan_file_hub_server.exe")
if (-not (Test-Path $serverExe)) { $serverExe = Join-Path $ProjectRoot 'build\debug\bin\lan_file_hub_server.exe' }

if (Test-BackendHealth -TimeoutSec 2) {
  Ok '后端已在运行且健康，跳过启动'
} elseif (-not (Test-Path $serverExe)) {
  Warn '找不到后端程序：先跑 .\scripts\build.ps1 -Preset debug'
  exit 1
} else {
  # 清理僵尸：占用后端端口却没通过健康检查的进程，以及名字对但没在服务的残留进程
  $stalePids = @()
  $owner = Get-PortOwnerPid $BackendPort
  if ($owner -gt 0) { $stalePids += $owner }
  foreach ($candidate in @(Get-Process -Name 'lan_file_hub_server' -ErrorAction SilentlyContinue)) {
    # 已退出但句柄还没释放的"僵尸"杀不掉也不需要杀（它不占端口），跳过免得误报
    $alive = $false
    try { $alive = -not $candidate.HasExited } catch { $alive = $false }
    if ($alive) { $stalePids += $candidate.Id }
  }
  $stalePids = @($stalePids | Sort-Object -Unique)
  if ($stalePids.Count -gt 0) {
    Warn ('清理未在服务的旧后端进程（pid=' + ($stalePids -join ',') + '）')
    foreach ($stalePid in $stalePids) { Stop-Process -Id $stalePid -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 400
  }

  # 必须重定向输出：否则后台进程会一直持有调用方的输出管道，外层永远等不到脚本结束。
  $serverProc = Start-Process -FilePath $serverExe -WorkingDirectory $ProjectRoot -WindowStyle Minimized -PassThru `
    -RedirectStandardOutput (Join-Path $env:TEMP 'lfh-backend.log') `
    -RedirectStandardError (Join-Path $env:TEMP 'lfh-backend.err')

  # 有界等待 + 可见进度：以前这里是 20 次静默重试（最多 50 秒没有任何输出），
  # 看起来就像卡死。现在最多 10 秒，每 0.5 秒打一个点，失败立刻报原因并中止。
  Write-Host -NoNewline '    等待后端就绪（最多 10 秒）'
  $backendUp = $false
  for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Milliseconds 500
    if ($serverProc -and $serverProc.HasExited) { break }
    if (Test-BackendHealth -TimeoutSec 1) { $backendUp = $true; break }
    Write-Host -NoNewline '.'
  }
  Write-Host ''
  if ($backendUp) {
    Ok ('后端: ' + $BackendUrl + '（日志: %TEMP%\lfh-backend.log）')
  } else {
    Warn '后端没有起来，日志末尾：'
    Show-BackendFailure
    Warn '已中止启动：前端不再启动，修好后回到菜单重按 1 即可'
    exit 1
  }
}

$ready = ''
try { $ready = (Invoke-WebRequest -Uri ($BackendUrl + '/health/ready') -UseBasicParsing -TimeoutSec 5).StatusCode } catch { $ready = '不可用' }
if ($ready -eq 200) { Ok '就绪检查通过（数据库/Redis/存储都可用）' } else { Warn ('就绪检查: ' + $ready + '（打开页面可能报错，看后端日志）') }

# ---- 4) 前端 ----
if (-not $NoFrontend) {
  Step '启动前端开发服务器'
  $viteUp = Test-Port 5173
  if ($viteUp) {
    Ok '前端已在运行（5173），跳过'
  } elseif (-not (Test-Path (Join-Path $ProjectRoot 'frontend\node_modules'))) {
    Warn '前端依赖没装：cd frontend; npm install'
  } else {
    # 必须重定向 stdout/stderr：否则 vite 会一直持有调用方的输出管道，
    # 外层（终端/脚本调用方）会一直等它退出——表现为"启动脚本卡死"。
    # LFH_BACKEND_URL 让 vite 的代理跟着 .env 的端口走，前端不用改配置。
    $env:LFH_BACKEND_URL = $BackendUrl
    # LFH_LAN_URL 让「分享链接」用局域网地址生成：否则在 127.0.0.1 下生成的链接手机打不开
    # （AGENTS.md §14.4，前端读 frontend/src/services/publicUrl.ts）。
    $env:LFH_LAN_URL = Get-LanFrontendUrl
    Start-Process -FilePath 'cmd.exe' -WindowStyle Minimized `
      -WorkingDirectory (Join-Path $ProjectRoot 'frontend') `
      -RedirectStandardOutput (Join-Path $env:TEMP 'lfh-frontend.log') `
      -RedirectStandardError (Join-Path $env:TEMP 'lfh-frontend.err') `
      -ArgumentList '/c', 'npm run dev'
    Write-Host -NoNewline '    等待前端就绪（最多 15 秒）'
    for ($i = 0; $i -lt 15; $i++) {
      Start-Sleep -Seconds 1
      if (Test-Port 5173) { $viteUp = $true; break }
      Write-Host -NoNewline '.'
    }
    Write-Host ''
    if ($viteUp) {
      Ok ('前端: ' + $FrontendUrl + '（日志: %TEMP%\lfh-frontend.log）')
    } else {
      Warn '前端 15 秒内没监听 5173：看 %TEMP%\lfh-frontend.log（后端已经可用了）'
    }
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
# 手机/其它电脑要在同一网络下打开这个地址（前端已监听 0.0.0.0）
$lanUrl = Get-LanFrontendUrl
if ($lanUrl) {
  Write-Host ('     📱 手机访问: ' + $lanUrl + '（需与电脑在同一 WiFi / 热点）') -ForegroundColor Yellow
} else {
  Write-Host '     📱 手机访问: 没检测到局域网 IP（检查网络连接）' -ForegroundColor DarkGray
}
Write-Host '     停止: .\scripts\stop.ps1' -ForegroundColor DarkGray
Write-Host ''

# 必须显式 exit 0：否则 $LASTEXITCODE 会残留上一条原生命令（docker / migrate / netstat）
# 的退出码，调用方（菜单、脚本）会把成功的启动误判成失败。
exit 0
