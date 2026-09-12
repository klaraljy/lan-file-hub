# 静态检查（clang-tidy）。需要先配置过 debug 预设（compile_commands.json）。
#   .\scripts\lint.ps1
param(
  [ValidateSet('debug', 'release')]
  [string]$Preset = 'debug'
)

$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\_vsenv.ps1"

Push-Location $ProjectRoot
try {
  $buildDir = Join-Path $ProjectRoot "build\$Preset"
  $compileCommands = Join-Path $buildDir 'compile_commands.json'
  if (-not (Test-Path $compileCommands)) {
    throw "找不到 $compileCommands，先运行 .\scripts\build.ps1 -Preset $Preset"
  }

  $files = Get-ChildItem -Path (Join-Path $ProjectRoot 'backend\src'), (Join-Path $ProjectRoot 'backend\include') `
      -Recurse -File | Where-Object { $_.Extension -in '.cpp', '.h', '.hpp' } |
    ForEach-Object { $_.FullName }

  Write-Host "==> clang-tidy（$($files.Count) 个文件）" -ForegroundColor Cyan
  # -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH：本机 clang-tidy 是 19.1.0，比 VS2026 STL
  # 要求的 Clang 20 旧；不加这个宏会直接报 STL1000，导致任何 STL 头文件都解析不了。
  & $ClangTidyExe -p $buildDir --quiet `
    --extra-arg=-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH `
    --extra-arg=-Wno-unused-command-line-argument `
    @files
  $code = $LASTEXITCODE
  if ($code -ne 0) { throw "clang-tidy 报告了问题，退出码 $code" }
  Write-Host "==> 静态检查通过" -ForegroundColor Green
} finally {
  Pop-Location
}
