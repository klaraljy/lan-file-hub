# 便携包一键恢复（2026-10-03）：把某次备份的数据库与文件对象放回去。
#
#   .\scripts\portable-restore.ps1                          # 列出可用备份，让你挑一个
#   .\scripts\portable-restore.ps1 -BackupDir <备份目录>     # 直接恢复指定备份
#   .\scripts\portable-restore.ps1 -BackupDir <目录> -Force  # 跳过二次确认（自动化用）
#
# ⛔ 这会**覆盖当前数据**（数据库整库重建、文件对象按备份内容替换）：
#    所以：① 先核对清单哈希；② 先做一次"当前状态"的备份并**检查它成功**；③ 必须人工确认一次。
#
# ⚠️ 两条踩过的坑（改这个脚本时别再犯）：
#   ① **先备份再停服务**：数据库停了 pg_dump 连不上，产出 0 字节的空 dump（第一次就踩了）；
#      备份失败必须中止，不许继续往下恢复。
#   ② **启停服务一律用 Start-Process（脱离式）**：用 `& pwsh -File tray.ps1 *> log` 会永久卡住
#      —— 被启动的 PG/Redis/后端继承那个重定向句柄，父进程永远等不到结束（AGENTS §11.1）。
param(
  [string]$BackupDir = '',
  [switch]$Force
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $PSCommandPath
# 与 portable-backup.ps1 同一套形态识别：脚本在 <便携包>\tools\ 还是 <仓库>\scripts\
$portableRoot = if (Test-Path (Join-Path (Split-Path -Parent $scriptDir) 'app\.env')) {
  Split-Path -Parent $scriptDir
} else {
  Join-Path (Split-Path -Parent $scriptDir) 'portable'
}

$appDir = Join-Path $portableRoot 'app'
$envPath = Join-Path $appDir '.env'
$pgRestore = Join-Path $portableRoot 'pgsql\bin\pg_restore.exe'
$psql = Join-Path $portableRoot 'pgsql\bin\psql.exe'
$backupsRoot = Join-Path $portableRoot 'backups'
$trayScript = Join-Path $portableRoot 'tools\tray.ps1'
if (-not (Test-Path $trayScript)) { $trayScript = Join-Path (Split-Path -Parent $scriptDir) 'tray.ps1' }
$logsDir = Join-Path $portableRoot 'logs'

if (-not (Test-Path $envPath)) { throw "找不到便携包配置：$envPath" }
if (-not (Test-Path $pgRestore)) { throw "找不到 pg_restore：$pgRestore" }
New-Item -ItemType Directory -Force -Path $logsDir | Out-Null

function Start-StackDetached {
  # ⛔ 必须脱离式启动：直接 & 调用 + 重定向会让父进程永远卡住（见文件头说明）
  Start-Process -FilePath 'pwsh' -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $trayScript, '-Action', 'start', '-NoBrowser'
  ) | Out-Null
}

function Stop-StackDetached {
  $process = Start-Process -FilePath 'pwsh' -WindowStyle Hidden -PassThru -ArgumentList @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $trayScript, '-Action', 'stop'
  )
  # 有界等待：最多 30 秒
  if (-not $process.WaitForExit(30000)) { throw '停止服务超时（30 秒）' }
}

function Wait-Health {
  param([int]$Seconds = 60)
  $deadline = (Get-Date).AddSeconds($Seconds)
  while ((Get-Date) -lt $deadline) {
    try {
      $code = (Invoke-WebRequest -Uri 'http://127.0.0.1:9180/health/live' -UseBasicParsing -TimeoutSec 3).StatusCode
      if ($code -eq 200) { return $true }
    } catch {
      # 还没起来，继续等
    }
    Start-Sleep -Seconds 2
  }
  return $false
}

