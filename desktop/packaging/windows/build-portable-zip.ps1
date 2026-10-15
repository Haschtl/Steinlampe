# Builds Quarzlampe-portable-windows.zip: both exes, zipped together - no
# installer, no registry entries, just extract and run quarzlampe-tray.exe.
# `quarzlampe-tray.exe --install` still works from inside the extracted
# folder for anyone who wants Start Menu/autostart integration without the
# MSI. This is also what `core::self_update` downloads for a Windows
# self-update (see that module's doc comment for why it's a replace-on-
# restart dance rather than a direct overwrite).
#
# Only uses Compress-Archive (built into PowerShell 5+, on every
# windows-latest runner) - no third-party zip tool.
# Unverified: no Windows machine here to run this.
#
# Usage: build-portable-zip.ps1 -ReleaseDir <path-to-target\release> -OutDir <output-dir>

param(
    [Parameter(Mandatory = $true)][string]$ReleaseDir,
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$staging = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $staging | Out-Null
try {
    Copy-Item (Join-Path $ReleaseDir "quarzlampe-tray.exe") $staging
    Copy-Item (Join-Path $ReleaseDir "quarzlampe-console.exe") $staging

    $zipPath = Join-Path $OutDir "Quarzlampe-portable-windows.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath }
    Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zipPath

    Write-Host "Built $zipPath"
}
finally {
    Remove-Item -Recurse -Force $staging
}
