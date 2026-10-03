# 把**已有**账号提升为管理员 —— **不需要口令**。
#
#   便携包（用户日常用）：双击 portable\设为管理员.cmd，或
#       portable\tools\make-admin.ps1 -Username 你的用户名
#   开发形态：
#       .\scripts\make-admin.ps1 -Username klaraljy
#
# 与 create-admin.ps1 的区别：create-admin 会**新建**账号（所以需要口令），
# 本脚本只把库里那条记录的 role 改成 ADMIN，**口令一个字都不动**。
#
# 为什么需要它（用户 2026-10-02）：注册页建出来的号是普通用户，
# "第一个注册的人自动变成管理员"是错的（谁先注册谁就是管理员不合理），
# 所以要有一个人为指定的动作；这条命令就是那个人为动作。
param(
  [Parameter(Mandatory = $true)][string]$Username,
  [ValidateSet('debug', 'release')][string]$Preset = 'release'
)

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot
# 便携形态：本脚本位于 <便携包>\tools\，同级的 app\ 里有迁移工具
$PortableExe = Join-Path $Root 'app\lan_file_hub_migrate.exe'

function Ok([string]$Text) { Write-Host ('    ' + $Text) -ForegroundColor Green }
function Warn([string]$Text) { Write-Host ('    ' + $Text) -ForegroundColor Yellow }

Write-Host ''
Write-Host ('  把账号提升为管理员：' + $Username) -ForegroundColor White

if (Test-Path $PortableExe) {
  $AppDir = Join-Path $Root 'app'
  Push-Location $AppDir
  try {
    # 口令、连接串都从 .env 读；只改 role，不动口令
    & $PortableExe --migrations (Join-Path $AppDir 'migrations') --env-file (Join-Path $AppDir '.env') --promote-admin $Username
    $code = $LASTEXITCODE
  } finally {
    Pop-Location
  }
} else {
  & (Join-Path $Root 'scripts\migrate.ps1') -PromoteAdmin $Username -Preset $Preset
  $code = $LASTEXITCODE
}

Write-Host ''
if ($code -eq 0) {
  Ok ('完成：' + $Username + ' 现在是管理员。')
  Write-Host '      重新登录后，左侧会出现「系统管理」（用户管理 / 指标 / 审计）。' -ForegroundColor DarkGray
  Write-Host '      口令没有改动 —— 原来怎么登录现在还怎么登录。' -ForegroundColor DarkGray
} else {
  Warn '没成功，原因见上面的输出（用户名要区分大小写，且必须是已经存在的账号）'
  exit 1
}
