# 数据库迁移（阶段十五第 9 条）。
#
#   .\scripts\migrate.ps1                      # 用本地构建的迁移工具打 .env 指向的库
#   .\scripts\migrate.ps1 -InContainer         # 用 docker compose 的 migrate 服务（生产路径）
#   .\scripts\migrate.ps1 -CreateAdmin alice --AdminEmail alice@example.com
#
# 管理员口令**只从环境变量读**（默认 INITIAL_ADMIN_PASSWORD），不写进仓库、不落命令行历史。
param(
  [string]$EnvFile = '.env',
  [string]$Migrations = 'migrations',
  [string]$CreateAdmin = '',
  [string]$AdminEmail = '',
  [string]$AdminPasswordEnv = 'INITIAL_ADMIN_PASSWORD',
  [switch]$InContainer,
  [ValidateSet('debug', 'release')][string]$Preset = 'debug'
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot
try {
  if ($InContainer) {
    Write-Host '==> 通过容器执行迁移（docker compose run --rm migrate）' -ForegroundColor Cyan
    & docker compose run --rm migrate
    if ($LASTEXITCODE -ne 0) { throw "容器迁移失败，退出码 $LASTEXITCODE" }
    Write-Host '==> 完成' -ForegroundColor Green
    exit 0
  }

  $exe = Join-Path $ProjectRoot "build\$Preset\bin\lan_file_hub_migrate.exe"
  if (-not (Test-Path $exe)) { throw "找不到迁移工具：$exe（先跑 .\scripts\build.ps1 -Preset $Preset）" }

  $args = @('--migrations', (Join-Path $ProjectRoot $Migrations), '--env-file', (Join-Path $ProjectRoot $EnvFile))
  if ($CreateAdmin) {
    $args += @('--create-admin', $CreateAdmin, '--admin-password-env', $AdminPasswordEnv)
    if ($AdminEmail) { $args += @('--admin-email', $AdminEmail) }
  }

  Write-Host ('==> 执行迁移：' + $exe) -ForegroundColor Cyan
  & $exe @args
  if ($LASTEXITCODE -ne 0) { throw "迁移失败，退出码 $LASTEXITCODE" }
  Write-Host '==> 完成' -ForegroundColor Green
} finally {
  Pop-Location
}
