# 便携包准备（五阶段改造 ④）：一键把"免 Docker、免 Node"的可分发包准备好。
#
#   .\scripts\portable-setup.ps1                       # 全量准备（含下载便携 PostgreSQL ≈330MB）
#   .\scripts\portable-setup.ps1 -SkipDownload         # 已经下载过，直接用缓存
#   .\scripts\portable-setup.ps1 -Force                # 重新初始化数据库（**会清空便携包里的数据**）
#   .\scripts\portable-setup.ps1 -AdminUser me -AdminEmail me@example.com -AdminPasswordEnv LFH_ADMIN_PW
#
# 做哪些事：
#   1. 下载并解压**免安装 PostgreSQL**（EDB 官方 binaries zip）到 portable\pgsql
#   2. 拷贝 Redis for Windows 到 portable\redis
#   3. 拷贝后端 release 程序 / 迁移工具 / migrations / 前端 dist 到 portable\app（前端放 app\web）
#   4. 生成 portable\app\.env：**随机 JWT 密钥与随机数据库口令**，端口避开开发环境
#   5. initdb + 启动 PostgreSQL + 建库 + 跑迁移 + 建管理员
#   6. 拷贝托盘脚本到 portable\tools
#
# 端口约定（**刻意避开开发环境**，两者可以同时跑）：
#   便携包：HTTP_PORT=9180、PostgreSQL=55432、Redis=56379
#   开发环境：9080 / 5432 / 6379
param(
  [string]$PortableDir = 'portable',
  [switch]$SkipDownload,
  [switch]$Force,
  [string]$AdminUser = 'admin',
  [string]$AdminEmail = 'admin@lan.local',
  [string]$AdminPasswordEnv = 'LFH_ADMIN_PASSWORD',
  # 不传就自己生成一个随机引导口令并打印一次
  [switch]$NonInteractive
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$TargetRoot = if ([System.IO.Path]::IsPathRooted($PortableDir)) { $PortableDir } else { Join-Path $ProjectRoot $PortableDir }
$TmpDir = 'E:\DeepSeekProjects\_tmp\lan-file-hub'
$ProxyUrl = 'http://127.0.0.1:65532'
$PgZipUrl = 'https://get.enterprisedb.com/postgresql/postgresql-16.4-1-windows-x64-binaries.zip'
$PgZip = Join-Path $TmpDir 'postgresql-windows-x64-binaries.zip'
# 该 zip 的 Content-Length（curl -I 实测）：用来判断下载是否完整、并支持断点续传
$PgZipSize = 338727828

$HttpPort = 9180
$PgPort = 55432
$RedisPort = 56379
$DbName = 'lanfilehub'
$DbUser = 'lanfilehub'

function Step([string]$Text) { Write-Host ('==> ' + $Text) -ForegroundColor Cyan }
function Ok([string]$Text) { Write-Host ('    ' + $Text) -ForegroundColor Green }
function Warn([string]$Text) { Write-Host ('    ' + $Text) -ForegroundColor Yellow }

function Test-Port([int]$Port, [int]$TimeoutMs = 500) {
  $client = New-Object System.Net.Sockets.TcpClient
  try {
    $task = $client.ConnectAsync('127.0.0.1', $Port)
    if ($task.Wait($TimeoutMs)) { return $client.Connected }
    return $false
  } catch { return $false } finally { $client.Close() }
}

function Get-LanAddress {
  $candidates = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
    Where-Object { $_.InterfaceAlias -notlike '*VMware*' -and $_.InterfaceAlias -notlike '*vEthernet*' -and $_.InterfaceAlias -notlike '本地连接*' }
  $preferred = $candidates | Where-Object { $_.PrefixOrigin -ne 'WellKnown' } | Select-Object -First 1
  if ($preferred) { return $preferred.IPAddress }
  if ($candidates) { return ($candidates | Select-Object -First 1).IPAddress }
  return ''
}

function New-RandomSecret([int]$Bytes = 32) {
  $buffer = New-Object byte[] $Bytes
  $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
  $rng.GetBytes($buffer)
  $rng.Dispose()
  return [Convert]::ToBase64String($buffer).Replace('+', 'A').Replace('/', 'B').Replace('=', 'C')
}

Write-Host ''
Write-Host '  便携包准备（免 Docker / 免 Node）' -ForegroundColor White
Write-Host ('  目标目录: ' + $TargetRoot) -ForegroundColor DarkGray
Write-Host ''

New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null
foreach ($dir in @('app', 'app\web', 'app\migrations', 'pgsql', 'redis', 'data', 'data\redis', 'logs', 'tools')) {
  New-Item -ItemType Directory -Force -Path (Join-Path $TargetRoot $dir) | Out-Null
}

# ---- 1) 便携 PostgreSQL -------------------------------------------------------
Step '准备免安装 PostgreSQL'
$PgCtl = Join-Path $TargetRoot 'pgsql\bin\pg_ctl.exe'
if ((Test-Path $PgCtl) -and -not $Force) {
  Ok '已有 pgsql\，跳过下载'
} else {
  $ZipComplete = (Test-Path $PgZip) -and ((Get-Item $PgZip).Length -eq $PgZipSize)
  if ($ZipComplete) {
    Ok ('使用已下载的完整压缩包：' + [Math]::Round($PgZipSize / 1MB, 1) + ' MB')
  } elseif ($SkipDownload) {
    Warn ('要求跳过下载，但压缩包不完整（' + $(if (Test-Path $PgZip) { [Math]::Round((Get-Item $PgZip).Length / 1MB, 1) } else { 0 }) + ' MB / 预计 ' + [Math]::Round($PgZipSize / 1MB, 1) + ' MB）')
    exit 1
  } else {
    if (Test-Path $PgZip) {
      Warn ('发现未下完的压缩包（' + [Math]::Round((Get-Item $PgZip).Length / 1MB, 1) + ' MB），将**断点续传**')
    } else {
      Write-Host ('    下载 ' + $PgZipUrl) -ForegroundColor DarkGray
    }
    Write-Host '    （约 330 MB；每 5 秒报一次进度；网络抖动自动重试，代理失败自动改直连）' -ForegroundColor DarkGray

    # 先走代理，失败再直连；`-C -` 让中断后接着下，不用从 0 开始
    $attempts = @(
      @{ Name = '走代理'; Args = @('-L', '--fail', '-C', '-', '--retry', '5', '--retry-all-errors', '--retry-delay', '2', '--proxy', $ProxyUrl, '-o', $PgZip, $PgZipUrl) },
      @{ Name = '直连';   Args = @('-L', '--fail', '-C', '-', '--retry', '5', '--retry-all-errors', '--retry-delay', '2', '-o', $PgZip, $PgZipUrl) }
    )
    $downloaded = $false
    foreach ($attempt in $attempts) {
      if ((Test-Path $PgZip) -and ((Get-Item $PgZip).Length -eq $PgZipSize)) { $downloaded = $true; break }
      Write-Host ('    尝试' + $attempt.Name + '…') -ForegroundColor DarkGray
      $curl = Start-Process -FilePath 'curl.exe' -PassThru -WindowStyle Hidden -ArgumentList $attempt.Args
      $lastReport = -1
      while (-not $curl.HasExited) {
        Start-Sleep -Seconds 5
        if (Test-Path $PgZip) {
          $mb = [Math]::Round((Get-Item $PgZip).Length / 1MB, 1)
          if ($mb - $lastReport -ge 20) { Write-Host ('    已下载 ' + $mb + ' MB / ' + [Math]::Round($PgZipSize / 1MB, 1) + ' MB'); $lastReport = $mb }
        }
      }
      if ((Test-Path $PgZip) -and ((Get-Item $PgZip).Length -eq $PgZipSize)) {
        $downloaded = $true
      } else {
        Warn ($attempt.Name + '没下完（curl 退出码 ' + $curl.ExitCode + '，当前 ' + $(if (Test-Path $PgZip) { [Math]::Round((Get-Item $PgZip).Length / 1MB, 1) } else { 0 }) + ' MB）')
      }
    }

    if (-not $downloaded) {
      Warn ('下载仍未完成。可以：1) 稍后重跑本脚本（会断点续传）；2) 手动下载后放到 ' + $PgZip + ' 再跑 -SkipDownload；3) 暂时继续用 Docker 模式')
      exit 1
    }
    Ok ('下载完成：' + [Math]::Round((Get-Item $PgZip).Length / 1MB, 1) + ' MB')
  }

  Step '解压 PostgreSQL'
  $raw = Join-Path $TmpDir 'pg-raw'
  if (Test-Path $raw) { Remove-Item $raw -Recurse -Force }
  Expand-Archive -Path $PgZip -DestinationPath $raw -Force
  $inner = Join-Path $raw 'pgsql'
  if (-not (Test-Path $inner)) { Warn '压缩包里没有 pgsql 目录，解压结构可能变了'; exit 1 }
  if (Test-Path (Join-Path $TargetRoot 'pgsql')) { Remove-Item (Join-Path $TargetRoot 'pgsql') -Recurse -Force }
  Move-Item $inner (Join-Path $TargetRoot 'pgsql')
  Ok ('pgsql\bin\pg_ctl.exe 存在: ' + (Test-Path $PgCtl))
}

# ---- 2) Redis ----------------------------------------------------------------
Step '准备 Redis for Windows'
$RedisSource = 'D:\Codex Tools\Redis\redis-server.exe'
$RedisTarget = Join-Path $TargetRoot 'redis\redis-server.exe'
if (Test-Path $RedisSource) {
  Copy-Item $RedisSource $RedisTarget -Force
  Ok ('已拷贝 redis-server.exe（' + [Math]::Round((Get-Item $RedisTarget).Length / 1KB) + ' KB）')
} else {
  Warn ('本机没有 ' + $RedisSource + '：便携包将不带 Redis（后端会提示 Redis 未配置，核心功能不受影响）')
}

# ---- 3) 拷贝程序与前端 --------------------------------------------------------
Step '拷贝后端程序、迁移工具与前端产物'
$ReleaseBin = Join-Path $ProjectRoot 'build\release\bin'
$serverSrc = Join-Path $ReleaseBin 'lan_file_hub_server.exe'
if (-not (Test-Path $serverSrc)) { $serverSrc = Join-Path $ProjectRoot 'build\debug\bin\lan_file_hub_server.exe' }
$migrateSrc = Join-Path $ReleaseBin 'lan_file_hub_migrate.exe'
if (-not (Test-Path $migrateSrc)) { $migrateSrc = Join-Path $ProjectRoot 'build\debug\bin\lan_file_hub_migrate.exe' }
if (-not (Test-Path $serverSrc)) { Warn '找不到后端程序：先跑 .\scripts\build.ps1 -Preset release'; exit 1 }
if (-not (Test-Path $migrateSrc)) { Warn '找不到迁移工具：先跑 .\scripts\build.ps1 -Preset release'; exit 1 }
Copy-Item $serverSrc (Join-Path $TargetRoot 'app\lan_file_hub_server.exe') -Force
Copy-Item $migrateSrc (Join-Path $TargetRoot 'app\lan_file_hub_migrate.exe') -Force
# ⚠️ 必须连**运行库 DLL** 一起拷：只拷 exe 会以退出码 -1073741515（STATUS_DLL_NOT_FOUND）失败，
#    表现为"迁移失败 / 建管理员失败"。构建的 post-build 步骤（vcpkg z-applocal）已经把需要的
#    DLL 放在同一个 bin\ 目录里了，整目录拷过来即可。
Get-ChildItem -Path $ReleaseBin -Filter '*.dll' -ErrorAction SilentlyContinue |
  Copy-Item -Destination (Join-Path $TargetRoot 'app') -Force
Copy-Item (Join-Path $ProjectRoot 'migrations\*') (Join-Path $TargetRoot 'app\migrations') -Recurse -Force
$dllCount = @(Get-ChildItem -Path (Join-Path $TargetRoot 'app') -Filter '*.dll' -ErrorAction SilentlyContinue).Count
Ok ('后端 ' + [Math]::Round((Get-Item (Join-Path $TargetRoot 'app\lan_file_hub_server.exe')).Length / 1MB, 1) + ' MB + 运行库 ' + $dllCount + ' 个 DLL，迁移脚本已拷贝')

$DistDir = Join-Path $ProjectRoot 'frontend\dist'
if (Test-Path (Join-Path $DistDir 'index.html')) {
  Copy-Item (Join-Path $DistDir '*') (Join-Path $TargetRoot 'app\web') -Recurse -Force
  Ok '前端 dist 已拷到 app\web'
} else {
  Warn '还没有前端产物：先在 frontend 里跑 npm run build（便携包网页托管需要它）'
}

foreach ($script in @('tray.ps1', '_env.ps1')) {
  Copy-Item (Join-Path $ProjectRoot ('scripts\' + $script)) (Join-Path $TargetRoot ('tools\' + $script)) -Force
}
Ok '托盘脚本已拷到 tools\'

# ---- 4) 生成 .env -------------------------------------------------------------
Step '生成配置（随机密钥，端口避开开发环境）'
$EnvPath = Join-Path $TargetRoot 'app\.env'
$PgData = Join-Path $TargetRoot 'data\pg'
$keepPassword = $null
if ((Test-Path $EnvPath) -and -not $Force) {
  # 保留已有口令，避免重跑把数据库口令改掉导致连不上
  foreach ($line in (Get-Content $EnvPath -Encoding UTF8 -ErrorAction SilentlyContinue)) {
    if ($line -match '^\s*DATABASE_URL\s*=.*://[^:]+:([^@]+)@') { $keepPassword = $Matches[1] }
  }
}
$DbPassword = if ($keepPassword) { $keepPassword } else { New-RandomSecret 24 }
$JwtSecret = New-RandomSecret 48
$LanAddress = Get-LanAddress
$corsOrigins = 'http://localhost:' + $HttpPort + ',http://127.0.0.1:' + $HttpPort
if ($LanAddress) { $corsOrigins += ',http://' + $LanAddress + ':' + $HttpPort }

$envLines = @(
  '# 便携包配置（由 scripts\portable-setup.ps1 生成，含随机密钥；**不要提交到 Git**）',
  'APP_ENV=production',
  ('HTTP_PORT=' + $HttpPort),
  'HTTP_THREADS=4',
  'LOG_LEVEL=info',
  ('WEB_ROOT=web'),
  ('DATABASE_URL=postgresql://' + $DbUser + ':' + $DbPassword + '@127.0.0.1:' + $PgPort + '/' + $DbName),
  ('REDIS_URL=redis://127.0.0.1:' + $RedisPort),
  'STORAGE_ROOT=data/storage',
  ('JWT_SECRET=' + $JwtSecret),
  ('CORS_ALLOWED_ORIGINS=' + $corsOrigins),
  ('PGPORT=' + $PgPort),
  ('REDISPORT=' + $RedisPort),
  'MAX_FILE_SIZE_BYTES=10737418240',
  'DEFAULT_CHUNK_SIZE_BYTES=8388608',
  'MIN_FREE_DISK_BYTES=1073741824'
)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($EnvPath, ($envLines -join "`n") + "`n", $utf8NoBom)
Ok ('.env 已写入（随机 JWT 密钥与数据库口令，未提交 Git）')

# ---- 5) 初始化数据库 ----------------------------------------------------------
Step '初始化数据库（initdb + 建库 + 迁移）'
$PgCtlPath = Join-Path $TargetRoot 'pgsql\bin\pg_ctl.exe'
$InitDb = Join-Path $TargetRoot 'pgsql\bin\initdb.exe'
$Psql = Join-Path $TargetRoot 'pgsql\bin\psql.exe'
if (-not (Test-Path $InitDb)) { Warn '找不到 initdb，PostgreSQL 没准备好'; exit 1 }

if ($Force -and (Test-Path $PgData)) {
  Warn '-Force：先停掉旧实例并清空 data\pg（便携包数据会丢，这是你要的）'
  if (Test-Path $PgCtlPath) { & $PgCtlPath -D $PgData stop -m immediate *> (Join-Path $TargetRoot 'logs\pg-stop.log') }
  Remove-Item $PgData -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path (Join-Path $PgData 'PG_VERSION'))) {
  $pwFile = Join-Path $TmpDir 'pg-pw.txt'
  [System.IO.File]::WriteAllText($pwFile, $DbPassword, $utf8NoBom)
  & $InitDb -D $PgData -U $DbUser --pwfile=$pwFile --auth-local=trust --auth-host=scram-sha-256 -E UTF8 *> (Join-Path $TargetRoot 'logs\initdb.log')
  Remove-Item $pwFile -Force -ErrorAction SilentlyContinue
  if (-not (Test-Path (Join-Path $PgData 'PG_VERSION'))) { Warn ('initdb 失败，看 ' + (Join-Path $TargetRoot 'logs\initdb.log')); exit 1 }
  Ok 'initdb 完成'
} else {
  Ok 'data\pg 已存在，跳过 initdb'
}

# ⛔ 端口**必须写进 postgresql.conf**：`-o '-p N'` 只对**这一次**启动有效，
#    之后任何不带 -o 的启动（手动 pg_ctl、托盘、照抄命令的人）都会退回默认 5432，
#    而后端 .env 连的是 PGPORT → 表现为「数据库明明起来了，后端却报 connection refused」
#    （2026-09-21 实测踩到：便携库跑在 5432，后端连 55432 失败）。
$PgConf = Join-Path $PgData 'postgresql.conf'
if (Test-Path $PgConf) {
  $confText = Get-Content $PgConf -Raw -Encoding UTF8
  if ($confText -notmatch ('(?m)^\s*port\s*=\s*' + $PgPort + '\s*$')) {
    Add-Content -Path $PgConf -Encoding UTF8 -Value (
      "`r`n# --- 便携包：固定端口（scripts\portable-setup.ps1 写入；改端口要同时改 app\.env 的 PGPORT）---`r`n" +
      'port = ' + $PgPort + "`r`n" + "listen_addresses = '127.0.0.1'`r`n")
    Ok ('已把端口 ' + $PgPort + ' 写入 postgresql.conf（避免退回 5432）')
  } else {
    Ok ('postgresql.conf 里的端口已是 ' + $PgPort)
  }
}

if (-not (Test-Port $PgPort)) {
  # ⛔ 这里**既不能用 `| Out-Null`、也不能用 `*>`**：pg_ctl 会派生出 postgres 子进程，子进程继承那个
  # 管道/重定向句柄，PowerShell 等不到 EOF → 脚本永久卡在这一行（实测踩到过，AGENTS.md §9 记着这个坑）。
  # 用 Start-Process 重定向到文件，再轮询端口判断就绪。
  # ⛔ -ArgumentList 传数组时不会给含空格的元素加引号，`-o '-p N -h 127.0.0.1'` 会被拆开，
  #    pg_ctl 报「无效的参数模式」拒绝启动 —— 必须拼成一整条带引号的字符串。
  $pgStartArgs = '-D "' + $PgData + '" -l "' + (Join-Path $TargetRoot 'logs\postgres.log') + '" -o "-p ' + $PgPort + ' -h 127.0.0.1" start'
  Start-Process -FilePath $PgCtlPath -WindowStyle Hidden -ArgumentList $pgStartArgs -RedirectStandardOutput (Join-Path $TargetRoot 'logs\pg-start.log') -RedirectStandardError (Join-Path $TargetRoot 'logs\pg-start.err') | Out-Null
  for ($i = 0; $i -lt 40 -and -not (Test-Port $PgPort); $i++) { Start-Sleep -Milliseconds 500 }
}
if (-not (Test-Port $PgPort)) { Warn ('PostgreSQL 没起来，看 ' + (Join-Path $TargetRoot 'logs\postgres.log')); exit 1 }
Ok ('PostgreSQL 在 127.0.0.1:' + $PgPort + ' 运行中')

$env:PGPASSWORD = $DbPassword
$exists = (& $Psql -h 127.0.0.1 -p $PgPort -U $DbUser -d postgres -tAc ("SELECT 1 FROM pg_database WHERE datname='" + $DbName + "'") 2>&1 | Out-String).Trim()
if ($exists -ne '1') {
  & $Psql -h 127.0.0.1 -p $PgPort -U $DbUser -d postgres -c ('CREATE DATABASE ' + $DbName) *> (Join-Path $TargetRoot 'logs\createdb.log')
  Ok ('已创建数据库 ' + $DbName)
} else {
  Ok ('数据库 ' + $DbName + ' 已存在')
}
$check = (& $Psql -h 127.0.0.1 -p $PgPort -U $DbUser -d $DbName -tAc 'SELECT 1' 2>&1 | Out-String).Trim()
Ok ('psql 连接自检: ' + $(if ($check -eq '1') { 'SELECT 1 成功 ✅' } else { '失败 ❌ -> ' + $check }))

Step '跑数据库迁移'
Push-Location (Join-Path $TargetRoot 'app')
try {
  & (Join-Path $TargetRoot 'app\lan_file_hub_migrate.exe') --migrations (Join-Path $TargetRoot 'app\migrations') --env-file $EnvPath *> (Join-Path $TargetRoot 'logs\migrate.log')
  $migrateCode = $LASTEXITCODE
} finally { Pop-Location }
if ($migrateCode -eq 0) { Ok '迁移完成' } else { Warn ('迁移失败（退出码 ' + $migrateCode + '），看 logs\migrate.log'); }

Step '创建管理员'
$adminPassword = [Environment]::GetEnvironmentVariable($AdminPasswordEnv, 'Process')
$generated = $false
if (-not $adminPassword) {
  $adminPassword = 'Lfh-' + (New-RandomSecret 12)
  $generated = $true
}
$env:INITIAL_ADMIN_PASSWORD = $adminPassword
Push-Location (Join-Path $TargetRoot 'app')
try {
  & (Join-Path $TargetRoot 'app\lan_file_hub_migrate.exe') --migrations (Join-Path $TargetRoot 'app\migrations') --env-file $EnvPath `
    --create-admin $AdminUser --admin-email $AdminEmail --admin-password-env INITIAL_ADMIN_PASSWORD *> (Join-Path $TargetRoot 'logs\create-admin.log')
  $adminCode = $LASTEXITCODE
} finally { Pop-Location }
Remove-Item Env:\INITIAL_ADMIN_PASSWORD -ErrorAction SilentlyContinue
if ($adminCode -eq 0) { Ok ('管理员 ' + $AdminUser + ' 就绪') } else { Warn ('建管理员失败（退出码 ' + $adminCode + '），看 logs\create-admin.log'); }

Remove-Item Env:\PGPASSWORD -ErrorAction SilentlyContinue

# ---- 6) 收尾 -----------------------------------------------------------------
# 图标：桌面快捷方式与网页标签都用它。源头在仓库的 frontend\public\favicon.ico，
# 便携包要自包含，所以拷一份进包根目录（scripts\create-desktop-shortcut.ps1 会用它）。
$faviconSource = Join-Path (Split-Path -Parent $PSScriptRoot) 'frontend\public\favicon.ico'
$faviconTarget = Join-Path $TargetRoot 'favicon.ico'
if ((Test-Path $faviconSource) -and -not (Test-Path $faviconTarget)) {
  Copy-Item $faviconSource $faviconTarget -Force
  Ok '已把 favicon.ico 拷进便携包（桌面图标 / 网页标签用）'
}

Write-Host ''
Write-Host '  ✅ 便携包准备完成' -ForegroundColor Green
Write-Host ('     目录: ' + $TargetRoot) -ForegroundColor White
Write-Host ('     电脑打开: http://127.0.0.1:' + $HttpPort + '/') -ForegroundColor White
if ($LanAddress) { Write-Host ('     手机打开: http://' + $LanAddress + ':' + $HttpPort + '/（同一 WiFi/热点）') -ForegroundColor Yellow }
Write-Host ('     启动: 双击 ' + (Join-Path $TargetRoot '启动.cmd')) -ForegroundColor DarkGray
Write-Host ('     管理员: ' + $AdminUser)
if ($generated) {
  # 口令**不打印**到控制台/日志（那会进聊天记录、进 CI 日志）；写进便携包自己的 logs\（已 gitignore）
  $notePath = Join-Path $TargetRoot 'logs\bootstrap-admin.txt'
  $note = @(
    'lan-file-hub 便携包：管理员引导口令（登录后请立刻修改，改完删掉本文件）',
    ('用户名: ' + $AdminUser),
    ('口令:   ' + $adminPassword),
    ('生成时间: ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
  ) -join "`r`n"
  [System.IO.File]::WriteAllText($notePath, $note + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
  Write-Host ''
  Write-Host ('     ⚠️ 引导口令已写入：' + $notePath) -ForegroundColor Yellow
  Write-Host '        （故意不打印到屏幕：口令不该进日志/聊天记录）登录后请立即修改，然后删掉该文件。' -ForegroundColor Yellow
}
Write-Host ''
