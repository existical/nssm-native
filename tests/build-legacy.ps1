param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$commit = 'dee49fc1a0804f06687299b8e9612a3fce1c5e9a'
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Legacy fixture output must not exist.' }
$description = & git -C $root describe --tags --long --abbrev=7 $commit
if ($LASTEXITCODE -ne 0 -or $description -ne 'v2.24-103-gdee49fc') { throw 'Expected preserved legacy history and tags.' }
$epoch = & git -C $root show -s --format=%ct $commit
if ($LASTEXITCODE -ne 0 -or $epoch -notmatch '^\d+$') { throw 'Cannot read the legacy commit date.' }
$date = [DateTimeOffset]::FromUnixTimeSeconds([long]$epoch).UtcDateTime.ToString('yyyy-MM-dd')
New-Item -ItemType Directory -Path $output | Out-Null
$archive = Join-Path $output 'source.zip'
& git -C $root archive --format=zip "--output=$archive" $commit
if ($LASTEXITCODE -ne 0) { throw 'Legacy archive failed.' }
$source = Join-Path $output 'source'
Expand-Archive -LiteralPath $archive -DestinationPath $source
# Hash every extracted original file before/after building: the adapter must
# never patch historical application source or checked-in resources.
$before = @{}
Get-ChildItem -LiteralPath $source -Recurse -File -Force | ForEach-Object { $before[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
function Checked([string]$Program, [string[]]$Arguments, [string]$Log) {
    $previous = $ErrorActionPreference
    try { $ErrorActionPreference = 'Continue'; & $Program @Arguments *> $Log; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { Get-Content -LiteralPath $Log -Tail 60; throw "Legacy fixture command failed: $Program" }
}
$build = Join-Path $output 'build'
Checked cmake @('-S', "$PSScriptRoot/legacy", '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DNSSM_LEGACY_SOURCE_DIR=$source", "-DNSSM_LEGACY_DATE=$date") "$output/configure.log"
Checked cmake @('--build', $build, '--config', 'Release', '--parallel') "$output/build.log"
$after = @(Get-ChildItem -LiteralPath $source -Recurse -File -Force)
if ($after.Count -ne $before.Count) { throw 'Legacy source file set changed during build.' }
foreach ($file in $after) {
    if ($before[$file.FullName] -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) { throw 'Legacy source changed during build.' }
}
$exe = Join-Path $build 'bin/Release/nssm.exe'
& "$PSScriptRoot/smoke.ps1" -Executable $exe -Architecture x64 -Configuration Release -VersionHeader "$build/generated/version.h"
$metadata = [ordered]@{
    schema = 1; source_commit = $commit; description = $description; original_files_unchanged = $true
    provenance = 'Test-only rebuild of preserved legacy source with the current MSVC/SDK and a separate CMake adapter; not an original distributed executable.'
    source_archive_sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    version_output = ((& $exe version) -join "`n").Trim()
    toolchain = Get-Content "$build/toolchain.json" -Raw | ConvertFrom-Json
}
[IO.File]::WriteAllText("$output/build-info.json", ($metadata | ConvertTo-Json -Depth 5))
Write-Output "Legacy comparison executable: $exe"
