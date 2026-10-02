# 托盘程序（五阶段改造 ④）：把"命令行菜单"换成托盘图标。
#
#   .\tray.ps1                        # 启动服务 + 命令行打印"电脑地址/手机地址" + 自动打开浏览器 + 托盘图标
#   .\tray.ps1 -Action start          # 只启动并打印地址面板（后端以**隐藏窗口**方式运行，绝不弹命令行）
#   .\tray.ps1 -NoBrowser             # 同上，但不自动打开浏览器（自动化验证用）
#   .\tray.ps1 -Action stop           # 停止
#   .\tray.ps1 -Action status         # 打印状态（非交互式调用默认走这条）
#   .\tray.ps1 -Action open           # 打开网页
#   .\tray.ps1 -Action tray -SelfTestSeconds 5   # 起托盘并在 N 秒后自动退出（自动化验证用）
#
# 两种运行形态（自动识别，不用配置）：
#   - **便携包形态**：脚本位于 <便携包>\tools\tray.ps1，同级的 app\ 里有后端与前端产物，
#     pgsql\ 是免安装 PostgreSQL，redis\ 是 Redis for Windows，data\ 与 logs\ 是运行目录。
#     这时 PostgreSQL / Redis / 后端**全部由本脚本自己拉起**，不需要 Docker、不需要 Node。
#   - **开发形态**：脚本位于 <仓库>\scripts\tray.ps1，则转调 scripts\start.ps1 / stop.ps1。
#
# 安全与隐私：日志写在 logs\ 下；本脚本不打印、不记录任何口令。
param(
  [ValidateSet('', 'start', 'stop', 'status', 'open', 'tray')][string]$Action = '',
  # 自动化验证用：起托盘后 N 秒自动退出（0 = 一直显示）
  [int]$SelfTestSeconds = 0,
  # 启动后**不**自动打开浏览器（自动化验证用；用户日常就是靠它自动弹出登录画面）
  [switch]$NoBrowser
)

$ErrorActionPreference = 'Continue'
# ⛔ 关掉进度条：PowerShell 5.1 的 Invoke-WebRequest 会往控制台写进度条，
#    而托盘每 5 秒查一次状态 —— 控制台窗口就会**不停闪烁**（2026-10-02 用户投诉"一闪一闪"）。
#    这里一关，加上 Update-Tray 不做 HTTP 请求，窗口启动打印完面板后就完全安静。
$ProgressPreference = 'SilentlyContinue'

# ---- 形态识别 ----------------------------------------------------------------
$ToolsDir = $PSScriptRoot
$PortableRoot = Split-Path -Parent $ToolsDir
$AppDir = Join-Path $PortableRoot 'app'
$IsPortable = Test-Path (Join-Path $AppDir 'lan_file_hub_server.exe')
if (-not $IsPortable) {
  # 开发形态：<仓库>\scripts\tray.ps1
  $ProjectRoot = Split-Path -Parent $ToolsDir
  $AppDir = $ProjectRoot
} else {
  $ProjectRoot = $PortableRoot
}

$LogDir = Join-Path $ProjectRoot 'logs'
$DataDir = Join-Path $ProjectRoot 'data'
$EnvFile = Join-Path $AppDir '.env'
$ServerExe = if ($IsPortable) { Join-Path $AppDir 'lan_file_hub_server.exe' } else { Join-Path $ProjectRoot 'build\debug\bin\lan_file_hub_server.exe' }
$MigrateExe = if ($IsPortable) { Join-Path $AppDir 'lan_file_hub_migrate.exe' } else { Join-Path $ProjectRoot 'build\debug\bin\lan_file_hub_migrate.exe' }
$PgCtl = Join-Path $PortableRoot 'pgsql\bin\pg_ctl.exe'
$RedisExe = Join-Path $PortableRoot 'redis\redis-server.exe'
$PgData = Join-Path $DataDir 'pg'
$RedisData = Join-Path $DataDir 'redis'

New-Item -ItemType Directory -Force -Path $LogDir, $DataDir | Out-Null

