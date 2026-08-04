# Compila el simulador. Usa el MISMO ui_ferced.cpp que el firmware y la MISMA
# LovyanGFX, con backend SDL en vez del panel RGB.

$ErrorActionPreference = 'Stop'
$sim  = $PSScriptRoot
$root = Split-Path $sim -Parent

$gccBin = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
if (-not (Test-Path "$gccBin\g++.exe")) { throw "no encuentro g++ en $gccBin" }

$sdl = Get-ChildItem "$sim\vendor" -Directory -Filter 'SDL2-*' | Select-Object -First 1
if (-not $sdl) { throw "falta SDL2 en sim/vendor" }
$sdlRoot = "$($sdl.FullName)\x86_64-w64-mingw32"

$lgfx = Get-ChildItem "$root\.pio\libdeps" -Directory -Recurse -Filter 'LovyanGFX' |
        Select-Object -First 1
if (-not $lgfx) { throw "no encuentro LovyanGFX en .pio/libdeps" }

Write-Host "g++      : $gccBin"
Write-Host "SDL2     : $sdlRoot"
Write-Host "LovyanGFX: $($lgfx.FullName)"

$obj = "$sim\build"
New-Item -ItemType Directory -Path $obj -Force | Out-Null

# LovyanGFX se compila entero: sus guardas de plataforma dejan afuera lo que no
# corresponde al host.
$lgfxSrc = Get-ChildItem "$($lgfx.FullName)\src" -Recurse -Filter '*.cpp' |
           Where-Object { $_.FullName -notmatch '\\platforms\\(esp32|arduino_default|samd|spresense|opencv|framebuffer)' } |
           ForEach-Object { $_.FullName }

$sources = @(
    "$sim\src\sdl_main.cpp",
    "$sim\src\sim_main.cpp",
    "$sim\src\sim_display.cpp",
    "$sim\src\sim_feed.cpp",
    "$sim\src\sim_stubs.cpp",
    "$root\src\ui_ferced.cpp",
    "$root\src\ui_config.cpp"
) + $lgfxSrc

$incs = @(
    "-I`"$sim\shim`"",
    "-I`"$sim\src`"",
    "-I`"$root\src`"",
    "-I`"$($lgfx.FullName)\src`"",
    "-I`"$sdlRoot\include`"",
    "-I`"$sdlRoot\include\SDL2`""
)

$flags = @('-std=c++17','-O2','-w','-DLGFX_SDL','-DSDL_MAIN_HANDLED','-DFERCED_SIM')
$libs  = @("-L`"$sdlRoot\lib`"", '-lmingw32', '-lSDL2main', '-lSDL2', '-lsetupapi', '-limm32', '-lversion', '-lwinmm', '-lole32', '-loleaut32', '-lgdi32', '-luser32')

# LovyanGFX trae 10 archivos en C (qrcode, jpeg, png, qoi, fuentes CJK). Van
# compilados como C, no como C++, asi que se hacen en una pasada aparte.
$cSrc = Get-ChildItem "$($lgfx.FullName)\src" -Recurse -Filter '*.c' | ForEach-Object { $_.FullName }
$cObjs = @()
Write-Host "compilando $($cSrc.Count) archivos C..."
foreach ($f in $cSrc) {
    $o = "$obj\c_$([System.IO.Path]::GetFileNameWithoutExtension($f)).o"
    # -std=c11: GCC 16 usa C23 por defecto, donde "bool" es palabra reservada,
    # y el codigo de LovyanGFX hace typedef unsigned char bool.
    $cArgs = @('-std=c11','-O2','-w','-c', ($f -replace '\\','/'), '-o', ($o -replace '\\','/'),
               "-I$($lgfx.FullName)/src" -replace '\\','/')
    $crsp = "$obj\c.rsp"
    Set-Content -Path $crsp -Value (($cArgs | ForEach-Object { $_ -replace '\\','/' }) -join "`n") -Encoding ASCII
    cmd /c "`"$gccBin\gcc.exe`" @`"$crsp`" >> `"$obj\compile.log`" 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "fallo compilando $f" }
    $cObjs += $o
}

Write-Host "`ncompilando $($sources.Count) archivos C++..."

# Argumentos sin comillas manuales: PowerShell los pasa uno a uno al proceso,
# que es lo unico que sobrevive rutas con espacios.
$argv = @()
$argv += $flags
$argv += @("-I$sim\shim", "-I$sim\src", "-I$root\src",
           "-I$($lgfx.FullName)\src", "-I$sdlRoot\include", "-I$sdlRoot\include\SDL2")
$argv += $sources
$argv += $cObjs
$argv += @('-o', "$obj\ferced-sim.exe")
# Enlazado estatico del runtime de GCC: si Windows encuentra en el PATH una
# libstdc++ o libwinpthread mas vieja, el exe muere con 0xC0000139 (punto de
# entrada no encontrado). Estatico lo vuelve autocontenido.
$argv += @('-static', '-static-libgcc', '-static-libstdc++')
$argv += @("-L$sdlRoot\lib", '-lmingw32', '-lSDL2main', '-lSDL2', '-lsetupapi',
           '-limm32', '-lversion', '-lwinmm', '-lole32', '-loleaut32', '-lgdi32', '-luser32')

# PowerShell 5.1 envuelve la salida de error de un exe nativo en ErrorRecords y
# se pierde. Un archivo de respuesta mas redireccion de cmd la conserva entera.
$rsp = "$obj\args.rsp"
# GCC toma "\" como escape dentro de un archivo de respuesta y se come las
# rutas. Las barras normales funcionan igual en Windows.
Set-Content -Path $rsp -Value (($argv | ForEach-Object { $_ -replace '\\','/' }) -join "`n") -Encoding ASCII
cmd /c "`"$gccBin\g++.exe`" @`"$rsp`" > `"$obj\compile.log`" 2>&1"

if ($LASTEXITCODE -eq 0) {
    Copy-Item "$sdlRoot\bin\SDL2.dll" $obj -Force
    Write-Host "`nOK -> $obj\ferced-sim.exe"
} else {
    throw "fallo la compilacion"
}

