param([ValidateSet('release','debug')][string]$Configuration = 'release', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ build tools are required.' }
$dev = Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1'
& $dev -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Push-Location $PSScriptRoot
try {
    cmake --preset $Configuration
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    cmake --build --preset $Configuration --parallel
    if ($LASTEXITCODE) { throw 'Build failed.' }
    if (-not $SkipTests) {
        ctest --preset $Configuration
        if ($LASTEXITCODE) { throw 'Tests failed.' }
    }
} finally { Pop-Location }
