# 一键菜单（阶段十六产物）：桌面「lan-file-hub.cmd」双击后进这里。
#
#   .\scripts\menu.ps1            # 交互菜单：1 启动 / 2 停止 / 3 状态 / 4 打开浏览器 / 5 创建管理员
#   .\scripts\menu.ps1 -Action start|stop|status|open|admin    # 直接执行某一项（脚本化调用）
param(
  [ValidateSet('', 'start', 'stop', 'status', 'open', 'admin')][string]$Action = ''
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
# 端口以 .env 的 HTTP_PORT 为准（见 scripts/_env.ps1）
. (Join-Path $PSScriptRoot '_env.ps1')
$BackendPort = Get-BackendPort
$BackendUrl = Get-BackendUrl
$FrontendUrl = 'http://127.0.0.1:5173'

function Test-Port([int]$Port, [int]$TimeoutMs = 500) {
  $client = New-Object System.Net.Sockets.TcpClient
  try {
    $task = $client.ConnectAsync('127.0.0.1', $Port)
    if ($task.Wait($TimeoutMs)) { return $client.Connected }
    return $false
  } catch { return $false } finally { $client.Close() }
}

function Get-StatusLines {
  $lines = @()
  $backendUp = Test-Port $BackendPort
  $frontendUp = Test-Port 5173
  $depsUp = (Test-Port 5432) -and (Test-Port 6379)
  $lines += ('后端(' + $BackendPort + '):   ' + $(if ($backendUp) { '运行中' } else { '未运行' }))
  $lines += ('前端(5173):   ' + $(if ($frontendUp) { '运行中' } else { '未运行' }))
  $lines += ('依赖(5432/6379): ' + $(if ($depsUp) { '可用' } else { '不可用（需要 Docker Desktop）' }))
  $lanUrl = Get-LanFrontendUrl
  if ($lanUrl) {
    $lanOk = if ($frontendUp) { '' } else { '（前端没起，先按 1）' }
    $lines += ('手机访问:      ' + $lanUrl + '  ' + $lanOk)
  }
  if ($backendUp) {
    $health = ''
    try { $health = (Invoke-WebRequest -Uri ($BackendUrl + '/health/ready') -UseBasicParsing -TimeoutSec 4).StatusCode } catch { $health = '未就绪' }
    $lines += ('后端就绪检查:  ' + $health)
  }
  return $lines
}

function Show-Status {
  Write-Host ''
  Write-Host '  当前状态' -ForegroundColor Cyan
  Get-StatusLines | ForEach-Object { Write-Host ('    ' + $_) }
  Write-Host ''
}

function Invoke-Start {
  # 直接在当前进程里跑 start.ps1：输出直接进控制台（编码正确），
  # 而且 start.ps1 自己就是把服务丢到后台后返回，不会挂住这里。
  & (Join-Path $PSScriptRoot 'start.ps1')
  if ($LASTEXITCODE -ne 0) { Write-Host '  启动未完成：原因见上面的输出' -ForegroundColor Yellow }
}

function Invoke-Stop {
  & (Join-Path $PSScriptRoot 'stop.ps1')
}

function Invoke-Open { Start-Process $FrontendUrl | Out-Null; Write-Host ('  已在浏览器打开 ' + $FrontendUrl) -ForegroundColor Green }

# 创建/提升管理员：口令由 create-admin.ps1 交互输入（不回显、不进命令行、不进日志）
function Invoke-Admin {
  & (Join-Path $PSScriptRoot 'create-admin.ps1')
  if ($LASTEXITCODE -ne 0) { Write-Host '  创建管理员未完成：原因见上面的输出' -ForegroundColor Yellow }
}

function Invoke-Once([string]$What) {
  switch ($What) {
    'start' { Invoke-Start }
    'stop' { Invoke-Stop }
    'status' { Show-Status }
    'open' { Invoke-Open }
    'admin' { Invoke-Admin }
  }
}

# 直接执行某一项（供脚本/快捷方式调用）
if ($Action) { Invoke-Once $Action; exit 0 }

# 非交互式调用（stdin 被重定向、由脚本驱动）时不要进菜单：
# Read-Host 在这种情况下会一直等输入，表现为"卡死"（自动化测试踩到过）。
if ([Console]::IsInputRedirected) {
  Write-Host '  检测到非交互式调用：请用 -Action start|stop|status|open|admin' -ForegroundColor Yellow
  Show-Status
  exit 0
}

# 交互菜单
while ($true) {
  Write-Host ''
  Write-Host '  lan-file-hub' -ForegroundColor White
  Show-Status
  Write-Host '    [1] 启动（依赖 → 迁移 → 后端 → 前端 → 打开浏览器）' -ForegroundColor White
  Write-Host '    [2] 停止（停后端与前端，数据保留）'
  Write-Host '    [3] 查看状态'
  Write-Host '    [4] 打开浏览器'
  Write-Host '    [5] 创建管理员（首次用管理员功能；已有用户可提升为管理员）'
  Write-Host '    [Q] 退出'
  Write-Host ''
  $choice = (Read-Host '  请选择').Trim().ToLower()
  switch ($choice) {
    '1' { Invoke-Start }
    '2' { Invoke-Stop }
    '3' { Show-Status }
    '4' { Invoke-Open }
    '5' { Invoke-Admin }
    'q' { Write-Host '  再见' -ForegroundColor DarkGray; exit 0 }
    '' { Invoke-Start }   # 直接回车 = 启动（最常用）
    default { Write-Host '  请输入 1 / 2 / 3 / 4 / 5 / Q' -ForegroundColor Yellow }
  }
}
