param(
    [Parameter(Mandatory=$true)]
    [string]$QtDir,

    [string]$Generator = "Visual Studio 17 2022",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$DistDir = Join-Path $Root "dist\ChromeBookmarkExplorer"

cmake -S $Root -B $BuildDir -G $Generator -A x64 -DCMAKE_PREFIX_PATH=$QtDir
cmake --build $BuildDir --config $Config

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item "$BuildDir\$Config\ChromeBookmarkExplorer.exe" $DistDir -Force

$Deploy = Join-Path $QtDir "bin\windeployqt.exe"
& $Deploy --release --no-translations "$DistDir\ChromeBookmarkExplorer.exe"

Write-Host "Done: $DistDir"
