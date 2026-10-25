param(
    [ValidateSet('build','flash','build-flash')]
    [string]$Action = 'build',
    [string]$Keil = 'D:\Keil_v5\UV4\UV4.exe'
)

# 使用现有Keil工程的下载器配置，不依赖窗口点击。
# flash会改写ZE板上的Flash，仅在明确需要下载时调用。
$ErrorActionPreference = 'Stop'
$mouseRoot = Split-Path -Parent $PSScriptRoot
$mouseProject = Join-Path $mouseRoot 'Project\Project.uvprojx'
$mouseLogs = Join-Path $mouseRoot 'Project\AutomationLogs'
if (-not (Test-Path -LiteralPath $Keil)) { throw "找不到Keil：$Keil" }
if (-not (Test-Path -LiteralPath $mouseProject)) { throw '找不到鼠标工程' }
if (Get-Process UV4 -ErrorAction SilentlyContinue) {
    throw '请先保存并关闭所有Keil窗口，避免调试连接占用或旧配置覆盖工程。'
}

if ($Action -ne 'build') {
    # 当前已验证的是ATK CMSIS-DAP。仅检查USB枚举，不等于SWD已经连通目标。
    $mouseProbe = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.Status -eq 'OK' -and $_.FriendlyName -eq 'ATK-FS-WINUSB-CMSIS-DAP' }
    if (-not $mouseProbe) { throw '未检测到已验证的ATK CMSIS-DAP下载器，停止烧录。' }
}

New-Item -ItemType Directory -Path $mouseLogs -Force | Out-Null
$mouseStamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
function Invoke-MouseKeil([string]$mode, [string]$operation) {
    $mouseLog = Join-Path $mouseLogs ($mouseStamp + '_' + $operation + '.log')
    $mouseArguments = @($mode, ('"' + $mouseProject + '"'), '-o', ('"' + $mouseLog + '"'))
    if ($operation -eq 'build') { $mouseArguments += '-j0' }
    $mouseProcess = Start-Process -FilePath $Keil -ArgumentList $mouseArguments -WindowStyle Hidden -PassThru
    if (-not $mouseProcess.WaitForExit(60000)) {
        # 不强杀下载器进程，避免中断进行中的Flash写入。
        throw "Keil超过60秒仍未结束，进程=$($mouseProcess.Id)，请检查；不要重复启动下载。日志：$mouseLog"
    }
    if (-not (Test-Path -LiteralPath $mouseLog)) { throw "没有生成操作日志：$mouseLog" }
    $mouseText = Get-Content -LiteralPath $mouseLog -Raw
    Write-Output $mouseText
    if ($operation -eq 'build') {
        # Keil返回1可表示只有warning；明确检查日志的零错误结果。
        if ($mouseProcess.ExitCode -gt 1 -or $mouseText -notmatch '0 Error\(s\)') {
            throw "编译未通过，禁止继续下载：$mouseLog"
        }
    } else {
        if ($mouseProcess.ExitCode -ne 0 -or $mouseText -notmatch 'Programming Done' -or
            $mouseText -notmatch 'Verify OK' -or $mouseText -notmatch 'Application running') {
            throw "下载/校验/启动没有全部得到确认，查看日志：$mouseLog"
        }
    }
    Write-Output "已完成 $operation；日志：$mouseLog"
}

if ($Action -in @('build','build-flash')) { Invoke-MouseKeil '-b' 'build' }
if ($Action -in @('flash','build-flash')) { Invoke-MouseKeil '-f' 'flash' }
