# 运行测试。
#   .\scripts\test.ps1                     # debug 全量
#   .\scripts\test.ps1 -Preset release
#   .\scripts\test.ps1 -Preset asan        # AddressSanitizer（先跑 build.ps1 -Preset asan）
#   .\scripts\test.ps1 -Label unit         # 只跑单元测试（不打库，快）
#   .\scripts\test.ps1 -Label integration  # 只跑集成测试（需要 PostgreSQL/Redis）
#
# 注意：集成测试**必须串行**跑（ctest 默认就是串行）。实测用 -j 4 并行跑集成测试会互相抢
# 同一个数据库，出现大批"创建根目录失败"这类假失败（2026-09-18 踩过）。
param(
  [ValidateSet('debug', 'release', 'asan')]
  [string]$Preset = 'debug',
  [ValidateSet('', 'unit', 'integration')]
  [string]$Label = ''
)

$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\_vsenv.ps1"

Push-Location $ProjectRoot
try {
  $ctestArgs = @('--preset', $Preset, '--output-on-failure')
  if ($Label) {
    $ctestArgs += @('-L', $Label)
    Write-Host "==> ctest ($Preset，只跑 $Label)" -ForegroundColor Cyan
  } else {
    Write-Host "==> ctest ($Preset)" -ForegroundColor Cyan
  }
  & $CtestExe @ctestArgs
  $code = $LASTEXITCODE
  if ($code -ne 0) { throw "测试失败，退出码 $code" }
  Write-Host "==> 测试全部通过" -ForegroundColor Green
} finally {
  Pop-Location
}
