# Deja el aparato listo para regalar, en un solo comando.
#
#   1. Flashea el firmware normal y espera a que se conecte.
#   2. GUARDA la lista de tareas del dueno actual en un archivo, leyendola del
#      servidor que el propio aparato levanta. Si no la puede guardar, FRENA:
#      borrar la lista de alguien sin copia no se arregla despues.
#   3. Flashea el firmware de un solo uso, que le pone nombre y le borra tareas,
#      avisos y WiFi.
#   4. Vuelve a flashear el normal, para que el aparato no se quede con el
#      firmware de preparacion puesto.
#
#   .\tools\preparar_regalo.ps1                  -> con lo que diga FERCED_REGALO
#   .\tools\preparar_regalo.ps1 -SoloGuardar     -> solo el paso 2, sin borrar nada
#
# El nombre sale de platformio.ini (env ferced_display_regalo, -DFERCED_REGALO).

param(
    [string]$Puerto = "COM3",
    [switch]$SoloGuardar,
    [string]$Backup = "output\tareas-respaldo.json"
)

$ErrorActionPreference = 'Stop'
$raiz = Split-Path $PSScriptRoot -Parent
Set-Location $raiz

function Flashear($env) {
    Write-Host "`n[1/4] compilando y flasheando $env ..." -ForegroundColor Cyan
    python -m platformio run -e $env
    if ($LASTEXITCODE -ne 0) { throw "no compilo $env" }
    python -m esptool --chip esp32s3 --port $Puerto --baud 921600 `
        --before default-reset --after hard-reset write-flash `
        --flash-mode keep --flash-size keep 0x10000 ".pio\build\$env\firmware.bin"
    if ($LASTEXITCODE -ne 0) { throw "no pude flashear $env" }
}

# La IP sale del log de arranque: "[WiFi] Connected! IP: 192.168.x.x".
function BuscarIP($segundos) {
    Write-Host "[2/4] esperando que se conecte para leerle la lista..." -ForegroundColor Cyan
    $fin = (Get-Date).AddSeconds($segundos)
    while ((Get-Date) -lt $fin) {
        try {
            $sp = New-Object System.IO.Ports.SerialPort $Puerto, 115200
            $sp.ReadTimeout = 800
            $sp.Open()
            while ((Get-Date) -lt $fin) {
                try { $linea = $sp.ReadLine() } catch { continue }
                if ($linea -match 'IP:\s*(\d+\.\d+\.\d+\.\d+)') {
                    $sp.Close()
                    return $Matches[1]
                }
            }
            $sp.Close()
        } catch {
            Start-Sleep -Milliseconds 700   # el puerto reaparece despues del reset
        }
    }
    return $null
}

if (-not $SoloGuardar) { Flashear "ferced_display_vps" }

$ip = BuscarIP 90
if (-not $ip) { throw "no le vi la IP por serie: sin eso no puedo guardar la lista, asi que no borro nada" }
Write-Host "    el aparato esta en $ip" -ForegroundColor Green

New-Item -ItemType Directory -Force (Split-Path $Backup) | Out-Null
$destino = Join-Path $raiz $Backup
Invoke-WebRequest -Uri "http://$ip/api/todos" -OutFile $destino -TimeoutSec 15
Write-Host "    lista guardada en $Backup" -ForegroundColor Green
Get-Content $destino

if ($SoloGuardar) { Write-Host "`nlisto (solo guardado, no toque nada mas)" -ForegroundColor Green; exit 0 }

Write-Host "`n[3/4] preparando para regalo (nombre + borrar tareas, avisos y WiFi)..." -ForegroundColor Cyan
Flashear "ferced_display_regalo"
Start-Sleep -Seconds 12   # que arranque y haga lo suyo

Write-Host "`n[4/4] devolviendo el firmware normal..." -ForegroundColor Cyan
Flashear "ferced_display_vps"

Write-Host "`nListo. El aparato arranca en la pantalla del QR." -ForegroundColor Green
Write-Host "La red que va a levantar se llama Ferced-<nombre>, clave ferced1234."
