# 运行测试。
#   .\scripts\test.ps1
#   .\scripts\test.ps1 -Preset release
param(
  [ValidateSet('debug', 'release')]
  [string]$Preset = 'debug'
)

$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\_vsenv.ps1"

Push-Location $ProjectRoot
try {
  Write-Host "==> ctest ($Preset)" -ForegroundColor Cyan
  & $CtestExe --preset $Preset --output-on-failure
  $code = $LASTEXITCODE
  if ($code -ne 0) { throw "测试失败，退出码 $code" }
  Write-Host "==> 测试全部通过" -ForegroundColor Green
} finally {
  Pop-Location
}
