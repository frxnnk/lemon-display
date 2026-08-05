# Recompila, levanta el simulador, avanza N items y captura la pantalla.
# Un solo comando por iteracion de UI.
#
#   .\shot.ps1                 -> primer item
#   .\shot.ps1 -Advance 3      -> cuarto item
#   .\shot.ps1 -Advance 3 -Mid -> a mitad de la animacion del cuarto item
#   .\shot.ps1 -Config         -> pantalla de configuracion
#   .\shot.ps1 -Progreso 45    -> configuracion con la descarga del OTA al 45%
#   .\shot.ps1 -NoBuild        -> sin recompilar

param(
    [int]$Advance = 0,
    [switch]$Mid,
    [switch]$Config,
    [switch]$Padel,
    [switch]$Launcher,
    [switch]$Tareas,
    [int]$Hacia = -1,
    [int]$Congelar = -1,
    [int]$Progreso = -1,
    [switch]$NoBuild,
    [string]$Out = "shot.png"
)

$ErrorActionPreference = 'Stop'
$sim = $PSScriptRoot

Get-Process ferced-sim -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

if (-not $NoBuild) {
    & "$sim\build.ps1" *> "$sim\build\last-build.log"
    if (-not (Test-Path "$sim\build\ferced-sim.exe")) {
        Get-Content "$sim\build\compile.log" | Where-Object { $_ -match 'error' } | Select-Object -First 8
        throw "no compilo"
    }
}

$simArgs = @("--still", "--item=$Advance")
if ($Padel)    { $simArgs += "--padel" }
if ($Tareas)   { $simArgs += "--tareas" }
if ($Hacia -ge 0)    { $simArgs += "--hacia=$Hacia" }
if ($Congelar -ge 0) { $simArgs += "--congelar=$Congelar" }
if ($Launcher) { $simArgs += "--launcher" }
if ($Config) { $simArgs += "--config" }
# --progreso ya implica la pantalla de configuracion del lado del simulador.
if ($Progreso -ge 0) { $simArgs += "--progreso=$Progreso" }
$p = Start-Process -FilePath "$sim\build\ferced-sim.exe" -ArgumentList $simArgs -WorkingDirectory $sim -PassThru
Start-Sleep -Seconds 4

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class SimWin {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

# Esperar el handle de verdad: si se captura antes de que exista la ventana,
# GetClientRect devuelve basura y se termina fotografiando la consola.
$proc = $null
for ($i = 0; $i -lt 40; $i++) {
    $proc = Get-Process -Name 'ferced-sim' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 300
    if ($proc) { $proc.Refresh() }
}
if (-not $proc -or $proc.MainWindowHandle -eq [IntPtr]::Zero) { throw "el simulador no abrio ventana" }
[SimWin]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800

if ($Mid) {
    # Reinicia la animacion y captura antes de que termine.
        Start-Sleep -Milliseconds 250
} else {
    Start-Sleep -Milliseconds 1600
}

$h = $proc.MainWindowHandle
$r = New-Object SimWin+RECT
[SimWin]::GetClientRect($h, [ref]$r) | Out-Null
$pt = New-Object SimWin+POINT
[SimWin]::ClientToScreen($h, [ref]$pt) | Out-Null

$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$dest = Join-Path "$sim\build" $Out
$bmp.Save($dest, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Write-Output "$dest  (${w}x${hh})"
