# Captura la serie del aparato a un archivo, con hora de pared por linea.
#
# Existe para el diagnostico de docs/HANDOFF-WIFI.md: dejarlo corriendo un dia
# y leer los [WiFi] caida: reason=... y los [Boot] motivo=... que aparezcan.
# La hora de pared importa por dos cosas que los millis del aparato no dan:
# correlacionar con el log del proxy en el VPS, y detectar un reinicio aunque
# no se capture el arranque (los millis van para atras).
#
# Reabre el puerto en bucle: sobrevive a los reinicios del aparato, a la
# re-enumeracion del CDC y a los cuelgues conocidos del USB. Mientras corre
# RETIENE el puerto: antes de flashear hay que pararlo (Stop-Process sobre el
# powershell que lo corre, o cerrar la ventana).
#
#   powershell -NoProfile -File tools\capture_serie.ps1
#   powershell -NoProfile -File tools\capture_serie.ps1 -Port COM3 -Out output\serie.log
param(
    [string]$Port = 'COM3',
    [string]$Out = ''
)

if ($Out -eq '') {
    $Out = Join-Path $PSScriptRoot ("..\output\serie-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$dir = Split-Path -Parent $Out
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

"[$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))] --- captura iniciada en $Port ---" |
    Out-File -FilePath $Out -Append -Encoding utf8

$p = $null
while ($true) {
    if ($null -eq $p) {
        try {
            $p = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'one'
            $p.ReadTimeout = 2000
            $p.DtrEnable = $true
            $p.Open()
            "[$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))] --- puerto abierto ---" |
                Out-File -FilePath $Out -Append -Encoding utf8
        } catch {
            $p = $null
            Start-Sleep -Milliseconds 500
            continue
        }
    }
    try {
        $linea = $p.ReadLine()
        "[$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))] $linea" |
            Out-File -FilePath $Out -Append -Encoding utf8
    } catch [System.TimeoutException] {
        # sin datos: normal, el aparato imprime cada tanto
    } catch {
        "[$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))] --- puerto perdido, reintentando ---" |
            Out-File -FilePath $Out -Append -Encoding utf8
        try { $p.Close() } catch {}
        $p = $null
    }
}
