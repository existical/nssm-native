param([Parameter(Mandatory=$true)][string]$SourceDirectory, [Parameter(Mandatory=$true)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $WorkDirectory ('version-' + [guid]::NewGuid().ToString('N'))
$repo = Join-Path $fixture 'repo'
$archive = Join-Path $fixture 'archive'
New-Item -ItemType Directory -Path $repo, $archive | Out-Null
function Git-Test([string[]]$Arguments) {
    $result = & git -C $repo @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git command failed: $Arguments" }
    return ($result -join "`n").Trim()
}
function Generate([string]$Directory, [bool]$Fails = $false) {
    # Windows PowerShell 5.1 turns redirected native stderr into ErrorRecords.
    # Inspect the exit code, including deliberately invalid identity cases.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & cmake "-DNSSM_SOURCE_DIR=$Directory" "-DNSSM_VERSION_HEADER=$Directory/version.h" -P "$SourceDirectory/cmake/Version.cmake" *> "$fixture/generate.log"
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if (($code -ne 0) -ne $Fails) { Get-Content "$fixture/generate.log"; throw 'Unexpected version generator result.' }
    if (-not $Fails) { return [IO.File]::ReadAllText("$Directory/version.h") }
}
[IO.File]::WriteAllText("$repo/VERSION", "2.24`n")
$null = Git-Test @('init','--quiet')
$null = Git-Test @('add','VERSION')
$null = Git-Test @('-c','user.name=NSSM Test','-c','user.email=test@example.invalid','-c','commit.gpgsign=false','commit','--quiet','-m','Fixture')
$commit = Git-Test @('rev-parse','HEAD')
$null = Git-Test @('tag','v2.24')
$legacy = Generate $repo
if (-not $legacy.Contains('NSSM_VERSION _T("2.24")') -or -not $legacy.Contains('NSSM_VERSIONINFO 2,24,0,0')) { throw 'Legacy tag version changed.' }
$null = Git-Test @('tag','-d','v2.24')
$null = Git-Test @('tag','v2.25.0-beta.1')
$beta = Generate $repo
if (-not $beta.Contains('NSSM_VERSION _T("2.25.0-beta.1")') -or -not $beta.Contains('NSSM_FILEFLAGS VS_FF_PRERELEASE')) { throw 'Prerelease tag metadata mismatch.' }
$null = Git-Test @('tag','-d','v2.25.0-beta.1')
$null = Git-Test @('tag','v2.25.0-rc.1')
if (-not (Generate $repo).Contains('NSSM_VERSION _T("2.25.0-rc.1")')) { throw 'RC tag mismatch.' }
$null = Git-Test @('tag','-d','v2.25.0-rc.1')
$null = Git-Test @('tag','v2.25.0')
if (-not (Generate $repo).Contains('NSSM_FILEFLAGS 0L')) { throw 'Stable tag flags mismatch.' }
[IO.File]::WriteAllText("$repo/next", "next")
$null = Git-Test @('add','next')
$null = Git-Test @('-c','user.name=NSSM Test','-c','user.email=test@example.invalid','-c','commit.gpgsign=false','commit','--quiet','-m','Next fixture commit')
$commit = Git-Test @('rev-parse','HEAD')
$description = Git-Test @('describe','--tags','--long')
$oldEpoch = $env:SOURCE_DATE_EPOCH
try {
    $env:SOURCE_DATE_EPOCH = '1700000000'
    $expected = Generate $repo
    if (-not $expected.Contains('NSSM_VERSIONINFO 2,25,0,1')) { throw 'Post-tag revision mismatch.' }
    Copy-Item -LiteralPath "$repo/VERSION" -Destination $archive
    $identity = @{schema=1; commit=$commit; description=$description; timestamp='1700000000'}
    [IO.File]::WriteAllText("$archive/source-info.json", ($identity | ConvertTo-Json))
    $env:SOURCE_DATE_EPOCH = $null
    if ((Generate $archive) -ne $expected) { throw 'Source archive identity differs from Git checkout.' }
    $identity.commit = 'invalid'
    [IO.File]::WriteAllText("$archive/source-info.json", ($identity | ConvertTo-Json))
    Generate $archive $true
    $identity.commit = $commit; $identity.description = 'unsupported'
    [IO.File]::WriteAllText("$archive/source-info.json", ($identity | ConvertTo-Json))
    Generate $archive $true
} finally { $env:SOURCE_DATE_EPOCH = $oldEpoch }
# Do not leave nested Git repositories for editor repository discovery.
$gitDirectory = [IO.Path]::GetFullPath((Join-Path $repo '.git'))
$fixtureRoot = [IO.Path]::GetFullPath($repo) + [IO.Path]::DirectorySeparatorChar
if (-not $gitDirectory.StartsWith($fixtureRoot, [StringComparison]::OrdinalIgnoreCase) -or
    ((Get-Item -LiteralPath $gitDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected fixture Git path.' }
Remove-Item -LiteralPath $gitDirectory -Recurse -Force
Write-Output 'Legacy/stable/beta/RC/post-tag versions, archive equivalence and malformed identity rejection passed.'
