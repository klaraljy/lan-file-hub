# 配置并构建后端。
#   .\scripts\build.ps1                 # debug
#   .\scripts\build.ps1 -Preset release
#   .\scripts\build.ps1 -Preset asan    # AddressSanitizer（MSVC）
#   .\scripts\build.ps1 -Clean          # 先删掉构建目录
param(
  [ValidateSet('debug', 'release', 'asan')]
  [string]$Preset = 'debug',
  [switch]$Clean
)

$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\_vsenv.ps1"

Push-Location $ProjectRoot
try {
  if ($Clean) {
    $dir = Join-Path $ProjectRoot "build\$Preset"
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
  }

  Write-Host "==> 配置 ($Preset)" -ForegroundColor Cyan
  & $CmakeExe --preset $Preset
  if ($LASTEXITCODE -ne 0) { throw "cmake 配置失败，退出码 $LASTEXITCODE" }

  Write-Host "==> 构建 ($Preset)" -ForegroundColor Cyan
  & $CmakeExe --build --preset $Preset
  if ($LASTEXITCODE -ne 0) { throw "cmake 构建失败，退出码 $LASTEXITCODE" }

  Write-Host "==> 完成：build/$Preset/bin/lan_file_hub_server" -ForegroundColor Green
} finally {
  Pop-Location
}
