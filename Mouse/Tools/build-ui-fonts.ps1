param([switch]$TextsOnly)
$ErrorActionPreference='Stop'
$mouseRoot=Split-Path -Parent $PSScriptRoot
$mouseMessages=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ui-texts.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$mouseUtf8=[System.Text.UTF8Encoding]::new($false)
$mouseHeader="#ifndef MOUSE_UI_TEXTS_H`n#define MOUSE_UI_TEXTS_H`n/* Generated UTF-8 bytes; edit Tools/ui-texts.json, not this file. */`n"
$mouseCharacters=[System.Collections.Generic.SortedSet[char]]::new()
foreach($mouseProperty in $mouseMessages.PSObject.Properties){
    $mouseBytes=[System.Text.Encoding]::UTF8.GetBytes([string]$mouseProperty.Value)
    $mouseEscaped=-join ($mouseBytes | ForEach-Object { '\x'+$_.ToString('x2') })
    $mouseHeader+='#define UI_TXT_'+$mouseProperty.Name+' "'+$mouseEscaped+'"'+"`n"
    foreach($mouseChar in ([string]$mouseProperty.Value).ToCharArray()){
        if([int]$mouseChar -gt 127){[void]$mouseCharacters.Add($mouseChar)}
    }
}
$mouseHeader+="#endif`n"
# 自动生成物：明确UTF-8字节，避免AC5按GBK解析中文字符串。
[System.IO.File]::WriteAllText((Join-Path $mouseRoot 'Core\Inc\ui_texts.h'),$mouseHeader,$mouseUtf8)
foreach($mouseSize in 16,20){
    $mouseOutput=Join-Path $mouseRoot "Core\Src\ui_font_$mouseSize.c"
    if(-not $TextsOnly){
        & node (Join-Path $PSScriptRoot 'font-tools\node_modules\lv_font_conv\lv_font_conv.js') --font (Join-Path $mouseRoot 'ThirdParty\fonts\MouseUi-subset.woff2') --symbols (-join $mouseCharacters) --range 0x20-0x7e --size $mouseSize --bpp 4 --format lvgl --no-compress --no-kerning --lv-include lvgl.h --output $mouseOutput
        if($LASTEXITCODE -ne 0){throw 'Font conversion failed'}
    }
    if(Test-Path -LiteralPath $mouseOutput){
        $mouseCurrent=Get-Content -LiteralPath $mouseOutput -Raw -Encoding UTF8
        foreach($mouseChar in $mouseCharacters){
            if($mouseCurrent -notmatch ('U\+'+([int]$mouseChar).ToString('X4')+'\b')){
                throw '新增文字超出现有字库，请先更新ThirdParty/fonts/MouseUi-subset.woff2，再重新生成。'
            }
        }
    }
}
Write-Output ('UTF-8文本及中文字形检查通过，共'+$mouseCharacters.Count+'个非ASCII字符。')
