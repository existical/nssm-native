param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-f]{40}$')][string]$Commit,
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+(-(beta|rc)\.[0-9]+)?$')][string]$Tag,
    [string]$OutputDirectory,
    [switch]$EnableScmTests
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
function Git-Text([string[]]$Arguments) {
    $value = & git -C $projectRoot @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Git failed: $Arguments" }
    return ($value -join "`n").Trim()
}
function Write-JsonFile($Value, [string]$Path) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 12) + "`n", (New-Object Text.UTF8Encoding($false)))
}
function Invoke-Checked([string]$Program, [string[]]$Arguments, [string]$Log) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments *> $Log
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($code -ne 0) { Get-Content -LiteralPath $Log -Tail 35; throw "$Program failed; see $Log" }
}

if ((Git-Text @('rev-parse', 'HEAD')) -ne $Commit) { throw 'Commit must be the full current HEAD hash.' }
if (Git-Text @('status', '--porcelain', '--untracked-files=all')) { throw 'Packaging requires a clean tree, including untracked files.' }
if ((Git-Text @('rev-parse', '--is-shallow-repository')) -ne 'false') { throw 'Packaging requires complete history and version tags.' }
$description = Git-Text @('describe', '--tags', '--long', '--match', 'v[0-9]*')
if ($description -notmatch '^v[0-9]+\.[0-9]+(\.[0-9]+)?(-(beta|rc)\.[0-9]+)?-[0-9]+-g[0-9a-f]+$') { throw 'Unsupported version tag.' }
if ($Tag) {
    if ((Git-Text @('rev-parse', ($Tag + '^{commit}'))) -ne $Commit) { throw 'Release tag must resolve to the requested commit.' }
    if ($description -notmatch ('^' + [regex]::Escape($Tag) + '-0-g')) { throw 'Version generation must select the exact release tag.' }
    if ($Tag -eq 'v2.24') { throw 'Do not reuse an upstream release tag.' }
}
$label = if ($Tag) { $Tag } else { 'candidate-' + $Commit.Substring(0,12) }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot "dist/$label" }
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Output directory already exists; choose a new directory.' }
$epoch = Git-Text @('show', '-s', '--format=%ct', $Commit)
$identity = [ordered]@{schema=1; repository='https://github.com/existical/nssm-native'; commit=$Commit; description=$description; tag=$Tag; timestamp=$epoch; clean=$true; legacy_baseline='dee49fc1a0804f06687299b8e9612a3fce1c5e9a'}
$work = Join-Path $projectRoot ('build/package-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $work 'source'
$evidence = Join-Path $work 'evidence'
New-Item -ItemType Directory -Path $work, $evidence | Out-Null
$sourceZip = Join-Path $work "nssm-native-$label-source.zip"
Invoke-Checked git @('-C', $projectRoot, 'archive', '--format=zip', ('--output=' + $sourceZip), $Commit) (Join-Path $evidence 'archive.log')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($sourceZip, [IO.Compression.ZipArchiveMode]::Update)
try {
    if ($zip.GetEntry('source-info.json')) { throw 'Tracked source-info.json would conflict with generated identity.' }
    $entry = $zip.CreateEntry('source-info.json')
    $writer = New-Object IO.StreamWriter($entry.Open(), (New-Object Text.UTF8Encoding($false)))
    try { $writer.Write(($identity | ConvertTo-Json -Depth 5) + "`n") } finally { $writer.Dispose() }
} finally { $zip.Dispose() }
Expand-Archive -LiteralPath $sourceZip -DestinationPath $source
$previousEpoch = $env:SOURCE_DATE_EPOCH
$env:SOURCE_DATE_EPOCH = $epoch
$records = @()
try {
    foreach ($architecture in @('x64', 'x86')) {
        $preset = "windows-$architecture"
        $build = Join-Path $source "build/$preset"
        # Only the Release build of the opted-in x64 tree runs privileged fixtures.
        Invoke-Checked cmake @('--preset', $preset, '-DNSSM_ENABLE_SCM_TESTS=OFF', '-S', $source) (Join-Path $evidence "$architecture-configure.log")
        foreach ($configuration in @('Release', 'Debug')) {
            $name = "$architecture-$configuration"
            $scm = $EnableScmTests -and $architecture -eq 'x64' -and $configuration -eq 'Release'
            Invoke-Checked cmake @('-S', $source, '-B', $build, ('-DNSSM_ENABLE_SCM_TESTS=' + $(if ($scm) {'ON'} else {'OFF'}))) (Join-Path $evidence "$name-configure.log")
            Invoke-Checked cmake @('--build', $build, '--config', $configuration, '--parallel') (Join-Path $evidence "$name-build.log")
            $report = Join-Path $evidence "$name-tests.xml"
            Invoke-Checked ctest @('--test-dir', $build, '-C', $configuration, '--output-on-failure', '--no-tests=error', '--output-junit', $report) (Join-Path $evidence "$name-tests.log")
            [xml]$testResults = Get-Content -LiteralPath $report -Raw
            if ([int]$testResults.testsuite.tests -le 0 -or [int]$testResults.testsuite.failures -ne 0 -or [int]$testResults.testsuite.disabled -ne 0) { throw 'Incomplete test evidence.' }
            $binary = Join-Path $build "bin/$configuration/nssm.exe"
            $symbols = Join-Path $build "bin/$configuration/nssm.pdb"
            $version = (& $binary version) -join "`n"
            if ($LASTEXITCODE -ne 0) { throw 'Version command failed.' }
            $fileInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($binary)
            $toolchain = Get-Content -LiteralPath (Join-Path $build 'toolchain.json') -Raw | ConvertFrom-Json
            if (-not $toolchain.compiler -or -not $toolchain.windows_sdk) { throw 'Missing compiler/SDK identity.' }
            $files = @($binary, $symbols) | ForEach-Object { [ordered]@{name=[IO.Path]::GetFileName($_); sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()} }
            $manifest = [ordered]@{
                schema=1; product='NSSM Native'; source=$identity; version_output=$version.Trim(); file_version=$fileInfo.FileVersion; product_version=$fileInfo.ProductVersion
                architecture=$architecture; configuration=$configuration; crt=$(if ($configuration -eq 'Debug') {'/MTd'} else {'/MT'}); compiler=$toolchain.compiler; windows_sdk=$toolchain.windows_sdk; generator=$toolchain.generator
                cmake=((& cmake --version | Select-Object -First 1) -join ''); build_os=[Environment]::OSVersion.VersionString
                built_at_utc=[DateTime]::UtcNow.ToString('o'); source_date_epoch=$epoch
                workflow=[ordered]@{repository=$env:GITHUB_REPOSITORY; name=$env:GITHUB_WORKFLOW; run_id=$env:GITHUB_RUN_ID; attempt=$env:GITHUB_RUN_ATTEMPT; sha=$env:GITHUB_SHA}
                validation=[ordered]@{tests=[int]$testResults.testsuite.tests; failures=0; scm_enabled=[bool]$scm; report="$name-tests.xml"}
                fixes=@(1..35 | ForEach-Object {'NSSM-{0:D3}' -f $_}); continuation_changes=@(1..5 | ForEach-Object {'CONT-{0:D3}' -f $_})
                release_status='Candidate only; publication and first-release assignments are separate'; limitations='See docs/releases/README.md and docs/releases/candidate.md'; artifacts=$files
            }
            $stage = Join-Path $work $name
            New-Item -ItemType Directory -Path $stage | Out-Null
            Copy-Item -LiteralPath $binary, $symbols, (Join-Path $source 'LICENSE'), (Join-Path $source 'README.md') -Destination $stage
            Copy-Item -LiteralPath (Join-Path $source 'docs') -Destination $stage -Recurse
            Write-JsonFile $manifest (Join-Path $stage 'build-info.json')
            $archive = Join-Path $work "nssm-native-$label-$name.zip"
            Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
            $records += [ordered]@{archive=[IO.Path]::GetFileName($archive); sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant(); build=$manifest}
            Write-Output "Packaged $name; $($manifest.validation.tests) tests passed."
        }
    }
    # Refuse a changed checkout even though the actual builds came from git archive.
    if ((Git-Text @('rev-parse','HEAD')) -ne $Commit -or (Git-Text @('status','--porcelain','--untracked-files=all'))) { throw 'Checkout changed during packaging.' }
    New-Item -ItemType Directory -Path $destination | Out-Null
    Copy-Item -LiteralPath $sourceZip -Destination $destination
    foreach ($record in $records) { Copy-Item -LiteralPath (Join-Path $work $record.archive) -Destination $destination }
    Copy-Item -LiteralPath $evidence -Destination $destination -Recurse
    Write-JsonFile ([ordered]@{schema=1; source=$identity; builds=$records; source_archive=[ordered]@{name=[IO.Path]::GetFileName($sourceZip); sha256=(Get-FileHash -LiteralPath $sourceZip -Algorithm SHA256).Hash.ToLowerInvariant()}}) (Join-Path $destination 'build-info.json')
    $hashes = Get-ChildItem -LiteralPath $destination -File -Recurse | Sort-Object FullName | ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.FullName.Substring($destination.Length + 1).Replace('\','/')
    }
    [IO.File]::WriteAllLines((Join-Path $destination 'SHA256SUMS.txt'), $hashes, (New-Object Text.UTF8Encoding($false)))
    Write-Output "Candidate packages: $destination"
} finally { $env:SOURCE_DATE_EPOCH = $previousEpoch }