# ---- 小工具 ------------------------------------------------------------------
function Read-DotEnvValue([string]$Path, [string]$Name, [string]$Default) {
  if (Test-Path $Path) {
    foreach ($line in (Get-Content $Path -Encoding UTF8 -ErrorAction SilentlyContinue)) {
      if ($line -match ('^\s*' + [regex]::Escape($Name) + '\s*=\s*(.*)$')) {
        $value = $Matches[1].Trim()
        if ($value.Length -ge 2 -and (($value.StartsWith('"') -and $value.EndsWith('"')) -or ($value.StartsWith("'") -and $value.EndsWith("'")))) {
          $value = $value.Substring(1, $value.Length - 2)
        }
        return $value
      }
    }
  }
  return $Default
}

function Test-Port([int]$Port, [int]$TimeoutMs = 400) {
  $client = New-Object System.Net.Sockets.TcpClient
  try {
    $task = $client.ConnectAsync('127.0.0.1', $Port)
    if ($task.Wait($TimeoutMs)) { return $client.Connected }
    return $false
  } catch { return $false } finally { $client.Close() }
}

# 本机局域网 IPv4（手机要用的那个地址）：跳过回环、169.254 与虚拟网卡
function Get-LanAddress {
  $candidates = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
    Where-Object { $_.InterfaceAlias -notlike '*VMware*' -and $_.InterfaceAlias -notlike '*vEthernet*' -and $_.InterfaceAlias -notlike '本地连接*' }
  $preferred = $candidates | Where-Object { $_.PrefixOrigin -ne 'WellKnown' } | Select-Object -First 1
  if ($preferred) { return $preferred.IPAddress }
  if ($candidates) { return ($candidates | Select-Object -First 1).IPAddress }
  return ''
}

# ---- 配置 --------------------------------------------------------------------
$HttpPort = [int](Read-DotEnvValue $EnvFile 'HTTP_PORT' '9080')
$PgPort = [int](Read-DotEnvValue $EnvFile 'PGPORT' '55432')
$RedisPort = [int](Read-DotEnvValue $EnvFile 'REDISPORT' '56379')
$WebRoot = Read-DotEnvValue $EnvFile 'WEB_ROOT' ''
$LanAddress = Get-LanAddress
# 打开网页时该用哪个端口：
#   - 便携包：后端自己托管前端（WEB_ROOT），用 HTTP_PORT；
#   - 开发形态：界面在 vite（5173）上，若它在跑就用 5173，否则退回后端端口。
$WebPort = $HttpPort
if (-not $IsPortable -and (Test-Port 5173)) { $WebPort = 5173 }
$WebUrlLocal = 'http://127.0.0.1:' + $WebPort + '/'
$WebUrlLan = if ($LanAddress) { 'http://' + $LanAddress + ':' + $WebPort + '/' } else { '' }

function Get-StackStatus {
  $backendUp = Test-Port $HttpPort
  $webRootOk = $false
  if ($backendUp) {
    try {
      $response = Invoke-WebRequest -Uri ($WebUrlLocal) -UseBasicParsing -TimeoutSec 3
      $webRootOk = ($response.StatusCode -eq 200)
    } catch { $webRootOk = $false }
  }
  return [ordered]@{
    Portable = $IsPortable
    Backend = $backendUp
    WebPage = $webRootOk
    Postgres = if ($IsPortable) { Test-Port $PgPort } else { Test-Port 5432 }
    Redis = if ($IsPortable) { Test-Port $RedisPort } else { Test-Port 6379 }
    WebUrl = if ($webRootOk) { $WebUrlLocal } else { '' }
    LanUrl = if ($webRootOk -and $WebUrlLan) { $WebUrlLan } else { '' }
  }
}

function Write-Status {
  $status = Get-StackStatus
  Write-Host ''
  Write-Host '  lan-file-hub 状态' -ForegroundColor Cyan
  Write-Host ('    后端(' + $HttpPort + '):  ' + $(if ($status.Backend) { '运行中' } else { '未运行' }))
  Write-Host ('    数据库:        ' + $(if ($status.Postgres) { '运行中' } else { '未运行' }))
  Write-Host ('    Redis:         ' + $(if ($status.Redis) { '运行中' } else { '未运行' }))
  if ($status.WebPage) {
    Write-Host ('    电脑打开:      ' + $status.WebUrl)
    if ($status.LanUrl) { Write-Host ('    手机打开:      ' + $status.LanUrl) -ForegroundColor Yellow }
  } elseif ($status.Backend) {
    Write-Host '    网页托管:      未接入（后端还没实现 WEB_ROOT，只有接口可用）' -ForegroundColor DarkGray
  }
  Write-Host ''
}

