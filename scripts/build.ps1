param(
    [ValidateSet('x64', 'x86')][string]$Architecture = 'x64',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$configurePreset = "windows-$Architecture"
$buildPreset = "$configurePreset-$($Configuration.ToLowerInvariant())"
Push-Location -LiteralPath $projectRoot
try {
    & cmake --preset $configurePreset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build --preset $buildPreset --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    & ctest --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { throw 'Smoke checks failed.' }
    Write-Output "Executable: $projectRoot\build\$configurePreset\bin\$Configuration\nssm.exe"
}
finally {
    Pop-Location
}
