param([Parameter(Mandatory=$true)][string]$PackageDirectory, [Parameter(Mandatory=$true)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$checkedFiles = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $package 'SHA256SUMS.txt'))) {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Malformed checksum line.' }
    $expected = $Matches[1]; $relative = $Matches[2]
    $path = [IO.Path]::GetFullPath((Join-Path $package $relative))
    if ($checkedFiles.ContainsKey($path)) { throw "Duplicate checksum entry: $relative" }
    $checkedFiles[$path] = $true
    if (-not $path.StartsWith($package + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Checksum path escapes the package.' }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "Checksum mismatch: $relative" }
}
$checksumFile = Join-Path $package 'SHA256SUMS.txt'
foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File -Force) {
    if ($file.FullName -ne $checksumFile -and !$checkedFiles.ContainsKey($file.FullName)) {
        throw "Missing checksum entry for $($file.Name)"
    }
}
function Get-PackageArchive([string]$Name) {
    if (!$Name -or [IO.Path]::GetFileName($Name) -cne $Name -or
        $Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or [IO.Path]::GetExtension($Name) -ine '.zip') {
        throw 'Expected a top-level ZIP archive name.'
    }
    $path = Join-Path $package $Name
    if (!$checkedFiles.ContainsKey($path)) { throw "Missing checksum-covered archive: $Name" }
    return $path
}
$manifest = Get-Content -LiteralPath (Join-Path $package 'build-info.json') -Raw | ConvertFrom-Json
if ($manifest.builds.Count -ne 4 -or $manifest.source.commit -notmatch '^[0-9a-f]{40}$' -or -not $manifest.source.clean) { throw 'Invalid package manifest.' }
if ($manifest.schema -notin @(1, 2)) { throw 'Unsupported package manifest version.' }
$archivePath = Get-PackageArchive $manifest.source_archive.name
foreach ($record in $manifest.builds) { $null = Get-PackageArchive $record.archive }
$combinations = @()
$work = Join-Path $WorkDirectory ('package-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($record in $manifest.builds) {
    $build = $record.build
    if ($build.architecture -notin @('x64','x86') -or $build.configuration -notin @('Release','Debug')) { throw 'Invalid build configuration.' }
    $report = "$($build.architecture)-$($build.configuration)-tests.xml"
    $reportPath = [IO.Path]::GetFullPath((Join-Path $package "evidence/$report"))
    if ($build.validation.report -cne $report -or !$checkedFiles.ContainsKey($reportPath)) { throw "Missing checksum-covered test report: $report" }
    [xml]$junit = Get-Content -LiteralPath $reportPath -Raw
    $suite = $junit.testsuite
    $cases = @($suite.testcase)
    if (!$suite -or !$suite.HasAttribute('tests') -or !$suite.HasAttribute('failures') -or
        [int]$suite.tests -ne $build.validation.tests -or $cases.Count -ne $build.validation.tests -or
        [int]$suite.failures -ne 0 -or $build.validation.failures -ne 0 -or
        [int]$suite.errors -ne 0 -or [int]$suite.skipped -ne 0 -or [int]$suite.disabled -ne 0 -or
        @($cases | Where-Object { $_.status -cne 'run' -or $_.SelectSingleNode('failure|error|skipped') }).Count -or
        @($cases.name | Sort-Object -Unique).Count -ne $cases.Count) { throw "Test results disagree with manifest: $report" }
    $smoke = @($cases | Where-Object name -CEQ 'nssm.smoke')
    if ($smoke.Count -ne 1 -or !$build.version_output -or !([string]$smoke[0].'system-out').Contains($build.version_output)) { throw "Smoke test identity mismatch: $report" }
    if ($manifest.schema -eq 2 -and $build.validation.scm_enabled) {
        if ('nssm.upgrade_live' -cnotin $cases.name) { throw "Missing upgrade/rollback evidence in $report" }
        $legacyPath = Join-Path $package 'evidence/legacy-build-info.json'
        if (!$checkedFiles.ContainsKey($legacyPath)) { throw 'Missing checksum-covered legacy build identity.' }
        $legacy = Get-Content -LiteralPath $legacyPath -Raw | ConvertFrom-Json
        if ($legacy.source_commit -ne 'dee49fc1a0804f06687299b8e9612a3fce1c5e9a' -or
            $legacy.source_commit -ne $manifest.source.legacy_baseline -or $legacy.original_files_unchanged -ne $true -or
            $legacy.executable_sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Legacy comparison identity mismatch.' }
    }
    if ($build.validation.scm_enabled) {
        foreach ($required in 'nssm.export_live','nssm.dependencies_live','nssm.controls_live','nssm.lifecycle_live') {
            if ($required -cnotin $cases.name) { throw "Missing live test $required in $report" }
        }
    }
    $expectedFiles = @('nssm.exe', 'nssm.pdb')
    if ($build.artifacts.Count -ne 2 -or (Compare-Object $expectedFiles @($build.artifacts.name) -CaseSensitive)) {
        throw 'Package must declare an executable and its symbols.'
    }
    $archive = Get-PackageArchive $record.archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) { throw 'Archive hash mismatch.' }
    $name = $record.build.architecture + '-' + $record.build.configuration
    if ($combinations -contains $name) { throw 'Duplicate build configuration.' }
    $combinations += $name
    $destination = Join-Path $work $name
    Expand-Archive -LiteralPath $archive -DestinationPath $destination
    $inner = Get-Content -LiteralPath (Join-Path $destination 'build-info.json') -Raw | ConvertFrom-Json
    if ($inner.source.commit -ne $manifest.source.commit -or $inner.validation.failures -ne 0 -or $inner.validation.tests -lt 37) { throw 'Binary identity or validation mismatch.' }
    if ($build.schema -ne $manifest.schema -or $inner.schema -ne $manifest.schema -or
        $inner.architecture -ne $build.architecture -or $inner.configuration -ne $build.configuration -or
        $inner.version_output -cne $build.version_output -or $inner.validation.report -cne $report -or
        $inner.validation.tests -ne $cases.Count -or $inner.validation.scm_enabled -ne $build.validation.scm_enabled) { throw "Inner package details disagree with $report" }
    if ($inner.artifacts.Count -ne 2 -or (Compare-Object $expectedFiles @($inner.artifacts.name) -CaseSensitive)) {
        throw 'Inner package must declare an executable and its symbols.'
    }
    foreach ($artifact in $inner.artifacts) {
        $outer = $build.artifacts | Where-Object name -CEQ $artifact.name
        if ($outer.sha256 -cne $artifact.sha256) { throw 'Inner and outer artifact hashes disagree.' }
        if ((Get-FileHash -LiteralPath (Join-Path $destination $artifact.name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $artifact.sha256) { throw 'Executable/symbol hash mismatch.' }
    }
    $actual = (& (Join-Path $destination 'nssm.exe') version) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne $inner.version_output) { throw 'Packaged executable version mismatch.' }
}
if (Compare-Object @('x64-Release','x64-Debug','x86-Release','x86-Debug') $combinations) { throw 'Incomplete build matrix.' }
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
