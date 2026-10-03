# 下载鉴权路径的 HTTP 验证（2026-10-03 新增）。
#
#   .\scripts\verify-download-paths.ps1 -Username admin
#   .\scripts\verify-download-paths.ps1 -Username admin -Password '...'   # 不传则交互输入
#
# 为什么要有这个脚本：项目里的自动化测试都在**服务层**（GoogleTest 直接调 Service），
# 而下载鉴权在 **HTTP 层**（AuthFilter + Controller）。2026-10-03 就因此漏掉一个真 bug：
# 带会话令牌下载 `/files/{id}/download` 返回 403「下载票据与文件不匹配」
# —— 根因是 Drogon 的 Attributes::get 在键不存在时不抛异常（解引用 end()）。
# 修完之后用这个脚本把三条路径都钉住：
#   ① 只用会话令牌（Bearer）                → 必须 200
#   ② 只用一次性票据（?ticket=，无 Bearer）  → 必须 200（浏览器原生下载走这条）
#   ③ 票据 + Bearer 同时带                  → 必须 200
#   ④ 拿 A 文件的票据去下 B 文件             → 必须 403（票据绑定校验不能被修没）
param(
  [string]$Username = 'admin',
  [string]$Password = '',
  [string]$BaseUrl = 'http://127.0.0.1:9180/api/v1'
)

$ErrorActionPreference = 'Stop'
$outDir = Join-Path $env:TEMP 'lan-file-hub-download-check'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if ([string]::IsNullOrWhiteSpace($Password)) {
  $secure = Read-Host -Prompt "  请输入 $Username 的口令" -AsSecureString
  $Password = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
    [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
}

$login = Invoke-RestMethod -Uri "$BaseUrl/auth/login" -Method Post -ContentType 'application/json' `
  -Body (@{ username = $Username; password = $Password } | ConvertTo-Json) -TimeoutSec 10
$token = $login.data.access_token
$headers = @{ Authorization = 'Bearer ' + $token }
Write-Output ("登录成功：" + $login.data.user.username)

# 找两个文件（第二个用来验证"票据不能跨文件"）
$root = Invoke-RestMethod -Uri "$BaseUrl/folders/root" -Headers $headers -TimeoutSec 10
$listing = Invoke-RestMethod -Uri "$BaseUrl/files?folder_id=$($root.data.id)&page_size=10" -Headers $headers -TimeoutSec 10
if (@($listing.data.items).Count -lt 1) { throw '这个账号根目录里没有文件，先传一个再跑这个脚本' }
$fileA = $listing.data.items[0]
$fileB = if (@($listing.data.items).Count -ge 2) { $listing.data.items[1] } else { $null }
Write-Output ("测试文件：" + $fileA.name + "（" + $fileA.size_bytes + " 字节）")

function Try-Download([string]$Url, [hashtable]$RequestHeaders) {
  $target = Join-Path $outDir ([guid]::NewGuid().ToString('N') + '.bin')
  try {
    if ($RequestHeaders) {
      Invoke-WebRequest -Uri $Url -Headers $RequestHeaders -OutFile $target -TimeoutSec 60 | Out-Null
    } else {
      Invoke-WebRequest -Uri $Url -OutFile $target -TimeoutSec 60 | Out-Null
    }
    $length = (Get-Item $target).Length
    Remove-Item $target -Force
    return @{ ok = $true; status = 200; length = $length }
  } catch {
    return @{ ok = $false; status = [int]$_.Exception.Response.StatusCode; length = 0 }
  }
}

$host_ = ([uri]$BaseUrl).Authority
$failures = @()

Write-Output ''
Write-Output '① 只用会话令牌（Bearer）'
$r1 = Try-Download "$BaseUrl/files/$($fileA.id)/download" $headers
Write-Output ('   → ' + $r1.status + '  ' + $r1.length + ' 字节')
if (-not $r1.ok) { $failures += '① 只用会话令牌下载被拒（应是 200）' }

Write-Output '② 只用一次性票据（无 Authorization 头）'
$ticketA = Invoke-RestMethod -Uri "$BaseUrl/files/$($fileA.id)/download-ticket" -Method Post `
  -Headers $headers -ContentType 'application/json' -Body '{}' -TimeoutSec 10
$r2 = Try-Download ("http://$host_" + $ticketA.data.url) $null
Write-Output ('   → ' + $r2.status + '  ' + $r2.length + ' 字节')
if (-not $r2.ok) { $failures += '② 票据下载被拒（应是 200）' }

Write-Output '③ 票据 + Bearer 同时带'
$r3 = Try-Download ("http://$host_" + $ticketA.data.url) $headers
Write-Output ('   → ' + $r3.status + '  ' + $r3.length + ' 字节')
if (-not $r3.ok) { $failures += '③ 票据与令牌同时带被拒（应是 200）' }

if ($fileB) {
  Write-Output '④ 拿 A 的票据去下 B（必须被拒）'
  $r4 = Try-Download ("http://$host_/api/v1/files/$($fileB.id)/download?ticket=" + $ticketA.data.ticket) $null
  Write-Output ('   → ' + $r4.status)
  if ($r4.status -ne 403) { $failures += '④ 跨文件用票据没有被拒（应是 403）' }
} else {
  Write-Output '④ 跳过：根目录只有一个文件，无法验证"票据不能跨文件"'
}

Write-Output ''
if ($failures.Count -eq 0) {
  Write-Host '  ✅ 四条路径全部符合预期' -ForegroundColor Green
  exit 0
}
foreach ($item in $failures) { Write-Host ('  ❌ ' + $item) -ForegroundColor Red }
exit 1
