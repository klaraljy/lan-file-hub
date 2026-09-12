# 性能与并发压测（阶段十四）。
#
#   .\scripts\bench.ps1                        # 默认：64 MiB 文件、200 次 API 请求、并发 8
#   .\scripts\bench.ps1 -FileMiB 16 -ApiRequests 100 -Concurrency 4
#   .\scripts\bench.ps1 -WriteJson D:\tmp\bench.json
#
# 前置：后端已在运行（默认 http://127.0.0.1:8080），PostgreSQL 与 Redis 可用。
# 脚本只创建一个临时测试用户与它自己的文件，不改服务端配置，也不清理已有数据。
# **任何一项失败都会以非零退出码结束**（任务书要求）。
param(
  [string]$BaseUrl = 'http://127.0.0.1:8080',
  [int]$FileMiB = 64,
  [int]$ApiRequests = 200,
  [int]$Concurrency = 8,
  [string]$WriteJson = ''
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
$script:Failures = New-Object System.Collections.ArrayList
$script:Results = [ordered]@{}

function Fail([string]$Reason) {
  [void]$script:Failures.Add($Reason)
  Write-Host ('  [FAIL] ' + $Reason) -ForegroundColor Red
}

function Ok([string]$Text) {
  Write-Host ('  [ok] ' + $Text) -ForegroundColor Green
}

function Api([string]$Method, [string]$Path, [string]$JsonBody = '', [string]$Token = '') {
  $bodyFile = Join-Path $script:WorkDir 'body.json'
  $respFile = Join-Path $script:WorkDir 'resp.json'
  $hdrFile = Join-Path $script:WorkDir 'hdr.txt'
  $curlArgs = @('-s', '-o', $respFile, '-D', $hdrFile, '-w', '%{http_code}', '-X', $Method)
  if ($JsonBody) {
    [System.IO.File]::WriteAllText($bodyFile, $JsonBody, (New-Object System.Text.UTF8Encoding($false)))
    $curlArgs += @('-H', 'Content-Type: application/json', '--data-binary', ('@' + $bodyFile))
  }
  if ($Token) { $curlArgs += @('-H', ('Authorization: Bearer ' + $Token)) }
  $curlArgs += ($script:ApiBase + $Path)
  $code = (& curl.exe @curlArgs 2>$null | Out-String).Trim()
  return @{
    Code = [int]$code
    Body = if (Test-Path $respFile) { [System.IO.File]::ReadAllText($respFile) } else { '' }
    Headers = if (Test-Path $hdrFile) { [System.IO.File]::ReadAllText($hdrFile) } else { '' }
  }
}

function Percentile([double[]]$Sorted, [double]$Fraction) {
  if ($Sorted.Count -eq 0) { return 0.0 }
  $index = [Math]::Min($Sorted.Count - 1, [Math]::Max(0, [int][Math]::Ceiling($Fraction * $Sorted.Count) - 1))
  return $Sorted[$index]
}

# 从 /metrics 的直方图桶里估算分位（桶分辨率，够用来观察趋势）
function PercentileFromMetrics([string]$MetricsText, [string]$Metric, [double]$Fraction) {
  $lines = $MetricsText -split "`n" | Where-Object { $_ -match ('^' + [regex]::Escape($Metric) + '_bucket\{') }
  $buckets = @()
  foreach ($line in $lines) {
    $m = [regex]::Match($line, 'le="([^"]+)"[^}]*\}\s+(\d+)')
    if ($m.Success) {
      $le = if ($m.Groups[1].Value -eq '+Inf') { [double]::MaxValue } else { [double]$m.Groups[1].Value }
      $buckets += [pscustomobject]@{ Le = $le; Count = [int]$m.Groups[2].Value }
    }
  }
  if ($buckets.Count -eq 0) { return -1.0 }
  $buckets = $buckets | Sort-Object Le
  $total = ($buckets | Select-Object -Last 1).Count
  if ($total -le 0) { return -1.0 }
  $target = [Math]::Ceiling($Fraction * $total)
  foreach ($bucket in $buckets) {
    if ($bucket.Count -ge $target) { return $bucket.Le }
  }
  return -1.0
}

function NewDeterministicFile([string]$Path, [int]$MiB) {
  # 固定 LCG 数据集：同样的参数生成同样的字节，便于跨机器/跨提交对比
  $blockSize = 4096
  $block = New-Object byte[] $blockSize
  [uint64]$state = 305419896
  for ($i = 0; $i -lt $blockSize; $i++) {
    $state = [uint64](($state * 1664525 + 1013904223) % 4294967296)
    $block[$i] = [byte](($state -shr 16) -band 0xFF)
  }
  $stream = [System.IO.File]::Create($Path)
  try {
    $blocks = [int](($MiB * 1024 * 1024) / $blockSize)
    for ($k = 0; $k -lt $blocks; $k++) { $stream.Write($block, 0, $blockSize) }
  } finally {
    $stream.Close()
  }
}

function ServerMemoryMb {
  $proc = Get-Process -Name 'lan_file_hub_server' -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $proc) { return -1.0 }
  return [Math]::Round($proc.WorkingSet64 / 1MB, 1)
}

