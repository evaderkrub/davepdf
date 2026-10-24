# Builds the portable release zip: a clean cmake --install into a staging
# folder (so nothing from a developer's working folder, such as settings.txt,
# leaks in), zipped as davepdf-<version>-windows-x64.zip under C:\buildfiles.
# Run after build.ps1. Pass -Smoke to extract the zip elsewhere and run the
# executable from it once, proving the archive is self-contained.
param([string]$BuildDir = 'C:/buildfiles/davepdf/release', [string]$OutDir = 'C:/buildfiles/davepdf', [switch]$Smoke)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(davepdf VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { throw 'Could not read the version from CMakeLists.txt' }
$stage = Join-Path $OutDir 'package'
$folder = Join-Path $stage 'davepdf'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
cmake --install $BuildDir --prefix $folder | Out-Null
if ($LASTEXITCODE) { throw 'cmake --install failed; build first with build.ps1' }
foreach ($required in 'davepdf.exe', 'pdfium.dll', 'README.md', 'LICENSE', 'assets/fonts/OpenSans-Regular.ttf', 'assets/licenses/PDFium.txt') {
    if (-not (Test-Path (Join-Path $folder $required))) { throw "Staged folder is missing $required" }
}
$zip = Join-Path $OutDir "davepdf-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $folder -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $zip -Leaf)" | Set-Content -Encoding ascii "$zip.sha256"
if ($Smoke) {
    $probe = Join-Path $env:TEMP "davepdf-zip-smoke-$([guid]::NewGuid().ToString('n'))"
    Expand-Archive -Path $zip -DestinationPath $probe -Force
    Push-Location $env:TEMP
    try {
        & (Join-Path $probe 'davepdf/davepdf.exe') --smoke --sample
        if ($LASTEXITCODE) { throw "Extracted copy exited with code $LASTEXITCODE" }
    } finally { Pop-Location; Remove-Item $probe -Recurse -Force -ErrorAction SilentlyContinue }
    Write-Output 'Smoke test of the extracted archive passed.'
}
Write-Output "Created $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB)"
Write-Output "SHA-256 $hash"
