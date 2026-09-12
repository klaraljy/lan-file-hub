# 恢复：把备份目录里的 database.dump 与 storage.tar.gz 还原（阶段十五第 12、13 条）。
#
#   # 安全演练（默认）：只恢复到临时库 lan_file_hub_restore_check，不动现有数据
#   .\scripts\restore.ps1 -BackupDir backups\20260912-160000
#
#   # 就地覆盖（破坏性！需要 -Force，且会先自动做一次当前状态的备份）
#   .\scripts\restore.ps1 -BackupDir backups\20260912-160000 -Force
#
# 设计取舍：
#   - 默认**不覆盖**现有库：恢复演练不该有权限顺手毁掉线上数据；
#   - 就地恢复前必须先备份当前状态（脚本自己调 backup.ps1），失败可以退回；
#   - 恢复对象目录采用"先解到临时目录再替换"的方式，避免解压中途失败留下半个存储。
param(
  [Parameter(Mandatory = $true)][string]$BackupDir,
  [string]$EnvFile = '.env',
  [string]$PgContainer = 'lan-file-hub-postgres',
  [string]$CheckDatabase = 'lan_file_hub_restore_check',
  [switch]$Force,
  [switch]$SkipStorage
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot
try {
  $source = if ([System.IO.Path]::IsPathRooted($BackupDir)) { $BackupDir } else { Join-Path $ProjectRoot $BackupDir }
  if (-not (Test-Path $source)) { throw "备份目录不存在：$source" }
  $dump = Join-Path $source 'database.dump'
  if (-not (Test-Path $dump)) { throw "备份缺少 database.dump：$source" }

  # 先核对清单里的 SHA-256，避免用坏掉的备份去覆盖数据
  $manifestPath = Join-Path $source 'manifest.json'
  if (Test-Path $manifestPath) {
    $manifest = Get-Content $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $actual = (Get-FileHash $dump -Algorithm SHA256).Hash.ToLower()
    $expected = $manifest.files.'database.dump'.sha256
    if ($expected -and $actual -ne $expected) { throw "备份校验失败：database.dump 的 SHA-256 与清单不一致" }
    Write-Host '==> 备份清单校验通过（SHA-256 一致）' -ForegroundColor Green
  }

  $envPath = Join-Path $ProjectRoot $EnvFile
  $dbUser = 'lan_file_hub'; $dbName = 'lan_file_hub'
  if (Test-Path $envPath) {
    foreach ($line in Get-Content $envPath -Encoding UTF8) {
      if ($line -match '^\s*POSTGRES_USER\s*=\s*(.+)$') { $dbUser = $Matches[1].Trim() }
      if ($line -match '^\s*POSTGRES_DB\s*=\s*(.+)$') { $dbName = $Matches[1].Trim() }
    }
  }

  $targetDb = if ($Force) { $dbName } else { $CheckDatabase }
  if ($Force) {
    Write-Host '==> 就地恢复：先备份当前状态' -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot 'backup.ps1') -EnvFile $EnvFile -OutDir 'backups/pre-restore'
    if ($LASTEXITCODE -ne 0) { throw '恢复前的安全备份失败，已中止（不覆盖数据）' }
  } else {
    Write-Host ('==> 演练模式：恢复到临时库 ' + $targetDb + '（现有库不动）') -ForegroundColor Cyan
    # 必须分成两条命令：psql -c 会把多条语句放进同一个事务，而 DROP/CREATE DATABASE 不能在事务里执行
    & docker exec $PgContainer psql -U $dbUser -d postgres -c "DROP DATABASE IF EXISTS $targetDb;" 2>&1 | Out-Null
    & docker exec $PgContainer psql -U $dbUser -d postgres -c "CREATE DATABASE $targetDb OWNER $dbUser;" 2>&1 | ForEach-Object { '    ' + $_ }
    if ($LASTEXITCODE -ne 0) { throw '创建临时库失败' }
  }

  Write-Host ('==> 恢复数据库到 ' + $targetDb) -ForegroundColor Cyan
  $remote = '/tmp/lfh-restore.dump'
  & docker cp $dump "${PgContainer}:$remote" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'docker cp 上传 dump 失败' }
  $flags = if ($Force) { '--clean --if-exists --no-owner' } else { '--no-owner' }
  & docker exec $PgContainer pg_restore -U $dbUser -d $targetDb $flags.Split(' ') $remote 2>&1 | Select-Object -Last 5
  $restoreCode = $LASTEXITCODE
  $null = & docker exec $PgContainer rm -f $remote 2>&1
  # pg_restore 对"对象已存在"会返回警告级非零码；演练模式（空库）必须为 0
  if (-not $Force -and $restoreCode -ne 0) { throw "pg_restore 失败，退出码 $restoreCode" }
  if ($Force -and $restoreCode -ne 0) { Write-Host ('  [警告] pg_restore 返回 ' + $restoreCode + '（就地恢复时对象已存在的警告属常见）') -ForegroundColor Yellow }

  Write-Host '==> 校验恢复结果' -ForegroundColor Cyan
  $counts = (& docker exec $PgContainer psql -U $dbUser -d $targetDb -t -A -F '=' -c "SELECT 'users', count(*) FROM users UNION ALL SELECT 'files', count(*) FROM files UNION ALL SELECT 'folders', count(*) FROM folders;" 2>&1) -join ';'
  Write-Host ('    ' + $counts)
  if (Test-Path $manifestPath) { Write-Host ('    备份时的统计: ' + (Get-Content $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json).database.counts) }

  if (-not $SkipStorage) {
    $archive = Join-Path $source 'storage.tar.gz'
    if (-not (Test-Path $archive)) {
      Write-Host '  [跳过] 备份里没有 storage.tar.gz' -ForegroundColor Yellow
    } else {
      Write-Host '==> 恢复对象存储（先解到临时目录再替换）' -ForegroundColor Cyan
      $staging = Join-Path $source 'storage-staging'
      if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
      New-Item -ItemType Directory -Force -Path $staging | Out-Null
      & docker run --rm -v "${staging}:/out" -v "${source}:/in:ro" alpine:3.20 tar xzf /in/storage.tar.gz -C /out 2>&1 | Out-Null
      if ($LASTEXITCODE -ne 0) { throw '解压对象归档失败' }
      $files = (Get-ChildItem $staging -Recurse -File | Measure-Object).Count
      Write-Host ('    解出 ' + $files + ' 个文件到 ' + $staging)
      if ($Force) {
        Write-Host '    就地替换对象目录：请确认已停止后端（避免写入与恢复竞争）' -ForegroundColor Yellow
        $volume = ((& docker volume ls --filter 'name=storage_data' --format '{{.Name}}' 2>&1) | Select-Object -First 1)
        if (-not $volume) { throw '找不到 storage_data 卷：请手动把临时目录内容同步回实际存储目录' }
        & docker run --rm -v "${staging}:/src:ro" -v "${volume}:/dst" alpine:3.20 sh -c 'rm -rf /dst/* && cp -a /src/. /dst/' 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw '写回对象存储卷失败' }
        Write-Host ('    已写回卷 ' + $volume) -ForegroundColor Green
      } else {
        Write-Host '    演练模式：对象已解到临时目录，未改动实际存储' -ForegroundColor Cyan
      }
    }
  }

  Write-Host '==> 恢复流程结束' -ForegroundColor Green
  if (-not $Force) {
    Write-Host ('    演练库 ' + $targetDb + ' 保留着，核对无误后可自行 DROP；生产恢复请加 -Force') -ForegroundColor Cyan
  }
} finally {
  Pop-Location
}
