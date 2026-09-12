# 一键菜单（阶段十六产物）：桌面「lan-file-hub.cmd」双击后进这里。
#
#   .\scripts\menu.ps1            # 交互菜单：1 启动 / 2 停止 / 3 状态 / 4 打开浏览器
#   .\scripts\menu.ps1 -Action start|stop|status|open    # 直接执行某一项（脚本化调用）
param(
  [ValidateSet('', 'start', 'stop', 'status', 'open')][string]$Action = ''
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BackendUrl = 'http://127.0.0.1:8080'
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
  $backendUp = Test-Port 8080
  $frontendUp = Test-Port 5173
  $depsUp = (Test-Port 5432) -and (Test-Port 6379)
  $lines += ('后端(8080):   ' + $(if ($backendUp) { '运行中' } else { '未运行' }))
  $lines += ('前端(5173):   ' + $(if ($frontendUp) { '运行中' } else { '未运行' }))
  $lines += ('依赖(5432/6379): ' + $(if ($depsUp) { '可用' } else { '不可用（需要 Docker Desktop）' }))
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
  # start.ps1 会启动后台进程，所以输出重定向到日志、并等它自己跑完
  $log = Join-Path $env:TEMP 'lfh-start.log'
  $proc = Start-Process -FilePath 'powershell.exe' -WorkingDirectory $ProjectRoot -PassThru -WindowStyle Hidden `
    -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'start.ps1') `
    -RedirectStandardOutput $log -RedirectStandardError (Join-Path $env:TEMP 'lfh-start.err')
  [void]$proc.WaitForExit(180000)
  Get-Content $log -Encoding UTF8 -ErrorAction SilentlyContinue | Where-Object { $_.Trim() } | ForEach-Object { Write-Host ('  ' + $_) }
}

function Invoke-Stop {
  $log = Join-Path $env:TEMP 'lfh-stop.log'
  $proc = Start-Process -FilePath 'powershell.exe' -WorkingDirectory $ProjectRoot -PassThru -WindowStyle Hidden `
    -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'stop.ps1') `
    -RedirectStandardOutput $log -RedirectStandardError (Join-Path $env:TEMP 'lfh-stop.err')
  [void]$proc.WaitForExit(60000)
  Get-Content $log -Encoding UTF8 -ErrorAction SilentlyContinue | Where-Object { $_.Trim() } | ForEach-Object { Write-Host ('  ' + $_) }
}

function Invoke-Open { Start-Process $FrontendUrl | Out-Null; Write-Host ('  已在浏览器打开 ' + $FrontendUrl) -ForegroundColor Green }

function Invoke-Once([string]$What) {
  switch ($What) {
    'start' { Invoke-Start }
    'stop' { Invoke-Stop }
    'status' { Show-Status }
    'open' { Invoke-Open }
  }
}

# 直接执行某一项（供脚本/快捷方式调用）
if ($Action) { Invoke-Once $Action; exit 0 }

# 非交互式调用（stdin 被重定向、由脚本驱动）时不要进菜单：
# Read-Host 在这种情况下会一直等输入，表现为"卡死"（自动化测试踩到过）。
if ([Console]::IsInputRedirected) {
  Write-Host '  检测到非交互式调用：请用 -Action start|stop|status|open' -ForegroundColor Yellow
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
  Write-Host '    [Q] 退出'
  Write-Host ''
  $choice = (Read-Host '  请选择').Trim().ToLower()
  switch ($choice) {
    '1' { Invoke-Start }
    '2' { Invoke-Stop }
    '3' { Show-Status }
    '4' { Invoke-Open }
    'q' { Write-Host '  再见' -ForegroundColor DarkGray; exit 0 }
    '' { Invoke-Start }   # 直接回车 = 启动（最常用）
    default { Write-Host '  请输入 1 / 2 / 3 / 4 / Q' -ForegroundColor Yellow }
  }
}
