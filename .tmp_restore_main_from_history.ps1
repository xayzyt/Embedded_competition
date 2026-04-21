function Restore-FromHistory {
    param([string]$Target, [string]$Source)
    Copy-Item -LiteralPath $Source -Destination $Target -Force
    Write-Host "restored history -> $Target"
}

function Restore-FromLog {
    param([string]$Target, [string]$LogPath, [string]$Needle)
    $match = Select-String -Path $LogPath -Pattern $Needle | Select-Object -First 1
    if ($null -eq $match) {
        throw "log entry not found for $Target"
    }
    $jsonStart = $match.Line.IndexOf('{')
    if ($jsonStart -lt 0) {
        throw "json payload not found for $Target"
    }
    $jsonText = $match.Line.Substring($jsonStart)
    $obj = $jsonText | ConvertFrom-Json -Depth 8
    Set-Content -LiteralPath $Target -Encoding UTF8 -Value $obj.textDocument.text
    Write-Host "restored log -> $Target"
}

$historyRoot = Join-Path $env:APPDATA 'Code\User\History'
$logTask = Join-Path $env:APPDATA 'Code\logs\20260406T202332\window1\exthost\output_logging_20260406T202350\7-Gemini Code Assist.log'
$logCh32 = Join-Path $env:APPDATA 'Code\logs\20260406T202332\window1\exthost\output_logging_20260406T202350\7-Gemini Code Assist.log'

$historyMap = @{
    'main\main.c' = (Join-Path $historyRoot '5e247889\XFRt.c')
    'main\app_apriltag.c' = (Join-Path $historyRoot '2c33f942\IBCd.c')
    'main\app_camera.c' = (Join-Path $historyRoot '5f36e8ed\xWoB.c')
    'main\app_cloud.c' = (Join-Path $historyRoot '21b6236f\YGQ2.c')
    'main\app_ctrl.c' = (Join-Path $historyRoot '5b7b28e7\7gFQ.c')
    'main\app_dock_judge.c' = (Join-Path $historyRoot '14292ff9\v9Cv.c')
    'main\app_ui.c' = (Join-Path $historyRoot '52e938be\xGqj.c')
    'main\app_video.c' = (Join-Path $historyRoot '3a6066a9\ZLk3.c')
    'main\app_vision.c' = (Join-Path $historyRoot '8258b16\0YJV.c')
    'main\bsp_display_port.c' = (Join-Path $historyRoot 'a0c58ee\vufj.c')
}

foreach ($pair in $historyMap.GetEnumerator()) {
    Restore-FromHistory -Target $pair.Key -Source $pair.Value
}

Restore-FromLog -Target 'main\app_task.c' -LogPath $logTask -Needle 'uri":"file:///c%3A/beifen/ESP32_P4_EV/test/main/app_task.c"'
Restore-FromLog -Target 'main\app_ch32_link.c' -LogPath $logCh32 -Needle 'uri":"file:///c%3A/beifen/ESP32_P4_EV/test/main/app_ch32_link.c"'
