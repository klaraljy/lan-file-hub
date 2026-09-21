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

# 本机在局域网里的 IPv4（手机/其它电脑访问用的那个地址）。
# 跳过回环、169.254 自动地址与虚拟网卡（VMware / vEthernet / WSL / 本地连接*）。
function Get-LanAddress {
  $candidates = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
    Where-Object { $_.InterfaceAlias -notlike '*VMware*' -and $_.InterfaceAlias -notlike '*vEthernet*' -and $_.InterfaceAlias -notlike '本地连接*' }
  $preferred = $candidates | Where-Object { $_.PrefixOrigin -ne 'WellKnown' } | Select-Object -First 1
  if ($preferred) { return $preferred.IPAddress }
  if ($candidates) { return ($candidates | Select-Object -First 1).IPAddress }
  return ''
}

# 手机/其它设备该打开的地址（前端端口 5173；没拿到局域网地址就返回空串）
function Get-LanFrontendUrl {
  $lan = Get-LanAddress
  if (-not $lan) { return '' }
  return ('http://' + $lan + ':5173')
}