# ---- 准备 ----
$script:Base = $BaseUrl.TrimEnd('/')
$script:ApiBase = $script:Base + '/api/v1'
$script:WorkDir = Join-Path $env:TEMP ('lfh-bench-' + (Get-Random -Maximum 99999))
New-Item -ItemType Directory -Force -Path $script:WorkDir | Out-Null
$api = $script:ApiBase

Write-Host ('=== lan-file-hub 压测 | 目标 ' + $script:Base + ' | 文件 ' + $FileMiB + ' MiB | API 请求 ' +
            $ApiRequests + ' | 并发 ' + $Concurrency + ' ===') -ForegroundColor Cyan

try {
  $live = 0
  try { $live = (Invoke-WebRequest -Uri ($script:Base + '/health/live') -UseBasicParsing -TimeoutSec 3).StatusCode } catch { }
  if ($live -ne 200) { Fail '服务不可用：先启动后端（/health/live 不是 200）'; throw '前置条件不满足' }
  try {
    $ready = (Invoke-WebRequest -Uri ($script:Base + '/health/ready') -UseBasicParsing -TimeoutSec 5).StatusCode
    if ($ready -ne 200) { Fail '依赖未就绪：/health/ready 不是 200（检查 PostgreSQL / Redis）' }
    else { Ok '健康检查：live/ready 均 200' }
  } catch { Fail '依赖未就绪：/health/ready 请求失败' }

  # ---- 测试用户 ----
  $user = 'bench' + (Get-Random -Maximum 999999)
  $password = 'Bench-Marker-Password-9!'
  $reg = Api 'POST' '/auth/register' ('{"username":"' + $user + '","email":"' + $user + '@bench.local","password":"' + $password + '"}')
  if ($reg.Code -ne 200 -and $reg.Code -ne 201) { Fail ('注册压测用户失败: HTTP ' + $reg.Code) }
  $login = Api 'POST' '/auth/login' ('{"username":"' + $user + '","password":"' + $password + '"}')
  $tokenMatch = [regex]::Match($login.Body, '"access_token"\s*:\s*"([^"]+)"')
  if (-not $tokenMatch.Success) { Fail ('登录失败: HTTP ' + $login.Code); throw '前置条件不满足' }
  $token = $tokenMatch.Groups[1].Value
  $headers = @('-H', ('Authorization: Bearer ' + $token))

  $rootMatch = [regex]::Match((Api 'GET' '/folders/root' '' $token).Body, '"id"\s*:\s*"([0-9a-f\-]{36})"')
  $rootId = $rootMatch.Groups[1].Value

  # ---- 固定数据集 ----
  $bigPath = Join-Path $script:WorkDir 'bench.bin'
  NewDeterministicFile $bigPath $FileMiB
  $bigHash = (Get-FileHash $bigPath -Algorithm SHA256).Hash.ToLower()
  $bigBytes = (Get-Item $bigPath).Length
  Ok ('数据集就绪：' + $bigBytes + ' 字节，SHA-256=' + $bigHash.Substring(0, 12) + '…')

  $chunkSize = 8 * 1024 * 1024
  $memBefore = ServerMemoryMb

  # ---- 上传吞吐（分块、顺序）----
  Write-Host '--- 上传 ---' -ForegroundColor Cyan
  $session = Api 'POST' '/uploads' ('{"folder_id":"' + $rootId + '","file_name":"bench.bin","size_bytes":' +
                                     $bigBytes + ',"chunk_size":' + $chunkSize + '}') $token
  $uploadIdMatch = [regex]::Match($session.Body, '"upload_id"\s*:\s*"([0-9a-f\-]{36})"')
  if (-not $uploadIdMatch.Success) { Fail ('创建上传会话失败: HTTP ' + $session.Code) }
  $uploadId = $uploadIdMatch.Groups[1].Value

  $chunkCount = [int][Math]::Ceiling($bigBytes / $chunkSize)
  $stream = [System.IO.File]::OpenRead($bigPath)
  $buffer = New-Object byte[] $chunkSize
  $uploadStart = Get-Date
  $uploadFailures = 0
  try {
    for ($index = 0; $index -lt $chunkCount; $index++) {
      $read = $stream.Read($buffer, 0, $chunkSize)
      $chunkFile = Join-Path $script:WorkDir ('chunk-' + $index + '.bin')
      $chunkBytes = New-Object byte[] $read
      [Array]::Copy($buffer, $chunkBytes, $read)
      [System.IO.File]::WriteAllBytes($chunkFile, $chunkBytes)
      $chunkHash = (Get-FileHash $chunkFile -Algorithm SHA256).Hash.ToLower()
      $code = (& curl.exe -s -o NUL -w '%{http_code}' -X PUT @headers -H ('X-Chunk-Sha256: ' + $chunkHash) `
               --data-binary ('@' + $chunkFile) ($api + '/uploads/' + $uploadId + '/chunks/' + $index) 2>$null | Out-String).Trim()
      if ($code -notin @('200', '201')) { $uploadFailures++; Fail ('分块 ' + $index + ' 上传失败: HTTP ' + $code) }
    }
  } finally {
    $stream.Close()
  }

  # ---- 重复分块（幂等重传，任务书第 8 条）----
  $dupFile = Join-Path $script:WorkDir 'chunk-0.bin'
  $dupHash = (Get-FileHash $dupFile -Algorithm SHA256).Hash.ToLower()
  $dupBody = (& curl.exe -s -X PUT @headers -H ('X-Chunk-Sha256: ' + $dupHash) --data-binary ('@' + $dupFile) `
              ($api + '/uploads/' + $uploadId + '/chunks/0') 2>$null | Out-String)
  if ($dupBody -match '"already_present"\s*:\s*true') { Ok '重复分块被识别为幂等重传（already_present）' }
  else { Fail '重复分块没有返回 already_present（幂等语义可能已破坏）' }

  $complete = Api 'POST' ('/uploads/' + $uploadId + '/complete') '' $token
  $uploadSeconds = ((Get-Date) - $uploadStart).TotalSeconds
  $fileIdMatch = [regex]::Match($complete.Body, '"id"\s*:\s*"([0-9a-f\-]{36})"')
  $fileId = $fileIdMatch.Groups[1].Value
  $uploadMiBs = [Math]::Round(($bigBytes / 1MB) / [Math]::Max($uploadSeconds, 0.001), 1)
  $script:Results['upload_mib_per_second'] = $uploadMiBs
  $script:Results['upload_seconds'] = [Math]::Round($uploadSeconds, 3)
  $script:Results['upload_failures'] = $uploadFailures
  if (-not $fileId) { Fail ('完成上传失败: HTTP ' + $complete.Code) }
  Ok ('上传吞吐：' + $uploadMiBs + ' MiB/s（' + $bigBytes + ' 字节 / ' + [Math]::Round($uploadSeconds, 2) + ' 秒，' + $chunkCount + ' 个分块）')

  $memAfterUpload = ServerMemoryMb

  # ---- 下载吞吐 ----
  Write-Host '--- 下载 ---' -ForegroundColor Cyan
  $downloadUrl = $api + '/files/' + $fileId + '/download'
  $downloadPath = Join-Path $script:WorkDir 'download.bin'
  $downloadTiming = (& curl.exe -s -o $downloadPath -w '%{http_code} %{time_total}' @headers $downloadUrl 2>$null | Out-String).Trim()
  $parts = $downloadTiming -split '\s+'
  $downloadSeconds = 0.0
  [void][double]::TryParse($parts[1], [ref]$downloadSeconds)
  $downloadHash = (Get-FileHash $downloadPath -Algorithm SHA256).Hash.ToLower()
  if ($downloadHash -ne $bigHash) { Fail '下载内容与服务端对象不一致（SHA-256 不匹配）' }
  $downloadMiBs = [Math]::Round(($bigBytes / 1MB) / [Math]::Max($downloadSeconds, 0.001), 1)
  $script:Results['download_mib_per_second'] = $downloadMiBs
  $script:Results['download_seconds'] = [Math]::Round($downloadSeconds, 3)
  Ok ('下载吞吐：' + $downloadMiBs + ' MiB/s（内容 SHA-256 校验通过）')

  # ---- 并发下载（任务书第 7 条）----
  $jobs = @()
  for ($i = 0; $i -lt $Concurrency; $i++) {
    $out = Join-Path $script:WorkDir ('parallel-' + $i + '.bin')
    $jobs += Start-Job -ScriptBlock {
      param($url, $token, $out)
      $timing = (& curl.exe -s -o $out -w '%{http_code} %{time_total}' -H ('Authorization: Bearer ' + $token) $url 2>$null | Out-String).Trim()
      return @{ Timing = $timing; Out = $out }
    } -ArgumentList $downloadUrl, $token, $out
  }
  $parallelStart = Get-Date
  $null = $jobs | Wait-Job -Timeout 600
  $parallelSeconds = ((Get-Date) - $parallelStart).TotalSeconds
  $parallelFailures = 0
  $hashesOk = 0
  foreach ($job in $jobs) {
    $result = Receive-Job $job
    Remove-Job $job -Force
    if (-not $result -or $result.Timing -notmatch '^200 ') { $parallelFailures++; continue }
    if ((Get-FileHash $result.Out -Algorithm SHA256).Hash.ToLower() -eq $bigHash) { $hashesOk++ } else { $parallelFailures++ }
  }
  $script:Results['concurrent_downloads'] = $Concurrency
  $script:Results['concurrent_download_seconds'] = [Math]::Round($parallelSeconds, 3)
  $script:Results['concurrent_download_failures'] = $parallelFailures
  if ($parallelFailures -eq 0) {
    Ok ('并发下载：' + $Concurrency + ' 路全部成功且内容一致，用时 ' + [Math]::Round($parallelSeconds, 2) + ' 秒（' +
        [Math]::Round($Concurrency / [Math]::Max($parallelSeconds, 0.001), 1) + ' 请求/秒）')
  } else {
    Fail ('并发下载失败 ' + $parallelFailures + ' 路（成功且哈希一致 ' + $hashesOk + ' 路）')
  }

  # ---- 并发上传（任务书第 6 条）----
  $jobs = @()
  $smallPath = Join-Path $script:WorkDir 'small.bin'
  NewDeterministicFile $smallPath 1
  $smallHash = (Get-FileHash $smallPath -Algorithm SHA256).Hash.ToLower()
  for ($i = 0; $i -lt $Concurrency; $i++) {
    $jobs += Start-Job -ScriptBlock {
      param($api, $token, $rootId, $path, $hash, $index)
      $body = '{"folder_id":"' + $rootId + '","file_name":"parallel-' + $index + '.bin","size_bytes":1048576,"chunk_size":1048576}'
      $bodyFile = [System.IO.Path]::GetTempFileName()
      [System.IO.File]::WriteAllText($bodyFile, $body)
      $session = (& curl.exe -s -X POST -H ('Authorization: Bearer ' + $token) -H 'Content-Type: application/json' `
                  --data-binary ('@' + $bodyFile) ($api + '/uploads') 2>$null | Out-String)
      $uploadId = [regex]::Match($session, '"upload_id"\s*:\s*"([0-9a-f\-]{36})"').Groups[1].Value
      if (-not $uploadId) { return 'BODY:' + $session }
      $chunk = (& curl.exe -s -o NUL -w '%{http_code}' -X PUT -H ('Authorization: Bearer ' + $token) `
               -H ('X-Chunk-Sha256: ' + $hash) --data-binary ('@' + $path) ($api + '/uploads/' + $uploadId + '/chunks/0') 2>$null | Out-String).Trim()
      $done = (& curl.exe -s -o NUL -w '%{http_code}' -X POST -H ('Authorization: Bearer ' + $token) `
               ($api + '/uploads/' + $uploadId + '/complete') 2>$null | Out-String).Trim()
      return $chunk + '/' + $done
    } -ArgumentList $api, $token, $rootId, $smallPath, $smallHash, $i
  }
  $null = $jobs | Wait-Job -Timeout 600
  $concurrentUploadFailures = 0
  foreach ($job in $jobs) {
    $result = Receive-Job $job
    Remove-Job $job -Force
    if ($result -notmatch '^200/200$') { $concurrentUploadFailures++ }
  }
  $script:Results['concurrent_uploads'] = $Concurrency
  $script:Results['concurrent_upload_failures'] = $concurrentUploadFailures
  if ($concurrentUploadFailures -eq 0) { Ok ('并发上传：' + $Concurrency + ' 路会话全部成功') }
  else { Fail ('并发上传失败 ' + $concurrentUploadFailures + ' 路') }

  # ---- 客户端断线（任务书第 14 条）----
  for ($i = 0; $i -lt 3; $i++) {
    $null = & curl.exe -s --max-time 0.05 -o NUL @headers $downloadUrl 2>$null
  }
  Start-Sleep -Milliseconds 500
  $afterAbort = 0
  try { $afterAbort = (Invoke-WebRequest -Uri ($script:Base + '/health/live') -UseBasicParsing -TimeoutSec 3).StatusCode } catch { }
  $recheck = (& curl.exe -s -o NUL -w '%{http_code}' @headers $downloadUrl 2>$null | Out-String).Trim()
  if ($afterAbort -eq 200 -and $recheck -eq '200') { Ok '客户端中途断线后服务仍健康，后续下载正常' }
  else { Fail ('断线后异常：health=' + $afterAbort + ' 重新下载=' + $recheck) }

  # ---- API 延迟与失败率 ----
  Write-Host '--- API 延迟 ---' -ForegroundColor Cyan
  $latencies = New-Object System.Collections.ArrayList
  $apiFailures = 0
  # 全部是 /api/v1 下的路径（/health/live 在根路径，不属于这里）
  $paths = @('/auth/me', ('/files?folder_id=' + $rootId + '&page=1&page_size=20'), '/folders/root', '/trash?page=1&page_size=20')
  for ($i = 0; $i -lt $ApiRequests; $i++) {
    $path = $paths[$i % $paths.Count]
    $timing = (& curl.exe -s -o NUL -w '%{http_code} %{time_total}' @headers ($api + $path) 2>$null | Out-String).Trim()
    $tp = $timing -split '\s+'
    $seconds = 0.0
    [void][double]::TryParse($tp[1], [ref]$seconds)
    if ($seconds -gt 0) { [void]$latencies.Add($seconds) }
    if ($tp[0] -notmatch '^2') { $apiFailures++ }
  }
  $sorted = @($latencies | Sort-Object)
  $p50 = Percentile $sorted 0.50
  $p95 = Percentile $sorted 0.95
  $p99 = Percentile $sorted 0.99
  $script:Results['api_p50_ms'] = [Math]::Round($p50 * 1000, 2)
  $script:Results['api_p95_ms'] = [Math]::Round($p95 * 1000, 2)
  $script:Results['api_p99_ms'] = [Math]::Round($p99 * 1000, 2)
  $script:Results['api_failure_rate'] = [Math]::Round($apiFailures / [Math]::Max($ApiRequests, 1), 4)
  Ok ('客户端观测延迟：P50=' + [Math]::Round($p50 * 1000, 2) + 'ms P95=' + [Math]::Round($p95 * 1000, 2) +
      'ms P99=' + [Math]::Round($p99 * 1000, 2) + 'ms（' + $sorted.Count + ' 次请求，失败 ' + $apiFailures + '）')
  if ($apiFailures -gt 0) { Fail ('API 失败率非零：' + $apiFailures + '/' + $ApiRequests) }

  # 服务端直方图分位（与客户端观测互相印证；桶分辨率，只能看量级）
  $metricsText = (& curl.exe -s ($script:Base + '/metrics') 2>$null | Out-String)
  $serverP50 = PercentileFromMetrics $metricsText 'lan_file_hub_http_request_duration_seconds' 0.50
  $serverP95 = PercentileFromMetrics $metricsText 'lan_file_hub_http_request_duration_seconds' 0.95
  $serverP99 = PercentileFromMetrics $metricsText 'lan_file_hub_http_request_duration_seconds' 0.99
  $script:Results['server_p50_ms'] = [Math]::Round($serverP50 * 1000, 2)
  $script:Results['server_p95_ms'] = [Math]::Round($serverP95 * 1000, 2)
  $script:Results['server_p99_ms'] = [Math]::Round($serverP99 * 1000, 2)
  Ok ('服务端指标分位（桶分辨率）：P50≈' + [Math]::Round($serverP50 * 1000, 2) + 'ms P95≈' +
      [Math]::Round($serverP95 * 1000, 2) + 'ms P99≈' + [Math]::Round($serverP99 * 1000, 2) + 'ms')

  # ---- 内存 ----
  $memEnd = ServerMemoryMb
  $script:Results['server_memory_mb_before'] = $memBefore
  $script:Results['server_memory_mb_after_upload'] = $memAfterUpload
  $script:Results['server_memory_mb_end'] = $memEnd
  if ($memEnd -gt 0) {
    Ok ('服务端内存（WorkingSet）：开始 ' + $memBefore + ' MB → 上传后 ' + $memAfterUpload + ' MB → 结束 ' + $memEnd + ' MB')
    if ($memAfterUpload -gt 0 -and ($memAfterUpload - $memBefore) -gt (($bigBytes / 1MB) * 1.5)) {
      Fail ('上传 ' + $FileMiB + ' MiB 后内存增长异常（' + [Math]::Round($memAfterUpload - $memBefore, 1) +
            ' MB）：流式路径不应把整个文件读进内存')
    }
  } else {
    Ok '未找到 lan_file_hub_server 进程（服务可能跑在容器里），跳过内存采样'
  }
}
catch {
  Fail ('压测中断：' + $_.Exception.Message)
}
finally {
  Write-Host ''
  Write-Host '=== 汇总 ===' -ForegroundColor Cyan
  foreach ($key in $script:Results.Keys) { Write-Host ('  ' + $key.PadRight(34) + ' = ' + $script:Results[$key]) }
  if ($WriteJson) {
    $json = $script:Results | ConvertTo-Json -Depth 3
    [System.IO.File]::WriteAllText($WriteJson, $json, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ('  结果已写入 ' + $WriteJson)
  }
  Write-Host ('  临时数据目录：' + $script:WorkDir)
  if ($script:Failures.Count -gt 0) {
    Write-Host ('压测失败 ' + $script:Failures.Count + ' 项：') -ForegroundColor Red
    $script:Failures | ForEach-Object { Write-Host ('  - ' + $_) -ForegroundColor Red }
    exit 1
  }
  Write-Host '压测通过：全部检查项成功' -ForegroundColor Green
  exit 0
}
