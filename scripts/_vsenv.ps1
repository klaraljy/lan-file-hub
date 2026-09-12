# 载入 MSVC 编译环境，并集中定义各工具的绝对路径。
# 用法（点源，不要直接执行）：  . "$PSScriptRoot\_vsenv.ps1"
#
# 为什么需要：本机 cmake / clang 系不在 PATH，且 Ninja + cl 必须先有 vcvars 环境。
# 工具绝对路径只写在这里，不要散落到文档和 CI 中。

$ErrorActionPreference = 'Continue'

$VisualStudioRoot = 'D:\VS2026'
$vcvars = Join-Path $VisualStudioRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
  throw "找不到 vcvars64.bat：$vcvars（预期 MSVC 位于 $VisualStudioRoot）"
}

# 已经在 MSVC 环境里就不重复载入
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
  $envLines = cmd /c "`"$vcvars`" >nul 2>&1 && set"
  if ($LASTEXITCODE -ne 0) { throw "载入 vcvars64.bat 失败（退出码 $LASTEXITCODE）" }
  foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
      Set-Item -Path ('env:' + $matches[1]) -Value $matches[2] -ErrorAction SilentlyContinue
    }
  }
}

$CmakeExe = 'D:\Codex Tools\CMake\cmake-3.31.6-windows-x86_64\bin\cmake.exe'
$CtestExe = 'D:\Codex Tools\CMake\cmake-3.31.6-windows-x86_64\bin\ctest.exe'
$ClangFormatExe = 'D:\Codex Tools\LLVM\bin\clang-format.exe'
$ClangTidyExe = 'D:\Codex Tools\LLVM\bin\clang-tidy.exe'
$VcpkgRoot = 'D:\Codex Tools\vcpkg'

foreach ($tool in @($CmakeExe, $CtestExe, $ClangFormatExe, $ClangTidyExe)) {
  if (-not (Test-Path $tool)) { throw "工具不存在：$tool" }
}

# 项目根 = 本脚本所在目录的上一级
$ProjectRoot = Split-Path $PSScriptRoot -Parent
