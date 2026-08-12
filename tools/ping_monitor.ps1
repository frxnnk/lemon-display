# Monitorea la red de casa con ping continuo y registra SOLO los cortes.
#
# Existe para dirimir la pregunta de docs/HANDOFF-WIFI.md desde el lado de la
# PC: si se corta el gateway y afuera a la vez, es la WiFi o el router; si el
# gateway sigue respondiendo y afuera se corta, es el WAN/ISP y la WiFi no
# tiene la culpa.
#
# Registra el primer fallo, la vuelta (con la duracion del corte) y un latido
# cada 30 min para saber que sigue vivo. Un fallo suelto de un ping no es un
# corte: se exigen 2 seguidos, porque un paquete perdido en WiFi es normal.
#
#   powershell -NoProfile -File tools\ping_monitor.ps1
param(
    [string[]]$Targets = @('192.168.1.1', '8.8.8.8'),
    [string]$Out = ''
)

if ($Out -eq '') {
    $Out = Join-Path $PSScriptRoot ("..\output\ping-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$dir = Split-Path -Parent $Out
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

function Sello { [DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss') }
function Log([string]$m) { "[$(Sello)] $m" | Out-File -FilePath $Out -Append -Encoding utf8 }

Log ("--- monitor iniciado: {0}, 1 ping/s por destino ---" -f ($Targets -join ' y '))

$ping = New-Object System.Net.NetworkInformation.Ping
$estado = @{}
foreach ($t in $Targets) {
    $estado[$t] = @{ fallosSeguidos = 0; caidoDesde = $null; cortes = 0; perdidos = 0; enviados = 0 }
}
$ultimoLatido = Get-Date

while ($true) {
    foreach ($t in $Targets) {
        $e = $estado[$t]
        $e.enviados++
        $ok = $false
        try { $ok = ($ping.Send($t, 1500).Status -eq 'Success') } catch { $ok = $false }

        if ($ok) {
            if ($null -ne $e.caidoDesde) {
                $dur = [int]((Get-Date) - $e.caidoDesde).TotalSeconds
                Log ("VUELVE  {0}  tras {1} s" -f $t, $dur)
                $e.caidoDesde = $null
            }
            $e.fallosSeguidos = 0
        } else {
            $e.perdidos++
            $e.fallosSeguidos++
            if ($e.fallosSeguidos -eq 2) {
                # El corte empezo en el primer fallo de la racha, hace ~2 s.
                $e.caidoDesde = (Get-Date).AddSeconds(-2)
                $e.cortes++
                Log ("CORTE   {0}" -f $t)
            }
        }
    }

    if (((Get-Date) - $ultimoLatido).TotalMinutes -ge 30) {
        $ultimoLatido = Get-Date
        $resumen = foreach ($t in $Targets) {
            $e = $estado[$t]
            "{0}: {1} cortes, {2}/{3} perdidos" -f $t, $e.cortes, $e.perdidos, $e.enviados
        }
        Log ("latido  {0}" -f ($resumen -join '  |  '))
    }

    Start-Sleep -Seconds 1
}
