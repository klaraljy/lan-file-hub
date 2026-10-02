# 在桌面创建"带图标"的快捷方式（用户入口）。
#
#   .\scripts\create-desktop-shortcut.ps1            # 便携包形态：指向 portable\启动.cmd
#   .\scripts\create-desktop-shortcut.ps1 -Dev       # 开发形态：指向 scripts\start.ps1
#   .\scripts\create-desktop-shortcut.ps1 -Remove    # 删掉桌面上的快捷方式
#
# 为什么用快捷方式而不是 .cmd 本身：**只有快捷方式能带自定义图标**
# （.cmd 在桌面上只能显示系统默认图标），而且它能指定"正常窗口"，
# 让用户双击后看得见启动过程与地址面板。
#
# 图标源头：frontend\public\favicon.ico（仓库里）→ 拷到 portable\favicon.ico（便携包自包含）。
param(
  [switch]$Dev,
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Desktop = [Environment]::GetFolderPath('Desktop')
$LnkPath = Join-Path $Desktop 'lan-file-hub.lnk'

if ($Remove) {
  if (Test-Path $LnkPath) {
    Remove-Item $LnkPath -Force
    Write-Host ('  已删除：' + $LnkPath) -ForegroundColor Green
  } else {
    Write-Host '  桌面上没有这个快捷方式'
  }
  exit 0
}

# 图标：优先用便携包里的，没有就从仓库源头拷一份过去
$IconPath = Join-Path $RepoRoot 'portable\favicon.ico'
if (-not (Test-Path $IconPath)) {
  $IconSource = Join-Path $RepoRoot 'frontend\public\favicon.ico'
  if (Test-Path $IconSource) {
    Copy-Item $IconSource $IconPath -Force
    Write-Host ('  已把图标拷进便携包：' + $IconPath)
  } else {
    $IconPath = ''
    Write-Host '  找不到 favicon.ico，快捷方式将使用系统默认图标' -ForegroundColor Yellow
  }
}

if ($Dev) {
  $Target = Join-Path $RepoRoot 'scripts\start.ps1'
  $Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $Target + '"'
  $WorkDir = $RepoRoot
} else {
  $Target = Join-Path $RepoRoot 'portable\启动.cmd'
  $Arguments = ''
  $WorkDir = Join-Path $RepoRoot 'portable'
}
if (-not (Test-Path $Target)) {
  Write-Host ('  找不到启动目标：' + $Target) -ForegroundColor Red
  exit 1
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($LnkPath)
$shortcut.TargetPath = $Target
$shortcut.Arguments = $Arguments
$shortcut.WorkingDirectory = $WorkDir
if ($IconPath) { $shortcut.IconLocation = $IconPath + ',0' }
$shortcut.Description = 'lan-file-hub 局域网文件柜'
# 1 = 正常窗口：让用户看得见启动面板（以后要"隐藏命令行"时把这里改成 7）
$shortcut.WindowStyle = 1
$shortcut.Save()

$check = $shell.CreateShortcut($LnkPath)
Write-Host ('  已创建：' + $LnkPath) -ForegroundColor Green
Write-Host ('     目标：' + $check.TargetPath + ' ' + $check.Arguments)
Write-Host ('     图标：' + $check.IconLocation)
Write-Host ('     起始位置：' + $check.WorkingDirectory)