# ---- 启动 / 停止 -------------------------------------------------------------
function Start-Stack {
  if ($IsPortable) {
    if (-not (Test-Path $PgCtl)) { Write-Host '  找不到便携 PostgreSQL（pgsql\bin\pg_ctl.exe），先跑 scripts\portable-setup.ps1' -ForegroundColor Red; return $false }
    if (-not (Test-Port $PgPort)) {
      Write-Host '  启动数据库…' -ForegroundColor Cyan
      # ⛔ 这里**既不能用 `| Out-Null`、也不能用 `*>` 重定向**：postgres 子进程会继承那个管道/重定向句柄，
      #    PowerShell 5.1 于是一直等一个永远不来的 EOF —— 现象是 pg-start.log 写着"服务器进程已经启动"、
      #    数据库端口也通了，脚本却**永久卡在这一行**，后面的 Redis 与后端根本不启动，托盘也不出来
      #    （2026-09-21 实测复现，用户双击桌面按钮就是这个症状）。
      #    正确做法：Start-Process 把输出重定向到**文件**，然后用**轮询端口**判断是否就绪。
      # ⛔ Start-Process 的 -ArgumentList 传**数组**时不会给含空格的元素自动加引号：
      #    `'-o', ('-p ' + $PgPort + ' -h 127.0.0.1')` 会被拆成两个参数，pg_ctl 直接报
      #    「无效的参数模式 "55432"」拒绝启动（2026-09-21 实测踩到）。这里拼成一整条带引号的字符串。
      $pgStartArgs = '-D "' + $PgData + '" -l "' + (Join-Path $LogDir 'postgres.log') + '" -o "-p ' + $PgPort + ' -h 127.0.0.1" start'
      Start-Process -FilePath $PgCtl -WindowStyle Hidden -ArgumentList $pgStartArgs -RedirectStandardOutput (Join-Path $LogDir 'pg-start.log') -RedirectStandardError (Join-Path $LogDir 'pg-start.err') | Out-Null
      for ($i = 0; $i -lt 40 -and -not (Test-Port $PgPort); $i++) { Start-Sleep -Milliseconds 500 }
      if (-not (Test-Port $PgPort)) {
        Write-Host '  数据库没起来，日志末尾：' -ForegroundColor Yellow
        if (Test-Path (Join-Path $LogDir 'postgres.log')) {
          Get-Content (Join-Path $LogDir 'postgres.log') -Encoding UTF8 -Tail 6 | ForEach-Object { Write-Host ('    ' + $_) -ForegroundColor DarkGray }
        }
        return $false
      }
      Write-Host ('  数据库已就绪（端口 ' + $PgPort + '）') -ForegroundColor Green
    }
    if (-not (Test-Port $RedisPort) -and (Test-Path $RedisExe)) {
      Write-Host '  启动 Redis…' -ForegroundColor Cyan
      # 注意：不要传 `--save ''` 这种空参数 —— Redis 7 会因此起不来（实测：进程活着但端口不监听）。
      # 数据文件默认落在 --dir 指定的目录里，足够用。
      Start-Process -FilePath $RedisExe -WindowStyle Hidden -ArgumentList @(
        '--port', $RedisPort, '--bind', '127.0.0.1', '--dir', $RedisData,
        '--logfile', (Join-Path $LogDir 'redis.log')
      ) | Out-Null
      for ($i = 0; $i -lt 20 -and -not (Test-Port $RedisPort); $i++) { Start-Sleep -Milliseconds 300 }
      if (-not (Test-Port $RedisPort)) { Write-Host '  Redis 没起来（可选组件，文件功能不受影响；看 logs\redis.log）' -ForegroundColor Yellow }
    }
  } else {
    # 开发形态：依赖由 Docker 起，交给既有脚本
    Write-Host '  开发形态：转调 scripts\start.ps1' -ForegroundColor Cyan
    # 同样不要 `*>`：start.ps1 会拉起常驻的 node/vite，它们继承句柄后本行永不返回
    $child = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @(
      '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $ProjectRoot 'scripts\start.ps1'), '-NoBrowser'
    ) -RedirectStandardOutput (Join-Path $LogDir 'start.log') -RedirectStandardError (Join-Path $LogDir 'start.err')
    [void]$child.WaitForExit(120000)
    return (Test-Port $HttpPort)
  }

  if (Test-Port $HttpPort) {
    Write-Host '  后端已在运行' -ForegroundColor Green
    return $true
  }
  if (-not (Test-Path $ServerExe)) { Write-Host ('  找不到后端程序：' + $ServerExe) -ForegroundColor Red; return $false }

  Write-Host '  启动后端（隐藏窗口，日志写 logs\backend.log）…' -ForegroundColor Cyan
  $process = Start-Process -FilePath $ServerExe -WorkingDirectory $AppDir -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $LogDir 'backend.log') `
    -RedirectStandardError (Join-Path $LogDir 'backend.err')
  Write-Host -NoNewline '  等待就绪'
  $ok = $false
  for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 500
    Write-Host -NoNewline '.'
    if ($process.HasExited) { break }
    if (Test-Port $HttpPort) { $ok = $true; break }
  }
  Write-Host ''
  if (-not $ok) {
    Write-Host '  后端没起来，日志末尾：' -ForegroundColor Yellow
    if (Test-Path (Join-Path $LogDir 'backend.log')) {
      Get-Content (Join-Path $LogDir 'backend.log') -Encoding UTF8 -Tail 6 | ForEach-Object { Write-Host ('    ' + $_) -ForegroundColor DarkGray }
    }
    if (Test-Path (Join-Path $LogDir 'backend.err')) {
      Get-Content (Join-Path $LogDir 'backend.err') -Encoding UTF8 -Tail 4 | ForEach-Object { Write-Host ('    ' + $_) -ForegroundColor DarkGray }
    }
    return $false
  }
  Write-Host '  后端已就绪' -ForegroundColor Green
  return $true
}

function Stop-Stack {
  if (-not $IsPortable) {
    & (Join-Path $ProjectRoot 'scripts\stop.ps1') *> (Join-Path $LogDir 'stop.log')
    return
  }
  # 只结束本便携包里的后端进程（按可执行文件路径匹配，绝不误杀别的实例）
  $killed = 0
  foreach ($process in @(Get-Process -Name 'lan_file_hub_server' -ErrorAction SilentlyContinue)) {
    $path = ''
    try { $path = $process.Path } catch { }
    if ($path -and ($path -ieq $ServerExe)) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      $killed++
    }
  }
  Write-Host ('  已停止后端进程：' + $killed + ' 个') -ForegroundColor Green

  foreach ($process in @(Get-Process -Name 'redis-server' -ErrorAction SilentlyContinue)) {
    $path = ''
    try { $path = $process.Path } catch { }
    if ($path -and ($path -ieq $RedisExe)) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
  }
  if ((Test-Path $PgCtl) -and (Test-Path $PgData)) {
    # 同 Start-Stack：停止也用 Start-Process，不要用 `*>`（重定向句柄同样会吊住脚本）；
    # 路径可能含空格，所以拼成整条带引号的参数串（原因见 Start-Stack）
    $pgStopArgs = '-D "' + $PgData + '" stop -m fast'
    Start-Process -FilePath $PgCtl -WindowStyle Hidden -ArgumentList $pgStopArgs -RedirectStandardOutput (Join-Path $LogDir 'pg-stop.log') -RedirectStandardError (Join-Path $LogDir 'pg-stop.err') | Out-Null
    for ($i = 0; $i -lt 30 -and (Test-Port $PgPort); $i++) { Start-Sleep -Milliseconds 500 }
  }
  Write-Host '  PostgreSQL / Redis 已停止（数据都在 data\ 里，下次启动还在）' -ForegroundColor Green
}

function Open-Web {
  $status = Get-StackStatus
  if (-not $status.Backend) {
    Write-Host '  服务没在运行：先启动' -ForegroundColor Yellow
    return
  }
  if ($status.WebPage) { Start-Process $WebUrlLocal | Out-Null } else { Start-Process ('http://127.0.0.1:' + $HttpPort + '/health/live') | Out-Null }
}

function Open-Path([string]$Path) {
  if (Test-Path $Path) { Start-Process $Path | Out-Null } else { Write-Host ('  不存在：' + $Path) -ForegroundColor Yellow }
}

# 用户要的"看得见"面板（2026-10-02 明确要求）：启动后在命令行里直接给出
# 「电脑打开哪个地址」「手机打开哪个地址」「各组件状态」「怎么用」「怎么停」，不用去翻文档。
#
# ⛔ 只允许 ASCII 装饰（= - [OK]）：East Asian Wide 的字符（中文、✅、：、→）在 chcp 65001 的
#    ConPTY 控制台里会被**重复打印一遍** —— 用户实测看到「已已启启动动 ✅✅」「电电脑脑打打开开：：」。
#    所以这里不再用 ✅/❌/═/─/→，入口脚本也不再 chcp 65001（让控制台留在系统默认代码页）。
function Get-Mark([bool]$Ok) { if ($Ok) { '[OK]' } else { '[--]' } }

function Show-Panel {
  $status = Get-StackStatus
  $ready = ($status.Backend -and $status.WebPage)
  Write-Host ''
  Write-Host '  ======================================================' -ForegroundColor DarkCyan
  Write-Host ('   lan-file-hub   ' + $(if ($ready) { '已启动，可以用了' } else { '还没就绪，请稍等或看日志' })) -ForegroundColor Cyan
  Write-Host '  ======================================================' -ForegroundColor DarkCyan
  Write-Host ''
  Write-Host ('   电脑打开：  ' + $WebUrlLocal) -ForegroundColor White
  if ($WebUrlLan) {
    Write-Host ('   手机打开：  ' + $WebUrlLan) -ForegroundColor Yellow
    Write-Host '               （手机要连同一个 WiFi 或手机热点）' -ForegroundColor DarkGray
  } else {
    Write-Host '   手机打开：  没找到局域网 IP，先让电脑连上 WiFi/热点' -ForegroundColor Yellow
  }
  Write-Host ''
  Write-Host ('   数据库 ' + (Get-Mark $status.Postgres) + '   Redis ' + (Get-Mark $status.Redis) + '   后端 ' + (Get-Mark $status.Backend) + '   网页 ' + (Get-Mark $status.WebPage)) -ForegroundColor Gray
  if ($IsPortable -and (Test-Path (Join-Path $PortableRoot 'logs\bootstrap-admin.txt'))) {
    Write-Host '   管理员口令：见 logs\bootstrap-admin.txt（登录后改掉口令并删掉这个文件）' -ForegroundColor DarkGray
  }
  Write-Host ''
  Write-Host '   --- 怎么用 ------------------------------------------' -ForegroundColor DarkGray
  Write-Host '   1) 电脑：浏览器已自动打开，登录后在「我的文件」里上传' -ForegroundColor Gray
  Write-Host '   2) 手机：浏览器输入上面"手机打开"的地址；或用手机 App 连接' -ForegroundColor Gray
  Write-Host '   3) 同一个账号在手机登录，看到的就是同一批文件（不用"传"）' -ForegroundColor Gray
  Write-Host '   停止服务：右键右下角托盘图标 ->「停止」（或双击 停止.cmd）' -ForegroundColor Gray
  Write-Host '   本窗口可最小化；直接关掉窗口不会停止服务' -ForegroundColor DarkGray
  Write-Host ''
}

# ---- 托盘 --------------------------------------------------------------------
function Show-Tray {
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -AssemblyName System.Drawing

  $notify = New-Object System.Windows.Forms.NotifyIcon
  $notify.Icon = [System.Drawing.SystemIcons]::Application
  $notify.Text = 'lan-file-hub'
  $notify.Visible = $true

  $menu = New-Object System.Windows.Forms.ContextMenuStrip
  function Add-MenuItem([string]$Text, [scriptblock]$Handler) {
    $item = New-Object System.Windows.Forms.ToolStripMenuItem
    $item.Text = $Text
    $item.add_Click($Handler)
    [void]$menu.Items.Add($item)
    return $item
  }

  $startItem = Add-MenuItem '启动' { Start-Stack | Out-Null; Update-Tray }
  $stopItem = Add-MenuItem '停止' { Stop-Stack; Update-Tray }
  [void]$menu.Items.Add((New-Object System.Windows.Forms.ToolStripSeparator))
  [void](Add-MenuItem '打开网页' { Open-Web })
  [void](Add-MenuItem '查看日志' { Open-Path $LogDir })
  [void](Add-MenuItem '打开数据目录' { Open-Path $DataDir })
  [void](Add-MenuItem '使用说明' {
    $guide = Join-Path $PortableRoot '使用说明.txt'
    if (Test-Path $guide) { Start-Process $guide | Out-Null } else { Start-Process (Join-Path $ProjectRoot 'docs\guide.md') | Out-Null }
  })
  [void]$menu.Items.Add((New-Object System.Windows.Forms.ToolStripSeparator))
  [void](Add-MenuItem '退出（不停止服务）' { $notify.Visible = $false; [System.Windows.Forms.Application]::Exit() })

  $notify.ContextMenuStrip = $menu

  function Update-Tray {
    # ⛔ 这个函数每 5 秒跑一次，**绝不许**在里面发 HTTP 请求或产生任何控制台输出
    #    （进度条/输出会让窗口一闪一闪）。只用 TCP 探活；
    #    HTTP 探测只放在 Show-Panel / Open-Web 这类一次性动作里。
    $backendUp = Test-Port $HttpPort
    if ($backendUp) {
      $notify.Text = 'lan-file-hub：运行中' + $(if ($WebUrlLan) { '（手机：' + $WebUrlLan + '）' } else { '' })
      $startItem.Enabled = $false
      $stopItem.Enabled = $true
    } else {
      $notify.Text = 'lan-file-hub：未运行（双击启动）'
      $startItem.Enabled = $true
      $stopItem.Enabled = $false
    }
  }

  $notify.add_DoubleClick({ Open-Web })
  $notify.add_MouseClick({
    param($sender, $eventArgs)
    if ($eventArgs.Button -eq [System.Windows.Forms.MouseButtons]::Left) { Open-Web }
  })

  Update-Tray
  # 双击图标 = 直接能用：托盘起来时如果服务没在跑，就自动启动一次（用户不用再点菜单）
  if (-not (Test-Port $HttpPort)) {
    Write-Host '  正在启动服务…' -ForegroundColor Cyan
    [void](Start-Stack)
    Update-Tray
  }
  # ⛔ 这里**不要**用 $notify.ShowBalloonTip：用户 2026-10-02 明确说"右下角每次都会弹出 powershell
  #    的消息"，要的是安静启动 —— 信息一律打在窗口里，不弹气泡。（托盘图标的悬停提示保留。）

  # 让用户一眼看到地址（而不是只有一个藏在右下角的图标），并自动弹出登录画面
  Show-Panel
  if (-not $NoBrowser -and (Test-Port $HttpPort)) { Start-Process $WebUrlLocal | Out-Null }

  $timer = New-Object System.Windows.Forms.Timer
  $timer.Interval = 5000
  $timer.add_Tick({ Update-Tray })
  $timer.Start()

  if ($SelfTestSeconds -gt 0) {
    $exitTimer = New-Object System.Windows.Forms.Timer
    $exitTimer.Interval = $SelfTestSeconds * 1000
    $exitTimer.add_Tick({
      $notify.Visible = $false
      $notify.Dispose()
      [System.Windows.Forms.Application]::Exit()
    })
    $exitTimer.Start()
  }

  [System.Windows.Forms.Application]::Run()
  $timer.Stop()
  $notify.Visible = $false
  $notify.Dispose()
}

# ---- 入口 --------------------------------------------------------------------
if ($Action) {
  switch ($Action) {
    'start' {
      [void](Start-Stack)
      Show-Panel
      if (-not $NoBrowser -and (Test-Port $HttpPort)) { Start-Process $WebUrlLocal | Out-Null }
    }
    'stop' { Stop-Stack }
    'status' { Write-Status }
    'open' { Open-Web }
    'tray' { Show-Tray }
  }
  exit 0
}

# 非交互式调用（stdin 被重定向）时不要挂在那里等：打印状态后退出
if ([Console]::IsInputRedirected) {
  Write-Host '  检测到非交互式调用：请用 -Action start|stop|status|open|tray' -ForegroundColor Yellow
  Write-Status
  exit 0
}

Show-Tray
