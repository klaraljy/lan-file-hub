# 开机自启（2026-10-03，用户批准的功能之一）：让便携包在登录 Windows 后自己起来。
#
#   .\scripts\autostart.ps1 -Enable     # 在"当前用户的启动文件夹"里放一个快捷方式
#   .\scripts\autostart.ps1 -Disable    # 删掉它（可随时撤销）
#   .\scripts\autostart.ps1 -Status     # 看现在是开还是关
#
# 为什么用"启动文件夹"而不是 Windows 服务：
#   - **不需要管理员权限**（服务要装、要 UAC，还会影响整台机器）；
#   - 只对当前登录用户生效，用户随时能自己删掉，完全可逆；
#   - 便携包本来就是"用户自己的程序 + 自己的数据"，与账户绑定正合适。
# ⛔ 这台机器上**不会被自动开启**：脚本只提供开关，由用户自己决定什么时候打开。
param(
  [switch]$Enable,
  [switch]$Disable,
  [switch]$Status
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $PSCommandPath

# 形态识别（与 portable-backup.ps1 同一套）：脚本在 <便携包>\tools\ 还是 <仓库>\scripts\
$portableRoot = if (Test-Path (Join-Path (Split-Path -Parent $scriptDir) 'app\.env')) {
  Split-Path -Parent $scriptDir
} else {
  Join-Path (Split-Path -Parent $scriptDir) 'portable'
}
$launcher = Join-Path $portableRoot '启动.cmd'
$shortcutName = '局域网文件柜.lnk'
$startupDir = [Environment]::GetFolderPath('Startup')
$shortcutPath = Join-Path $startupDir $shortcutName

function ShowStatus {
  $exists = Test-Path $shortcutPath
  Write-Host ''
  Write-Host ('  开机自启：' + $(if ($exists) { '已开启' } else { '未开启' })) -ForegroundColor $(if ($exists) { 'Green' } else { 'Gray' })
  Write-Host ('  启动项：' + $shortcutPath) -ForegroundColor DarkGray
  Write-Host ('  会启动：' + $launcher) -ForegroundColor DarkGray
  Write-Host ''
}

if ($Status -or (-not $Enable -and -not $Disable)) {
  ShowStatus
  exit 0
}

if ($Disable) {
  if (Test-Path $shortcutPath) {
    Remove-Item $shortcutPath -Force
    Write-Host '  已关闭开机自启（启动项已删除）' -ForegroundColor Green
  } else {
    Write-Host '  本来就是关着的（没有启动项）' -ForegroundColor Gray
  }
  ShowStatus
  exit 0
}

# ---- 开启 ----
if (-not (Test-Path $launcher)) {
  throw "找不到启动器：$launcher（便携包不完整？）"
}
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $launcher
$shortcut.WorkingDirectory = $portableRoot
$shortcut.Description = '开机后自动启动局域网文件柜（关掉：再跑一次 autostart.ps1 -Disable）'
$shortcut.Save()

Write-Host '  已开启开机自启' -ForegroundColor Green
Write-Host '    开机（登录）后会自动启动服务与托盘图标；随时可以关掉：' -ForegroundColor Gray
Write-Host ('    ' + (Join-Path $portableRoot 'tools\autostart.ps1') + ' -Disable') -ForegroundColor DarkGray
ShowStatus
