# 便携包一键备份（2026-10-03）：数据库 + 文件对象 + 清单，全部在本机完成，不需要 Docker。
#
#   .\scripts\portable-backup.ps1                     # 备份到 portable\backups\<时间戳>\
#   .\scripts\portable-backup.ps1 -OutDir D:\backups  # 备份到别处（强烈建议放到别的盘/U 盘）
#   .\scripts\portable-backup.ps1 -SkipStorage         # 只备份数据库（文件多时快）
#
# 备份目录里会有：
#   database.dump     pg_dump -Fc（已压缩；含 schema_migrations，恢复时知道结构版本）
#   storage.tar       对象存储目录的完整归档（objects/ 与 tmp/）
#   manifest.json     大小、SHA-256、迁移版本、用户/文件计数 —— 恢复前先核对
#
# ⛔ 备份里有用户的文件内容与账号数据，属于敏感数据：不要放进仓库、不要上传到网盘。
# 用法（便携包形态）：双击 portable\备份.cmd，或
#   portable\tools\portable-backup.ps1
param(
  [string]$OutDir = '',
  [switch]$SkipStorage
)

$ErrorActionPreference = 'Stop'

# ---- 形态识别：便携包（有 pgsql\ 与 app\.env）还是仓库开发形态 ----
$scriptDir = Split-Path -Parent $PSCommandPath
$portableRoot = Split-Path -Parent $scriptDir  # 占位，下面按形态重算
# 形态识别：脚本在 <便携包>\tools\ 下（同级有 app\.env）→ 便携包根就是上一级；
# 脚本在 <仓库>\scripts\ 下 → 便携包在仓库的 portable\ 里。
if (Test-Path (Join-Path (Split-Path -Parent $scriptDir) 'app\.env')) {
  $portableRoot = Split-Path -Parent $scriptDir
} else {
  $portableRoot = Join-Path (Split-Path -Parent $scriptDir) 'portable'
}

$appDir = Join-Path $portableRoot 'app'
$envPath = Join-Path $appDir '.env'
$pgDump = Join-Path $portableRoot 'pgsql\bin\pg_dump.exe'
$psql = Join-Path $portableRoot 'pgsql\bin\psql.exe'

if (-not (Test-Path $envPath)) { throw "找不到便携包配置：$envPath（先跑 scripts\portable-setup.ps1）" }
if (-not (Test-Path $pgDump)) { throw "找不到 pg_dump：$pgDump" }

