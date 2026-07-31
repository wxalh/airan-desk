$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$magick = (Get-Command magick -ErrorAction SilentlyContinue).Source
if (-not $magick) {
    $defaultMagick = "C:\Program Files\ImageMagick-7.1.2-Q16-HDRI\magick.exe"
    if (Test-Path -LiteralPath $defaultMagick) {
        $magick = $defaultMagick
    }
}

if (-not $magick) {
    throw "ImageMagick magick.exe not found. Add it to PATH or update tools/update_icons.ps1."
}

$svg = Join-Path $repoRoot "resource\icons\app.svg"
$ico = Join-Path $repoRoot "resource\icons\windows\app.ico"
$hicolorRoot = Join-Path $repoRoot "resource\icons\hicolor"
$sizes = @(16, 24, 32, 48, 64, 128, 256)

& $magick -background none -density 384 $svg -define icon:auto-resize=256,128,64,48,32,24,16 $ico

foreach ($size in $sizes) {
    $dir = Join-Path $hicolorRoot "$($size)x$($size)\apps"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    & $magick -background none -density 384 $svg -resize "$($size)x$($size)" (Join-Path $dir "airan-desk.png")
}
