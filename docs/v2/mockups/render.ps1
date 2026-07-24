param(
  [string]$ChromePath = "C:\Program Files\Google\Chrome\Application\chrome.exe"
)

$ErrorActionPreference = "Stop"
$mockupDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$outputDir = Join-Path $mockupDir "rendered"

if (-not (Test-Path -LiteralPath $ChromePath)) {
  throw "Chrome no encontrado en $ChromePath"
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$indexUri = ([System.Uri](Join-Path $mockupDir "index.html")).AbsoluteUri

foreach ($scene in @("home", "tape", "news")) {
  $output = Join-Path $outputDir "$scene-480x480.png"
  & $ChromePath `
    --headless=new `
    --disable-gpu `
    --hide-scrollbars `
    --force-device-scale-factor=1 `
    --window-size=480,480 `
    --screenshot=$output `
    "${indexUri}?scene=$scene" | Out-Null

  if (-not (Test-Path -LiteralPath $output)) {
    throw "No se generó $output"
  }
}

Get-ChildItem -LiteralPath $outputDir -Filter "*-480x480.png" |
  Select-Object Name, Length