# ---- 挑一个备份 ----
if ([string]::IsNullOrWhiteSpace($BackupDir)) {
  if (-not (Test-Path $backupsRoot)) { throw "还没有任何备份：$backupsRoot（先跑 portable-backup.ps1）" }
  $candidates = @(Get-ChildItem $backupsRoot -Directory | Sort-Object Name -Descending | Where-Object {
      (Test-Path (Join-Path $_.FullName 'database.dump')) -and
      ((Get-Item (Join-Path $_.FullName 'database.dump')).Length -gt 0)
    })
  if ($candidates.Count -eq 0) { throw "还没有可用的备份（空的 dump 不算）：$backupsRoot" }
  Write-Host ''
  Write-Host '  可用的备份：' -ForegroundColor White
  for ($i = 0; $i -lt $candidates.Count; $i++) {
    $manifestPath = Join-Path $candidates[$i].FullName 'manifest.json'
    $info = ''
    if (Test-Path $manifestPath) {
      $m = Get-Content $manifestPath -Raw | ConvertFrom-Json
      $info = '  ' + $m.database.counts + '  ' + $m.created_at
    }
    Write-Host ('   [' + ($i + 1) + '] ' + $candidates[$i].Name + $info) -ForegroundColor Gray
  }
  Write-Host ''
  $pick = Read-Host '  请输入要恢复的编号（回车取消）'
  if ([string]::IsNullOrWhiteSpace($pick)) { Write-Host '  已取消' -ForegroundColor Yellow; exit 0 }
  $index = 0
  if (-not [int]::TryParse($pick, [ref]$index) -or $index -lt 1 -or $index -gt $candidates.Count) {
    throw '编号无效'
  }
  $BackupDir = $candidates[$index - 1].FullName
}

$dumpPath = Join-Path $BackupDir 'database.dump'
if (-not (Test-Path $dumpPath)) { throw "这个目录里没有 database.dump：$BackupDir" }
if ((Get-Item $dumpPath).Length -le 0) { throw "database.dump 是空文件（上次备份失败），换一个备份：$BackupDir" }

Write-Host ''
Write-Host '  局域网文件柜 · 一键恢复' -ForegroundColor White
Write-Host ('  备份来源：' + $BackupDir) -ForegroundColor Cyan

# ---- 核对清单（哈希对不上就别恢复，宁可不做也不要做一半）----
$manifestPath = Join-Path $BackupDir 'manifest.json'
if (Test-Path $manifestPath) {
  $m = Get-Content $manifestPath -Raw | ConvertFrom-Json
  $actual = (Get-FileHash $dumpPath -Algorithm SHA256).Hash.ToLower()
  if ($m.database.sha256 -and $actual -ne $m.database.sha256) {
    throw '数据库备份的 SHA-256 与清单不一致（文件被改过或拷坏了），已中止'
  }
  Write-Host ('  清单核对通过：' + $m.database.counts + '，迁移 ' + $m.database.migrations) -ForegroundColor Green
} else {
  Write-Host '  ⚠ 没有 manifest.json，跳过核对（老备份）' -ForegroundColor Yellow
}

if (-not $Force) {
  Write-Host ''
  Write-Host '  ⛔ 恢复会覆盖当前数据库与文件对象。' -ForegroundColor Yellow
  $answer = Read-Host '  确认继续吗？输入 yes 继续（其它任何输入都会取消）'
  if ($answer -ne 'yes') { Write-Host '  已取消（什么都没改）' -ForegroundColor Yellow; exit 0 }
}

