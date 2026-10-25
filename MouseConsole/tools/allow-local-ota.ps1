# Run elevated once. Never disable the firewall or modify unrelated rules.
param(
    [string]$NodePath = (Get-Command node.exe -ErrorAction Stop).Source,
    [string]$InterfaceAlias = 'WLAN'
)
$ErrorActionPreference = 'Stop'
$mouseResult = Join-Path $PSScriptRoot '..\.local\firewall-result.json'
try {
    $mouseNode = (Resolve-Path -LiteralPath $NodePath).Path
    $mouseAddress = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\.local\ota-host.txt') -Raw).Trim()
    if (-not (Test-Path -LiteralPath $mouseNode)) { throw 'Expected Node executable not found' }
    $mouseInterface = Get-NetIPAddress -AddressFamily IPv4 -IPAddress $mouseAddress
    if (-not $mouseInterface -or $mouseInterface.InterfaceAlias -ne $InterfaceAlias) { throw 'Expected interface IP not present; stop instead of broadening access' }
    $mouseRuleName = 'MouseProject-Local-MQTT-OTA-20261025'
    if (-not (Get-NetFirewallRule -Name $mouseRuleName -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -Name $mouseRuleName -DisplayName 'MouseProject LAN MQTT and OTA' `
            -Description 'User approved: Node only; WLAN local IP; local subnet; TCP 18830 and 8089.' `
            -Direction Inbound -Action Allow -Enabled True -Profile Any -InterfaceAlias $InterfaceAlias `
            -LocalAddress $mouseAddress -RemoteAddress LocalSubnet -Protocol TCP `
            -LocalPort 18830,8089 -Program $mouseNode | Out-Null
    }
    @{ok=$true;rule=$mouseRuleName;address=$mouseAddress;ports=@(18830,8089)} |
        ConvertTo-Json | Set-Content -LiteralPath $mouseResult -Encoding UTF8
} catch {
    @{ok=$false;error=$_.Exception.Message} | ConvertTo-Json |
        Set-Content -LiteralPath $mouseResult -Encoding UTF8
    exit 1
}
