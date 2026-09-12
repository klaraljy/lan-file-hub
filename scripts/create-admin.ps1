# 创建（或提升）管理员账号 —— 桌面菜单里按 5 就是它。
#
#   .\scripts\create-admin.ps1                                    # 交互：问用户名、邮箱、口令（口令不回显）
#   .\scripts\create-admin.ps1 -Username alice -Email a@b.com     # 仍然会问口令
#   $env:INITIAL_ADMIN_PASSWORD='<至少8位>'; .\scripts\create-admin.ps1 -Username alice -Email a@b.com -NoPrompt
#
# 行为（与迁移工具一致，可重复执行）：
#   - 用户名不存在 → 用邮箱新建 ADMIN 用户；
#   - 用户名已存在但不是管理员 → 提升为 ADMIN（**口令不变**）；
#   - 已是管理员 → 幂等跳过。
# 口令只通过**进程环境变量**传给后端工具：不进命令行、不落盘、不进日志、不进 shell 历史。
param(
  [string]$Username = 'admin',
  [string]$Email = '',
  [string]$PasswordEnv = 'INITIAL_ADMIN_PASSWORD',
  [switch]$NoPrompt,
  [switch]$SkipDeps,
  [ValidateSet('debug', 'release')][string]$Preset = 'debug'
)

$ErrorActionPreference = 'Continue'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot '_env.ps1')
Set-Location $ProjectRoot

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

# SecureString → 明文（只在本进程内转换，用于传给子进程的环境变量）
function ConvertFrom-SecureText([System.Security.SecureString]$Secure) {
  $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Secure)
  try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
  finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}

Write-Host ''
Write-Host '  创建 / 提升管理员' -ForegroundColor White
Write-Host ''

# ---- 1) 依赖服务（只需要 PostgreSQL；数据库没起就先拉起来）----
if (-not $SkipDeps -and -not (Test-Port 5432)) {
  Step 'PostgreSQL 没在监听，先启动依赖服务'
  $job = Start-Job { docker version --format '{{.Server.Version}}' 2>$null }
  $engineUp = Wait-Job $job -Timeout 10
  Remove-Job $job -Force -ErrorAction SilentlyContinue
  if (-not $engineUp) {
    Warn 'Docker 引擎没在运行：先启动 Docker Desktop，或回菜单按 1'
    exit 1
  }
  docker compose up -d postgres redis 2>&1 | Out-Null
  Write-Host -NoNewline '    等待 PostgreSQL 就绪（最多 30 秒）'
  $health = ''
  for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 1
    $health = (docker inspect -f '{{.State.Health.Status}}' lan-file-hub-postgres 2>$null | Out-String).Trim()
    if ($health -eq 'healthy') { break }
    Write-Host -NoNewline '.'
  }
  Write-Host ''
  if ($health -ne 'healthy') {
    Warn ('PostgreSQL 没就绪（' + $health + '）：docker compose logs postgres 看原因')
    exit 1
  }
  Ok '依赖就绪'
}

# ---- 2) 用户名 / 邮箱 / 口令 ----
$weSetPassword = $false   # 只有交互式输入的口令才由本脚本负责清除
if (-not $NoPrompt) {
  # stdin 被重定向时 Read-Host 会一直等输入（看起来像卡死），所以直接给用法退出
  if ([Console]::IsInputRedirected) {
    Warn '当前是非交互式调用：请加 -NoPrompt，并先设置好口令环境变量（默认 INITIAL_ADMIN_PASSWORD）'
    exit 1
  }
  $answer = (Read-Host ('  用户名 [回车用默认: ' + $Username + ']')).Trim()
  if ($answer) { $Username = $answer }
  if (-not $Email) {
    $Email = (Read-Host '  管理员邮箱（新建用户时必填）').Trim()
  }
  $first = Read-Host '  请输入管理员口令（至少 8 位，输入时不回显）' -AsSecureString
  $second = Read-Host '  请再输入一次确认' -AsSecureString
  $plain = ConvertFrom-SecureText $first
  if ($plain -ne (ConvertFrom-SecureText $second)) {
    Warn '两次输入的口令不一致，已取消（未做任何改动）'
    exit 1
  }
  if ($plain.Length -lt 8) {
    Warn '口令至少 8 位，已取消（未做任何改动）'
    exit 1
  }
  [Environment]::SetEnvironmentVariable($PasswordEnv, $plain, 'Process')
  $weSetPassword = $true
  $plain = $null
}

if (-not [Environment]::GetEnvironmentVariable($PasswordEnv, 'Process')) {
  Warn ('没有口令：请交互运行，或先设置环境变量 ' + $PasswordEnv)
  exit 1
}

# ---- 3) 跑迁移（幂等）+ 创建/提升管理员 ----
Step ('创建 / 提升管理员: ' + $Username + $(if ($Email) { ' <' + $Email + '>' } else { '' }))
$code = 0
try {
  & (Join-Path $PSScriptRoot 'migrate.ps1') -CreateAdmin $Username -AdminEmail $Email `
    -AdminPasswordEnv $PasswordEnv -Preset $Preset
  $code = $LASTEXITCODE
} catch {
  Warn $_.Exception.Message
  $code = 1
} finally {
  # 只清掉本脚本自己交互输入的口令；调用方（-NoPrompt）传进来的不动，便于同一会话连续执行
  if ($weSetPassword) {
    [Environment]::SetEnvironmentVariable($PasswordEnv, $null, 'Process')
  }
}

if ($code -ne 0) {
  Warn '未完成：原因见上面的输出'
  exit 1
}
Write-Host ''
Ok '完成。接下来：'
Write-Host ('      1) 回菜单按 1 启动服务（若还没起）') -ForegroundColor DarkGray
Write-Host ('      2) 浏览器打开 http://127.0.0.1:5173 用 ' + $Username + ' 登录') -ForegroundColor DarkGray
Write-Host ('      3) 管理员功能在左侧「系统管理」') -ForegroundColor DarkGray
Write-Host ('      引导口令登录后请尽快修改；仓库里不保存任何默认口令') -ForegroundColor DarkGray
Write-Host ''
exit 0
