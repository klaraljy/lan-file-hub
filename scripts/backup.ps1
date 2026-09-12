# 备份：数据库（pg_dump 自定义格式）+ 文件对象目录 + 一份可核对的清单（阶段十五第 11、13 条）。
#
#   .\scripts\backup.ps1                          # 备份到 backups\<时间戳>\
#   .\scripts\backup.ps1 -OutDir D:\backups
#   .\scripts\backup.ps1 -SkipStorage             # 只备份数据库
#
# 备份目录里会有：
#   database.dump       pg_dump -Fc（已压缩，含 schema_migrations，恢复时知道结构版本）
#   storage.tar.gz      对象存储卷的完整归档（内容寻址的 objects/ 与 tmp/）
#   manifest.json       大小、SHA-256、迁移版本、文件计数——恢复前先核对
#
# 注意：备份包含用户文件内容，属于敏感数据，请自行控制存放位置与访问权限。
param(
  [string]$OutDir = 'backups',
  [string]$EnvFile = '.env',
  [string]$PgContainer = 'lan-file-hub-postgres',
  [string]$BackendContainer = 'lan-file-hub-backend',
  [string]$StorageVolume = '',
  [switch]$SkipStorage
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot
try {
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  # OutDir 可能是绝对路径：Join-Path 拼绝对路径会得到非法路径
  $baseDir = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $ProjectRoot $OutDir }
  $target = Join-Path $baseDir $stamp
  New-Item -ItemType Directory -Force -Path $target | Out-Null
  Write-Host ('==> 备份目录：' + $target) -ForegroundColor Cyan

  # 从 .env 读连接信息；口令只用于容器内的 psql/pg_dump，不落盘
  $envPath = Join-Path $ProjectRoot $EnvFile
  $dbUser = 'lan_file_hub'; $dbName = 'lan_file_hub'; $storageRoot = 'storage'
  if (Test-Path $envPath) {
    foreach ($line in Get-Content $envPath -Encoding UTF8) {
      if ($line -match '^\s*POSTGRES_USER\s*=\s*(.+)$') { $dbUser = $Matches[1].Trim() }
      if ($line -match '^\s*POSTGRES_DB\s*=\s*(.+)$') { $dbName = $Matches[1].Trim() }
      if ($line -match '^\s*STORAGE_ROOT\s*=\s*(.+)$') { $storageRoot = $Matches[1].Trim() }
    }
  }

  # ---- 数据库 ----
  Write-Host '==> 导出数据库（pg_dump -Fc）' -ForegroundColor Cyan
  $dumpInContainer = '/tmp/lfh-backup.dump'
  $dump = "& docker exec $PgContainer pg_dump -U $dbUser -d $dbName -Fc -f $dumpInContainer"
  $null = Invoke-Expression $dump 2>&1
  if ($LASTEXITCODE -ne 0) { throw "pg_dump 失败，退出码 $LASTEXITCODE" }
  & docker cp "${PgContainer}:$dumpInContainer" (Join-Path $target 'database.dump') | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'docker cp 导出 dump 失败' }
  $null = & docker exec $PgContainer rm -f $dumpInContainer 2>&1

  # ---- 迁移版本（恢复后能对照）----
  $migrations = (& docker exec $PgContainer psql -U $dbUser -d $dbName -t -A -c 'SELECT version FROM schema_migrations ORDER BY version;' 2>&1) -join ','
  $tableCounts = (& docker exec $PgContainer psql -U $dbUser -d $dbName -t -A -F '=' -c "SELECT 'users', count(*) FROM users UNION ALL SELECT 'files', count(*) FROM files UNION ALL SELECT 'file_versions', count(*) FROM file_versions UNION ALL SELECT 'folders', count(*) FROM folders;" 2>&1) -join ';'

  # ---- 对象存储 ----
  $storageArchive = ''
  if (-not $SkipStorage) {
    Write-Host '==> 归档对象存储卷' -ForegroundColor Cyan
    if (-not $StorageVolume) {
      # compose 的卷名前缀默认是项目目录名；找不到就退回从运行中的 backend 容器里取
      $StorageVolume = ((& docker volume ls --filter 'name=storage_data' --format '{{.Name}}' 2>&1) | Select-Object -First 1)
    }
    $storageArchive = Join-Path $target 'storage.tar.gz'
    $archived = $false

    # 方式一：显式指定的卷
    if ($StorageVolume) {
      & docker run --rm -v "${StorageVolume}:/data:ro" -v "${target}:/backup" alpine:3.20 tar czf /backup/storage.tar.gz -C /data . 2>&1 | Out-Null
      $archived = (Test-Path $storageArchive)
    }
    # 方式二：compose 的 storage_data 卷
    if (-not $archived) {
      $volume = ((& docker volume ls --filter 'name=storage_data' --format '{{.Name}}' 2>&1) | Select-Object -First 1)
      if ($volume) {
        $StorageVolume = $volume
        & docker run --rm -v "${volume}:/data:ro" -v "${target}:/backup" alpine:3.20 tar czf /backup/storage.tar.gz -C /data . 2>&1 | Out-Null
        $archived = (Test-Path $storageArchive)
      }
    }
    # 方式三：运行中的 backend 容器（/data/storage）
    if (-not $archived) {
      $running = ((& docker ps --filter "name=$BackendContainer" --format '{{.Names}}' 2>&1) | Select-Object -First 1)
      if ($running) {
        & docker cp "${BackendContainer}:/data/storage" (Join-Path $target 'storage') | Out-Null
        if ($LASTEXITCODE -eq 0) {
          & docker run --rm -v "${target}:/backup" alpine:3.20 tar czf /backup/storage.tar.gz -C /backup storage 2>&1 | Out-Null
          Remove-Item -Recurse -Force (Join-Path $target 'storage') -ErrorAction SilentlyContinue
          $archived = (Test-Path $storageArchive)
        }
      }
    }
    # 方式四：本地目录（本机开发就是这种：STORAGE_ROOT 指向项目里的 storage/）
    if (-not $archived) {
      $localStorage = if ([System.IO.Path]::IsPathRooted($storageRoot)) { $storageRoot } else { Join-Path $ProjectRoot $storageRoot }
      if (Test-Path $localStorage) {
        Write-Host ('    本地目录：' + $localStorage)
        & tar.exe -czf $storageArchive -C $localStorage . 2>&1 | Out-Null
        $archived = (Test-Path $storageArchive)
        $StorageVolume = ''
      }
    }
    if (-not $archived) {
      throw ('找不到对象存储：没有 storage_data 卷、没有运行中的 backend 容器，也没有本地目录 ' + $storageRoot + '（可用 -SkipStorage 只备份数据库）')
    }
  }

  # ---- 清单 ----
  $manifest = [ordered]@{
    created_at        = (Get-Date).ToString('o')
    database          = @{ user = $dbUser; name = $dbName; migrations = $migrations; counts = $tableCounts }
    storage_volume    = $StorageVolume
    storage_included  = (-not $SkipStorage)
    files             = @{}
  }
  foreach ($name in @('database.dump', 'storage.tar.gz')) {
    $path = Join-Path $target $name
    if (Test-Path $path) {
      $item = Get-Item $path
      $manifest.files[$name] = @{
        size_bytes = $item.Length
        sha256     = (Get-FileHash $path -Algorithm SHA256).Hash.ToLower()
      }
    }
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $target 'manifest.json') -Encoding UTF8

  Write-Host '==> 备份完成' -ForegroundColor Green
  foreach ($name in $manifest.files.Keys) {
    Write-Host ('    ' + $name.PadRight(16) + ' ' + [Math]::Round($manifest.files[$name].size_bytes / 1MB, 2) + ' MB  sha256=' + $manifest.files[$name].sha256.Substring(0, 12) + '…')
  }
  Write-Host ('    迁移版本: ' + $migrations)
  Write-Host ('    行数统计: ' + $tableCounts)
} finally {
  Pop-Location
}
