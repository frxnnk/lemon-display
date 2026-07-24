param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^COM\d+$')]
  [string]$Port,

  [switch]$ConfirmCanary
)

$ErrorActionPreference = "Stop"

if (-not $ConfirmCanary) {
  throw "Falta -ConfirmCanary. Este script escribe firmware en una sola unidad."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$environment = "matouch_esp32s3_40_v2_demo"
$firmware = Join-Path $repoRoot ".pio\build\$environment\firmware.bin"

$env:LEMON_V2_CANARY_PORT = $Port
$detectedVid = & python -c @'
import os
import serial.tools.list_ports as lp
port = os.environ['LEMON_V2_CANARY_PORT'].upper()
match = next((item for item in lp.comports() if item.device.upper() == port), None)
print(f'0x{match.vid:04X}' if match and match.vid is not None else 'NONE')
'@

if ($detectedVid.Trim() -ne "0x303A") {
  throw "$Port no es una Lemon Box ESP32-S3 detectada con VID 0x303A (VID: $detectedVid)."
}

if (-not (Test-Path -LiteralPath $firmware)) {
  & python -m platformio run -e $environment
  if ($LASTEXITCODE -ne 0) {
    throw "Fallo la compilacion de $environment."
  }
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $firmware).Hash
Write-Host "Canary: $Port"
Write-Host "Firmware: $firmware"
Write-Host "SHA-256: $hash"

$flashArgs = @(
  "--chip", "esp32s3",
  "--port", $Port,
  "--baud", "921600",
  "--before", "default-reset",
  "--after", "hard-reset",
  "write-flash",
  "--flash-mode", "keep",
  "--flash-size", "keep",
  "0x10000", $firmware
)

& python -m esptool @flashArgs
if ($LASTEXITCODE -ne 0) {
  throw "Flash interrumpido. No repetir este script: seguir el procedimiento recovery del skill lemon-box-flasher."
}

Write-Host "Flash verificado por esptool. Confirmar visualmente home V2 y el ciclo de 78 segundos."
