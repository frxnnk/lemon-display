param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^COM\d+$')]
  [string]$Port,
  [Parameter(Mandatory = $true)]
  [string]$ExpectedSerial,
  [switch]$ConfirmCanary,
  [switch]$ManualBootloader
)

$ErrorActionPreference = "Stop"
if (-not $ConfirmCanary) { throw "Falta -ConfirmCanary. Este script escribe firmware en una sola unidad." }

$repoRoot = Split-Path -Parent $PSScriptRoot
$environment = "matouch_esp32s3_40_v2_real"
$buildDir = Join-Path $repoRoot ".pio\build\$environment"
$firmware = Join-Path $buildDir "firmware.bin"
$builtPartitions = Join-Path $buildDir "partitions.bin"

$env:LEMON_V2_CANARY_PORT = $Port
$detectedJson = & python -c @'
import json, os
import serial.tools.list_ports as lp
port = os.environ['LEMON_V2_CANARY_PORT'].upper()
item = next((p for p in lp.comports() if p.device.upper() == port), None)
print(json.dumps({'device': item.device if item else None,
                  'vid': item.vid if item else None,
                  'pid': item.pid if item else None,
                  'serial': item.serial_number if item else None}))
'@
$detected = $detectedJson | ConvertFrom-Json
$allowedVids = @(0x303A, 0x10C4, 0x1A86, 0x0403)
if ($null -eq $detected.device -or $allowedVids -notcontains [int]$detected.vid) {
  throw "$Port no usa USB nativo ESP32-S3 ni un bridge UART permitido."
}
if ([string]::IsNullOrWhiteSpace($detected.serial) -or $detected.serial -ne $ExpectedSerial) {
  throw "Serial inesperado. Esperado: $ExpectedSerial. Detectado: $($detected.serial)."
}

& python -m platformio run -e $environment
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion de $environment." }

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$rollbackDir = Join-Path $repoRoot "output\v2-real-canary-$stamp"
New-Item -ItemType Directory -Path $rollbackDir | Out-Null
$devicePartitions = Join-Path $rollbackDir "device-partitions.bin"
$rollback = Join-Path $rollbackDir "rollback-app.bin"
$firmwareCopy = Join-Path $rollbackDir "v2-real-firmware.bin"
Copy-Item -LiteralPath $firmware -Destination $firmwareCopy

$vidHex = "0x{0:X4}" -f [int]$detected.vid
Write-Host "Canary: $Port / serial $($detected.serial) / VID $vidHex"
$readBase = @("--chip", "esp32s3", "--port", $Port, "--baud", "921600")
$initialReset = if ($ManualBootloader) { "no-reset" } else { "default-reset" }

& python -m esptool @readBase --before $initialReset --after no-reset read-flash "0x8000" "0xC00" $devicePartitions
if ($LASTEXITCODE -ne 0) { throw "No se pudo leer la tabla de particiones." }

$builtPartitionHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $builtPartitions).Hash
$devicePartitionHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $devicePartitions).Hash
if ($builtPartitionHash -ne $devicePartitionHash) {
  throw "La tabla de particiones no coincide con el build. No se escribio firmware."
}

& python -m esptool @readBase --before no-reset --after no-reset read-flash "0x10000" "0x640000" $rollback
if ($LASTEXITCODE -ne 0) { throw "No se pudo crear el rollback de la app actual." }

$firmwareHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $firmwareCopy).Hash
$rollbackHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $rollback).Hash
Write-Host "V2 SHA-256: $firmwareHash"
Write-Host "Rollback SHA-256: $rollbackHash"
Write-Host "Partitions SHA-256: $devicePartitionHash"

$flashArgs = @(
  "--chip", "esp32s3", "--port", $Port, "--baud", "921600",
  "--before", "no-reset", "--after", "hard-reset", "write-flash",
  "--flash-mode", "keep", "--flash-size", "keep", "0x10000", $firmwareCopy
)
& python -m esptool @flashArgs
if ($LASTEXITCODE -ne 0) {
  throw "Flash interrumpido. Conservar $rollbackDir y seguir recovery del skill lemon-box-flasher."
}

$rollbackCommand = "# Enter manual bootloader first when auto-reset is unavailable.`npython -m esptool --chip esp32s3 --port $Port --baud 921600 --before no-reset --after hard-reset write-flash --flash-mode keep --flash-size keep 0x10000 `"$rollback`""
Set-Content -LiteralPath (Join-Path $rollbackDir "ROLLBACK_COMMAND.txt") -Value $rollbackCommand -Encoding UTF8

$env:LEMON_V2_MONITOR_PORT = $Port
& python -c @'
import os, time
import serial.tools.list_ports as lp
port = os.environ['LEMON_V2_MONITOR_PORT'].upper()
deadline, changes, last = time.time() + 12, [], None
while time.time() < deadline:
    present = any(p.device.upper() == port for p in lp.comports())
    if present != last:
        changes.append((round(time.time(), 2), present)); last = present
    time.sleep(0.25)
print('Port stability:', changes)
if not last or len(changes) > 3:
    raise SystemExit('Puerto inestable luego del flash.')
'@
if ($LASTEXITCODE -ne 0) { throw "La canary no mantuvo un puerto USB estable." }

Write-Host "Puerto estable. Falta confirmacion visual/touch de Home, Market Tape, Contexto y Ajustes."
Write-Host "Rollback preparado en: $rollbackDir"
