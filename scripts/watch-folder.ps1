# 「放进文件夹就自动上传」（2026-10-03，用户批准的功能之一）。
#
#   .\scripts\watch-folder.ps1 -Folder D:\扫描件 -Username admin              # 一直盯着（Ctrl+C 停）
#   .\scripts\watch-folder.ps1 -Folder D:\扫描件 -Username admin -Once        # 只扫一遍就退出
#   .\scripts\watch-folder.ps1 -Folder D:\扫描件 -Username admin -RemoteFolder 扫描件
#
# 干什么：你把文件**丢进这个本地文件夹**，它就自动传到电脑上的文件柜里（单向：本地 → 服务器）。
# 只做单向同步是刻意的：双向同步要处理冲突、删除、改名，属于另一个量级的工程
# （AGENTS §1「不做完整桌面同步客户端」）；单向已经覆盖"扫描件/截图自动进文件柜"这类真实需求。
#
# 三条纪律：
#   - 口令只用于**换一次令牌**，令牌保存在本机（默认 %LOCALAPPDATA%\lan-file-hub\watch-token.txt）；
#     ⛔ 口令不进命令行、不落盘、不进日志。
#   - 传过的文件记在状态文件里（相对路径 + 大小 + 修改时间），重复运行**不会重复上传**。
#   - 服务器上同名文件由后端自动加 (1)、(2)（用户 2026-10-03 的要求），本地文件不会被覆盖或改名。
param(
  [Parameter(Mandatory = $true)][string]$Folder,
  [string]$Username = '',
  [string]$Password = '',           # 不传则交互输入（推荐）
  [string]$BaseUrl = 'http://127.0.0.1:9180/api/v1',
  [string]$RemoteFolder = '',       # 服务器上的目标文件夹（默认：根目录）
  [string]$StateFile = '',
  [int]$ChunkSize = 8388608,        # 8 MiB，与后端默认一致
  [int]$IntervalSeconds = 10,       # 轮询间隔（兜住 FileSystemWatcher 漏掉的事件）
  [switch]$Once,                    # 扫一遍就退出（做计划任务/测试时用）
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Folder)) { throw "请告诉我要盯哪个文件夹，例如：-Folder D:\扫描件" }
if (-not (Test-Path $Folder)) { throw "这个文件夹不存在：$Folder" }
$Folder = (Resolve-Path $Folder).Path

# ---- 令牌：优先复用本机保存的（有就不用再输口令）----
$tokenDir = Join-Path $env:LOCALAPPDATA 'lan-file-hub'
New-Item -ItemType Directory -Force -Path $tokenDir | Out-Null
if ([string]::IsNullOrWhiteSpace($StateFile)) { $StateFile = Join-Path $tokenDir 'watch-state.json' }
$tokenFile = Join-Path $tokenDir 'watch-token.txt'

function Write-Info([string]$Text) { if (-not $Quiet) { Write-Host ('  ' + $Text) -ForegroundColor Gray } }

