Write-Host "=== Limpieza de drivers fantasma ESP32 ===" -ForegroundColor Cyan
$ghosts = Get-PnpDevice | Where-Object { $_.InstanceId -like "*303A*1001*" -and $_.Status -eq "Unknown" }
Write-Host "Encontrados: $($ghosts.Count) dispositivos fantasma"
foreach ($dev in $ghosts) {
    Write-Host "  Removing: $($dev.FriendlyName) [$($dev.InstanceId)]"
    pnputil /remove-device $dev.InstanceId 2>$null | Out-Null
}
Write-Host "`nListo! Desenchufa y volve a enchufar el Lemon Box." -ForegroundColor Green
pause