if ([string]::IsNullOrWhiteSpace($OutDir)) { $OutDir = Join-Path $portableRoot 'backups' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$target = Join-Path $OutDir $stamp
New-Item -ItemType Directory -Force -Path $target | Out-Null

Write-Host ''
Write-Host '  局域网文件柜 · 一键备份' -ForegroundColor White
Write-Host ('  备份目录：' + $target) -ForegroundColor Cyan
Write-Host ''

# ---- 从 app\.env 读连接串（口令只用于本次 pg_dump，不落盘、不进日志）----
$dbUrl = ''
$storageRoot = 'data/storage'
foreach ($line in Get-Content $envPath -Encoding UTF8) {
  if ($line -match '^\s*DATABASE_URL\s*=\s*(.+)$') { $dbUrl = $Matches[1].Trim() }
  if ($line -match '^\s*STORAGE_ROOT\s*=\s*(.+)$') { $storageRoot = $Matches[1].Trim() }
}
if ([string]::IsNullOrWhiteSpace($dbUrl)) { throw 'app\.env 里没有 DATABASE_URL' }
if ($dbUrl -notmatch '^postgresql://([^:]+):([^@]+)@([^:/]+):(\d+)/(.+)$') { throw 'DATABASE_URL 不是预期的 postgresql://user:pass@host:port/db 形式' }
$dbUser = $Matches[1]; $dbPass = $Matches[2]; $dbHost = $Matches[3]; $dbPort = $Matches[4]; $dbName = $Matches[5]
$env:PGPASSWORD = $dbPass

$migrations = ''
$counts = ''
try {
  # ---- 1) 数据库 ----
  Write-Host '==> 导出数据库（pg_dump -Fc）' -ForegroundColor Cyan
  $dumpPath = Join-Path $target 'database.dump'
  & $pgDump -h $dbHost -p $dbPort -U $dbUser -d $dbName -Fc -f $dumpPath
  if ($LASTEXITCODE -ne 0) { throw "pg_dump 失败，退出码 $LASTEXITCODE" }
  Write-Host ('    ' + [Math]::Round((Get-Item $dumpPath).Length / 1KB, 1) + ' KB') -ForegroundColor Gray

  # ---- 2) 迁移版本与计数（恢复前能对照）----
  $migrations = ((& $psql -h $dbHost -p $dbPort -U $dbUser -d $dbName -tAc 'SELECT version FROM schema_migrations ORDER BY version;') -join ',')
  $counts = ((& $psql -h $dbHost -p $dbPort -U $dbUser -d $dbName -tAc "SELECT 'users=' || (SELECT count(*) FROM users) || ' files=' || (SELECT count(*) FROM files) || ' folders=' || (SELECT count(*) FROM folders);")).Trim()
  Write-Host ('    迁移：' + $migrations) -ForegroundColor Gray
  Write-Host ('    数据：' + $counts) -ForegroundColor Gray
} finally {
  Remove-Item Env:\PGPASSWORD -ErrorAction SilentlyContinue
}

# ---- 3) 文件对象 ----
$storageArchive = ''
$storagePath = Join-Path $appDir $storageRoot
if (-not $SkipStorage) {
  if (-not (Test-Path $storagePath)) {
    Write-Host ('==> 跳过文件归档：目录不存在 ' + $storagePath) -ForegroundColor Yellow
  } else {
    Write-Host '==> 归档文件对象（tar）' -ForegroundColor Cyan
    $storageArchive = Join-Path $target 'storage.tar'
    # Windows 自带 tar.exe（bsdtar）；-C 到父目录，归档里保留 storage 这一层
    $parent = Split-Path -Parent $storagePath
    $leaf = Split-Path -Leaf $storagePath
    & tar.exe -cf $storageArchive -C $parent $leaf
    if ($LASTEXITCODE -ne 0) { throw "tar 归档失败，退出码 $LASTEXITCODE" }
    Write-Host ('    ' + [Math]::Round((Get-Item $storageArchive).Length / 1MB, 2) + ' MB') -ForegroundColor Gray
  }
}

# ---- 4) 清单（恢复前先核对，出问题能立刻看出来）----
$manifest = [ordered]@{
  created_at      = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss') + 'Z'
  app             = 'lan-file-hub portable'
  database        = @{
    file     = 'database.dump'
    size     = (Get-Item (Join-Path $target 'database.dump')).Length
    sha256   = (Get-FileHash (Join-Path $target 'database.dump') -Algorithm SHA256).Hash.ToLower()
    name     = $dbName
    migrations = $migrations
    counts   = $counts
  }
  storage         = if ($storageArchive) {
    @{
      file   = 'storage.tar'
      size   = (Get-Item $storageArchive).Length
      sha256 = (Get-FileHash $storageArchive -Algorithm SHA256).Hash.ToLower()
      root   = $storageRoot
    }
  } else { $null }
}
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $target 'manifest.json'), ($manifest | ConvertTo-Json -Depth 5), $utf8NoBom)

Write-Host ''
Write-Host ('  ✅ 备份完成：' + $target) -ForegroundColor Green
Write-Host '     恢复：双击 portable\恢复.cmd，或 portable\tools\portable-restore.ps1 -BackupDir <这个目录>' -ForegroundColor DarkGray
Write-Host '     ⛔ 里面有账号与文件内容，别放进仓库、别上传网盘' -ForegroundColor Yellow
Write-Host ''