function Get-Token {
  if (Test-Path $tokenFile) {
    $saved = (Get-Content $tokenFile -Raw).Trim()
    if ($saved) {
      try {
        # 令牌可能已过期：拿 /auth/me 试一下，不行就重新登录
        $null = Invoke-RestMethod -Uri "$BaseUrl/auth/me" -Headers @{ Authorization = "Bearer $saved" } -TimeoutSec 8
        return $saved
      } catch {
        Write-Info '保存的登录已过期，需要重新登录一次'
      }
    }
  }

  if ([string]::IsNullOrWhiteSpace($Username)) {
    $Username = (Read-Host '  电脑上的账号（用户名或邮箱）').Trim()
  }
  if ([string]::IsNullOrWhiteSpace($Password)) {
    $secure = Read-Host -Prompt '  口令（不会显示、不会保存）' -AsSecureString
    $Password = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
      [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
  }
  $body = if ($Username.Contains('@')) {
    @{ email = $Username; password = $Password; remember = $true } | ConvertTo-Json
  } else {
    @{ username = $Username; password = $Password; remember = $true } | ConvertTo-Json
  }
  $login = Invoke-RestMethod -Uri "$BaseUrl/auth/login" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10
  $Password = ''
  # ⛔ 只保存令牌（不是口令）；用户可以随时删掉这个文件来"退出登录"
  [System.IO.File]::WriteAllText($tokenFile, $login.data.access_token, (New-Object System.Text.UTF8Encoding($false)))
  Write-Info ('已登录：' + $login.data.user.username + '（令牌有效期 ' + [Math]::Round($login.data.expires_in / 86400, 1) + ' 天）')
  return $login.data.access_token
}

$script:Token = Get-Token
$script:Headers = @{ Authorization = "Bearer $script:Token" }

function Get-RootFolderId {
  return (Invoke-RestMethod -Uri "$BaseUrl/folders/root" -Headers $script:Headers -TimeoutSec 10).data.id
}

function Ensure-RemoteFolder([string]$name) {
  if ([string]::IsNullOrWhiteSpace($name)) { return (Get-RootFolderId) }
  $parent = Get-RootFolderId
  $children = Invoke-RestMethod -Uri "$BaseUrl/folders/$parent/children?page_size=100" -Headers $script:Headers -TimeoutSec 10
  foreach ($child in $children.data.items) {
    if ($child.name -eq $name) { return $child.id }
  }
  $created = Invoke-RestMethod -Uri "$BaseUrl/folders" -Method Post -Headers $script:Headers -ContentType 'application/json' `
    -Body (@{ parent_id = $parent; name = $name } | ConvertTo-Json) -TimeoutSec 10
  Write-Info ('已在服务器上创建文件夹「' + $name + '」')
  return $created.data.id
}

function Send-File([string]$path, [string]$folderId) {
  $info = Get-Item $path
  $name = $info.Name
  $size = $info.Length
  $bytes = [System.IO.File]::ReadAllBytes($path)
  $sha = [System.BitConverter]::ToString(([System.Security.Cryptography.SHA256]::Create()).ComputeHash($bytes)).Replace('-', '').ToLower()

  $create = @{
    folder_id  = $folderId
    file_name  = $name
    size_bytes = $size
    chunk_size = $ChunkSize
    sha256     = $sha
  } | ConvertTo-Json
  $session = Invoke-RestMethod -Uri "$BaseUrl/uploads" -Method Post -Headers $script:Headers -ContentType 'application/json' -Body $create -TimeoutSec 20
  $uploadId = $session.data.upload_id
  $total = [int]$session.data.total_chunks
  for ($index = 0; $index -lt $total; $index++) {
    $offset = $index * $ChunkSize
    $length = [Math]::Min($ChunkSize, $size - $offset)
    if ($length -le 0) { $length = 0 }
    $part = New-Object byte[] $length
    if ($length -gt 0) { [Array]::Copy($bytes, $offset, $part, 0, $length) }
    $null = Invoke-RestMethod -Uri "$BaseUrl/uploads/$uploadId/chunks/$index" -Method Put -Headers $script:Headers `
      -ContentType 'application/octet-stream' -Body $part -TimeoutSec 120
  }
  $done = Invoke-RestMethod -Uri "$BaseUrl/uploads/$uploadId/complete" -Method Post -Headers $script:Headers `
    -ContentType 'application/json' -Body '{}' -TimeoutSec 120
  return $done.data.name
}

# ---- 状态：相对路径 → 大小 + 修改时间（判断"是不是新文件/改过的文件"）----
function Load-State {
  if (Test-Path $StateFile) {
    try { return (Get-Content $StateFile -Raw | ConvertFrom-Json) } catch { return @{} }
  }
  return @{}
}

function Save-State($state) {
  [System.IO.File]::WriteAllText($StateFile, ($state | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))
}

function Sync-Once([string]$folderId, $state) {
  $files = @(Get-ChildItem -Path $Folder -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notlike '~$*' -and $_.Name -notlike '*.tmp' -and $_.Name -notlike '*.partial' })
  $uploaded = 0
  foreach ($file in $files) {
    $relative = $file.FullName.Substring($Folder.Length).TrimStart('\')
    $stamp = [int64]$file.LastWriteTimeUtc.Ticks
    $known = $state.$relative
    if ($known -and $known.size -eq $file.Length -and $known.stamp -eq $stamp) {
      continue  # 传过了，跳过
    }
    # 正在写入的文件先跳过（大小还没稳定），下一轮再传
    Start-Sleep -Milliseconds 150
    $again = Get-Item $file.FullName -ErrorAction SilentlyContinue
    if (-not $again -or $again.Length -ne $file.Length) { continue }

    try {
      $remote = Send-File $file.FullName $folderId
      $state | Add-Member -NotePropertyName $relative -NotePropertyValue @{ size = $file.Length; stamp = $stamp } -Force
      Save-State $state
      $uploaded++
      Write-Info ('已上传：' + $relative + ' → ' + $remote)
    } catch {
      Write-Host ('  ⚠ 上传失败（下一轮自动重试）：' + $relative + ' —— ' + $_.Exception.Message) -ForegroundColor Yellow
    }
  }
  return $uploaded
}

# ---- 主流程 ----
$targetFolderId = Ensure-RemoteFolder $RemoteFolder
$state = Load-State
Write-Host ''
Write-Host '  局域网文件柜 · 自动上传' -ForegroundColor White
Write-Host ('  本地文件夹：' + $Folder) -ForegroundColor Cyan
Write-Host ('  传到服务器：' + $(if ($RemoteFolder) { $RemoteFolder } else { '根目录' })) -ForegroundColor Cyan
Write-Host ('  已上传记录：' + $StateFile) -ForegroundColor DarkGray
Write-Host ''

$first = Sync-Once $targetFolderId $state
Write-Info ('本轮上传 ' + $first + ' 个文件')

if ($Once) {
  Write-Host '  ✅ 扫描完成（-Once 模式，退出）' -ForegroundColor Green
  exit 0
}

Write-Host '  正在盯着这个文件夹…（把文件丢进去就会自动上传；按 Ctrl+C 停止）' -ForegroundColor Green
$watcher = New-Object System.IO.FileSystemWatcher
$watcher.Path = $Folder
$watcher.IncludeSubdirectories = $true
$watcher.EnableRaisingEvents = $true
$pending = $false
$handler = { $script:pending = $true }
$null = Register-ObjectEvent $watcher 'Created' -Action $handler
$null = Register-ObjectEvent $watcher 'Changed' -Action $handler
$null = Register-ObjectEvent $watcher 'Renamed' -Action $handler

try {
  while ($true) {
    Start-Sleep -Seconds $IntervalSeconds
    if ($script:pending -or $true) {
      # 事件只是"早点动手"的信号；每轮都做一次全量比对，漏事件也不会漏文件
      $script:pending = $false
      $null = Sync-Once $targetFolderId $state
    }
  }
} finally {
  $watcher.EnableRaisingEvents = $false
  $watcher.Dispose()
  Write-Host '  已停止（本地文件都还在，服务器上已传的也都在）' -ForegroundColor Yellow
}