# ---- ① 先备份当前状态（**服务还开着**，数据库在线才 dump 得出来）----
Write-Host '==> 先备份当前状态（失败就中止，不动数据）' -ForegroundColor Cyan
$backupScript = Join-Path $portableRoot 'tools\portable-backup.ps1'
if (-not (Test-Path $backupScript)) { $backupScript = Join-Path $scriptDir 'portable-backup.ps1' }
$preBackupLog = Join-Path $logsDir 'restore-prebackup.log'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $backupScript -OutDir $backupsRoot *> $preBackupLog
if ($LASTEXITCODE -ne 0) {
  Get-Content $preBackupLog -Tail 6 | ForEach-Object { '   ' + $_ }
  throw "恢复前的备份失败（退出码 $LASTEXITCODE），已中止：不冒险覆盖当前数据"
}
$latest = Get-ChildItem $backupsRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
Write-Host ('    已另存到 backups\' + $latest.Name) -ForegroundColor Gray

# ---- ② 停服务 ----
Write-Host '==> 停止服务' -ForegroundColor Cyan
Stop-StackDetached

# ---- ③ 起数据库与服务（pg_restore 要连库；后端稍后会再重启一次）----
Write-Host '==> 启动数据库与服务' -ForegroundColor Cyan
Start-StackDetached
if (-not (Wait-Health -Seconds 90)) { throw '服务在 90 秒内没有就绪，已中止（数据未被改动）' }
Write-Host '    服务已就绪' -ForegroundColor Gray

# ---- ④ 恢复数据库 ----
$dbUrl = ''
$storageRoot = 'data/storage'
foreach ($line in Get-Content $envPath -Encoding UTF8) {
  if ($line -match '^\s*DATABASE_URL\s*=\s*(.+)$') { $dbUrl = $Matches[1].Trim() }
  if ($line -match '^\s*STORAGE_ROOT\s*=\s*(.+)$') { $storageRoot = $Matches[1].Trim() }
}
if ($dbUrl -notmatch '^postgresql://([^:]+):([^@]+)@([^:/]+):(\d+)/(.+)$') { throw 'DATABASE_URL 形式不对' }
$dbUser = $Matches[1]; $dbPass = $Matches[2]; $dbHost = $Matches[3]; $dbPort = $Matches[4]; $dbName = $Matches[5]
$env:PGPASSWORD = $dbPass

Write-Host '==> 恢复数据库（断开连接 → 整库重建 → pg_restore）' -ForegroundColor Cyan
try {
  & $psql -h $dbHost -p $dbPort -U $dbUser -d postgres -tAc "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = '$dbName' AND pid <> pg_backend_pid();" | Out-Null
  & $psql -h $dbHost -p $dbPort -U $dbUser -d postgres -c "DROP DATABASE IF EXISTS $dbName" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "删除旧库失败，退出码 $LASTEXITCODE" }
  & $psql -h $dbHost -p $dbPort -U $dbUser -d postgres -c "CREATE DATABASE $dbName" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "重建库失败，退出码 $LASTEXITCODE" }
  & $pgRestore -h $dbHost -p $dbPort -U $dbUser -d $dbName --no-owner --no-privileges $dumpPath
  if ($LASTEXITCODE -ne 0) { throw "pg_restore 失败，退出码 $LASTEXITCODE" }

  $after = ((& $psql -h $dbHost -p $dbPort -U $dbUser -d $dbName -tAc "SELECT 'users=' || (SELECT count(*) FROM users) || ' files=' || (SELECT count(*) FROM files) || ' folders=' || (SELECT count(*) FROM folders);")).Trim()
  if (Test-Path $manifestPath) {
    Write-Host ('    恢复后：' + $after + '（清单里是 ' + $m.database.counts + '）') -ForegroundColor Gray
  } else {
    Write-Host ('    恢复后：' + $after) -ForegroundColor Gray
  }
} finally {
  Remove-Item Env:\PGPASSWORD -ErrorAction SilentlyContinue
}
Write-Host '    数据库已恢复' -ForegroundColor Green

# ---- ⑤ 恢复文件对象（先解到临时目录，解包失败时原来的对象还在）----
$storageArchive = Join-Path $BackupDir 'storage.tar'
if (Test-Path $storageArchive) {
  Write-Host '==> 恢复文件对象' -ForegroundColor Cyan
  $storagePath = Join-Path $appDir $storageRoot
  $parent = Split-Path -Parent $storagePath
  $staging = Join-Path $parent ('storage.restore-' + (Get-Date -Format 'HHmmss'))
  New-Item -ItemType Directory -Force -Path $staging | Out-Null
  & tar.exe -xf $storageArchive -C $staging
  if ($LASTEXITCODE -ne 0) { throw "解包失败，退出码 $LASTEXITCODE（原对象未改动）" }
  $leaf = Split-Path -Leaf $storagePath
  $restored = Join-Path $staging $leaf
  if (-not (Test-Path $restored)) { throw ('解包里没有 ' + $leaf + '（原对象未改动）') }
  if (Test-Path $storagePath) { Remove-Item -Recurse -Force $storagePath }
  Move-Item $restored $storagePath
  Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue
  Write-Host '    文件对象已恢复' -ForegroundColor Green
} else {
  Write-Host '  ⚠ 这次备份里没有 storage.tar，文件对象保持现状' -ForegroundColor Yellow
}

# ---- ⑥ 再重启一次服务：整库重建期间后端会看到"数据库不存在"，重启一次状态最干净 ----
Write-Host '==> 重启服务（让后端连上恢复后的库）' -ForegroundColor Cyan
Stop-StackDetached
Start-StackDetached
$readyAgain = Wait-Health -Seconds 90
$health = if ($readyAgain) { '200' } else { '连不上（请看 logs\backend.log）' }
Write-Host ('    健康检查：/health/live = ' + $health) -ForegroundColor Gray

Write-Host ''
Write-Host '  ✅ 恢复完成' -ForegroundColor Green
Write-Host '     建议：登录网页看一眼文件列表与「回收站」，确认是自己要的那份数据。' -ForegroundColor DarkGray
Write-Host ''
