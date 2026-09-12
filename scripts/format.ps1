# 格式化检查 / 修复。
#   .\scripts\format.ps1           # 检查（不修改文件），有不合规即非零退出
#   .\scripts\format.ps1 -Fix      # 就地格式化
param(
  [switch]$Fix
)

$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\_vsenv.ps1"

Push-Location $ProjectRoot
try {
  $files = Get-ChildItem -Path (Join-Path $ProjectRoot 'backend') -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.h', '.hpp', '.cc' } |
    ForEach-Object { $_.FullName }

  if (-not $files) { Write-Host "没有找到 C++ 源文件"; exit 0 }

  if ($Fix) {
    Write-Host "==> clang-format -i（$($files.Count) 个文件）" -ForegroundColor Cyan
    & $ClangFormatExe -i --style=file @files
  } else {
    Write-Host "==> clang-format --dry-run --Werror（$($files.Count) 个文件）" -ForegroundColor Cyan
    & $ClangFormatExe --dry-run --Werror --style=file @files
  }
  $code = $LASTEXITCODE
  if ($code -ne 0) {
    if ($Fix) { throw "格式化失败，退出码 $code" }
    throw "存在不符合 .clang-format 的文件，运行 .\scripts\format.ps1 -Fix 修复"
  }
  Write-Host "==> 格式检查通过" -ForegroundColor Green
} finally {
  Pop-Location
}
