# 读取项目根 .env 的共用小工具（menu / start / stop / bench 共用），**只读不写**。
#
# 存在的理由：后端端口必须只有一处配置。本机实测 Windows 保留端口段
# （Docker Desktop 预留了 7920-8419）覆盖了默认的 8080，后端根本绑不上；
# 而脚本里硬编码 8080 会让"改了 .env 也没用"。
# 用法：. (Join-Path $PSScriptRoot '_env.ps1')   然后 Get-BackendPort / Get-BackendUrl

$Script:LfhProjectRoot = Split-Path -Parent $PSScriptRoot

function Read-EnvFileValue {
  param([string]$Name, [string]$Default)
  $envFile = Join-Path $Script:LfhProjectRoot '.env'
  if (Test-Path $envFile) {
    foreach ($line in (Get-Content $envFile -Encoding UTF8 -ErrorAction SilentlyContinue)) {
      if ($line -match ('^\s*' + [regex]::Escape($Name) + '\s*=\s*(.*)$')) {
        $value = $Matches[1].Trim()
        $quoted = $value.Length -ge 2 -and
          (($value.StartsWith('"') -and $value.EndsWith('"')) -or ($value.StartsWith("'") -and $value.EndsWith("'")))
        if ($quoted) { $value = $value.Substring(1, $value.Length - 2) }
        return $value
      }
    }
  }
  return $Default
}

# 后端监听端口，以 .env 的 HTTP_PORT 为准（默认 8080）
function Get-BackendPort {
  $port = 0
  $raw = Read-EnvFileValue -Name 'HTTP_PORT' -Default '8080'
  if ([int]::TryParse($raw, [ref]$port) -and $port -ge 1 -and $port -le 65535) { return $port }
  return 8080
}

function Get-BackendUrl { return ('http://127.0.0.1:' + (Get-BackendPort)) }
