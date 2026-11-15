# Builds the Windows release package: satreplay.exe with Qt and ffmpeg beside it, zipped.
#
#   pwsh packaging/windows/package.ps1 -Ffmpeg C:\path\to\ffmpeg.exe [-Build build/dist]
#
# Run after `cmake --preset dist` and `cmake --build --preset dist` in a Visual Studio developer
# shell, with the Qt to deploy on the PATH (windeployqt). Writes
# dist/SatReplay-<version>-Windows-x64.zip.
param(
    [Parameter(Mandatory = $true)][string]$Ffmpeg,
    [string]$Build = "build/dist"
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "../..")

$version = (Select-String -Path CMakeLists.txt -Pattern '^\s*VERSION ([0-9.]+)$').Matches[0].Groups[1].Value
$name = "SatReplay-$version-Windows-x64"
$stage = Join-Path "dist" $name
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

# Qt puts Windows executables at the top of the build folder (next to their DLLs), not in app/.
$exe = Get-ChildItem $Build -Recurse -Filter satreplay.exe | Select-Object -First 1
if (-not $exe) { throw "satreplay.exe not found under $Build; build the dist preset first" }
Copy-Item $exe.FullName $stage
# ffmpeg goes next to the executable, where the app looks for it first.
Copy-Item $Ffmpeg (Join-Path $stage "ffmpeg.exe")
# Qt libraries and plugins, including the multimedia backend.
windeployqt --release --no-translations --no-system-d3d-compiler (Join-Path $stage "satreplay.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
Copy-Item README.md, LICENSE $stage

$zip = "dist/$name.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $stage -DestinationPath $zip
Write-Output $zip
