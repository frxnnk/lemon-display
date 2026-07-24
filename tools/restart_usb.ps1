# Restart ESP32 USB device and immediately try to flash
Write-Host "=== Restarting ESP32 USB device ===" -ForegroundColor Cyan

$dev = Get-PnpDevice | Where-Object { $_.InstanceId -like "*303A*1001*" -and $_.Status -eq "OK" }
if (-not $dev) {
    $dev = Get-PnpDevice | Where-Object { $_.InstanceId -like "*303A*1001*" -and $_.Status -ne "Unknown" }
}

if ($dev) {
    foreach ($d in $dev) {
        Write-Host "  Disabling: $($d.FriendlyName)"
        Disable-PnpDevice -InstanceId $d.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 3
    foreach ($d in $dev) {
        Write-Host "  Enabling: $($d.FriendlyName)"
        Enable-PnpDevice -InstanceId $d.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 2
    Write-Host "Done! Try flashing now." -ForegroundColor Green
} else {
    Write-Host "No active ESP32 device found" -ForegroundColor Red
}
pause
