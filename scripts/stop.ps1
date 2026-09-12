# 停止 lan-file-hub 本机进程（阶段十六产物）。
#
#   .\scripts\stop.ps1              # 停后端 + 前端 dev server（依赖服务继续跑）
#   .\scripts\stop.ps1 -WithDeps    # 连 PostgreSQL / Redis 容器一起停
param([switch]$WithDeps)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $ProjectRoot

Write-Host ''
Write-Host '  停止 lan-file-hub' -ForegroundColor White

# 后端
$server = Get-Process -Name 'lan_file_hub_server' -ErrorAction SilentlyContinue
if ($server) {
  $server | Stop-Process -Force
  Write-Host ('    已停止后端（' + $server.Count + ' 个进程）') -ForegroundColor Green
} else {
  Write-Host '    后端没在运行' -ForegroundColor DarkGray
}

# 前端 dev server：按端口找进程（避免误杀其它 node）
$conn = Get-NetTCPConnection -LocalPort 5173 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
if ($conn) {
  $proc = Get-Process -Id $conn.OwningProcess -ErrorAction SilentlyContinue
  if ($proc) {
    Stop-Process -Id $proc.Id -Force
    Write-Host ('    已停止前端 dev server（pid=' + $proc.Id + '，' + $proc.ProcessName + '）') -ForegroundColor Green
  }
} else {
  Write-Host '    前端没在运行' -ForegroundColor DarkGray
}

if ($WithDeps) {
  Write-Host '    停止依赖容器（postgres / redis）…' -ForegroundColor Cyan
  docker compose stop postgres redis 2>&1 | Select-Object -Last 2 | ForEach-Object { '    ' + $_ }
}

Write-Host ''
Write-Host '  完成。数据都在（Docker 卷 + storage 目录），下次直接 .\scripts\start.ps1' -ForegroundColor Green
Write-Host ''
