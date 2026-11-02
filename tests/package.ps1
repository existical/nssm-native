param([Parameter(Mandatory=$true)][string]$PackageDirectory, [Parameter(Mandatory=$true)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
foreach ($line in [IO.File]::ReadAllLines((Join-Path $package 'SHA256SUMS.txt'))) {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Malformed checksum line.' }
    $expected = $Matches[1]; $relative = $Matches[2]
    $path = [IO.Path]::GetFullPath((Join-Path $package $relative))
    if (-not $path.StartsWith($package + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Checksum path escapes the package.' }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "Checksum mismatch: $relative" }
}
$manifest = Get-Content -LiteralPath (Join-Path $package 'build-info.json') -Raw | ConvertFrom-Json
if ($manifest.builds.Count -ne 4 -or $manifest.source.commit -notmatch '^[0-9a-f]{40}$' -or -not $manifest.source.clean) { throw 'Invalid package manifest.' }
$combinations = @()
$work = Join-Path $WorkDirectory ('package-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($record in $manifest.builds) {
    $archive = Join-Path $package $record.archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) { throw 'Archive hash mismatch.' }
    $name = $record.build.architecture + '-' + $record.build.configuration
    if ($combinations -contains $name) { throw 'Duplicate build configuration.' }
    $combinations += $name
    $destination = Join-Path $work $name
    Expand-Archive -LiteralPath $archive -DestinationPath $destination
    $inner = Get-Content -LiteralPath (Join-Path $destination 'build-info.json') -Raw | ConvertFrom-Json
    if ($inner.source.commit -ne $manifest.source.commit -or $inner.validation.failures -ne 0 -or $inner.validation.tests -lt 37) { throw 'Binary identity or validation mismatch.' }
    foreach ($artifact in $inner.artifacts) {
        if ((Get-FileHash -LiteralPath (Join-Path $destination $artifact.name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $artifact.sha256) { throw 'Executable/symbol hash mismatch.' }
    }
    $actual = (& (Join-Path $destination 'nssm.exe') version) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne $inner.version_output) { throw 'Packaged executable version mismatch.' }
}
if (Compare-Object @('x64-Release','x64-Debug','x86-Release','x86-Debug') $combinations) { throw 'Incomplete build matrix.' }
$archivePath = Join-Path $package $manifest.source_archive.name
if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.source_archive.sha256) { throw 'Source ZIP hash mismatch.' }
$source = Join-Path $work 'source'
Expand-Archive -LiteralPath $archivePath -DestinationPath $source
$identity = Get-Content -LiteralPath (Join-Path $source 'source-info.json') -Raw | ConvertFrom-Json
if ($identity.commit -ne $manifest.source.commit -or $identity.description -ne $manifest.source.description -or (Test-Path -LiteralPath "$source/.git")) { throw 'Source archive revision mismatch.' }
& cmake "-DNSSM_SOURCE_DIR=$source" "-DNSSM_VERSION_HEADER=$source/version.h" -P "$source/cmake/Version.cmake"
if ($LASTEXITCODE -ne 0) { throw 'Archive version generation failed.' }
$header = Get-Content -LiteralPath "$source/version.h" -Raw
$version = [regex]::Match($header, '#define NSSM_VERSION _T\("([^"]+)"\)').Groups[1].Value
foreach ($record in $manifest.builds) { if ($record.build.file_version -ne $version) { throw 'Source/binary version mismatch.' } }
Write-Output 'All checksums, four executable/symbol packages, test evidence and source-archive identity verified.'
