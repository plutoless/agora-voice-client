$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "build\Release\agora-voice-client.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $root "build\agora-voice-client.exe" }
$out  = Join-Path $root "dist\agora-voice-client-windows-x64"
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $exe $out
Copy-Item (Join-Path $root "third_party\agora\bin\*.dll") $out
$zip = Join-Path $root "dist\agora-voice-client-windows-x64.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $out -DestinationPath $zip
Write-Host "Wrote $zip"
