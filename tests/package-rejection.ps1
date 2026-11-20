param([Parameter(Mandatory=$true)][string]$SourceDirectory, [Parameter(Mandatory=$true)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
$repo = Join-Path $WorkDirectory ('package-reject-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $repo 'scripts') | Out-Null
$repo = (Resolve-Path -LiteralPath $repo).Path
Copy-Item -LiteralPath (Join-Path $SourceDirectory 'scripts/package.ps1') -Destination (Join-Path $repo 'scripts')
function Run-Git([string[]]$Arguments) {
    $value = & git -C $repo @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Fixture Git operation failed.' }
    return ($value -join "`n").Trim()
}
$null = Run-Git @('init','--quiet')
$null = Run-Git @('add','scripts/package.ps1')
$null = Run-Git @('-c','user.name=NSSM Test','-c','user.email=test@example.invalid','-c','commit.gpgsign=false','commit','--quiet','-m','Packaging fixture')
$commit = Run-Git @('rev-parse','HEAD')
$null = Run-Git @('tag','v2.24')
function Reject([string]$Revision, [string]$Reason) {
    $caught = $false
    try { & "$repo/scripts/package.ps1" -Commit $Revision -OutputDirectory "$repo/output" }
    catch { if ($_.Exception.Message -notlike "*$Reason*") { throw }; $caught = $true }
    if (-not $caught -or (Test-Path -LiteralPath "$repo/output")) { throw 'Expected rejection before output creation.' }
}
Reject ('0' * 40) 'current HEAD'
[IO.File]::WriteAllText("$repo/untracked", 'untracked')
Reject $commit 'clean tree'
Remove-Item -LiteralPath "$repo/untracked"
[IO.File]::AppendAllText("$repo/scripts/package.ps1", "`n# dirty fixture`n")
Reject $commit 'clean tree'
$null = Run-Git @('add','scripts/package.ps1')
Reject $commit 'clean tree'
# Do not leave nested Git repositories for editor repository discovery.
$gitDirectory = [IO.Path]::GetFullPath((Join-Path $repo '.git'))
$fixtureRoot = [IO.Path]::GetFullPath($repo) + [IO.Path]::DirectorySeparatorChar
if (-not $gitDirectory.StartsWith($fixtureRoot, [StringComparison]::OrdinalIgnoreCase) -or
    ((Get-Item -LiteralPath $gitDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected fixture Git path.' }
Remove-Item -LiteralPath $gitDirectory -Recurse -Force
$pkg = Join-Path $repo 'incomplete-package'
New-Item -ItemType Directory -Path (Join-Path $pkg 'evidence') | Out-Null
$report = 'x64-Release-tests.xml'
$identity = @{commit=$commit; clean=$true}
$record = @{archive='fixture.zip'; build=@{source=$identity; architecture='x64'; configuration='Release'; validation=@{tests=37; failures=0; report=$report}}}
@{schema=1; builds=@($record)*4; source_archive=@{name='fixture.zip'}; source=@{commit=$commit; clean=$true}} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $pkg 'build-info.json')
$sums = Join-Path $pkg 'SHA256SUMS.txt'
[IO.File]::WriteAllText($sums, '')
function Reject-Evidence($reason) {
    try { & (Join-Path $SourceDirectory 'tests/package.ps1') -PackageDirectory $pkg -WorkDirectory $WorkDirectory }
    catch { if ($_.Exception.Message -like "*$reason*") { return }; throw }
    throw "Expected evidence rejection: $reason"
}
$manifestPath = Join-Path $pkg 'build-info.json'
function Update-Checksums {
    $lines = foreach ($file in Get-ChildItem -LiteralPath $pkg -File -Recurse) {
        if ($file.FullName -ne $sums) {
            $relative = $file.FullName.Substring($pkg.Length + 1).Replace('\', '/')
            (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() + '  ' + $relative
        }
    }
    [IO.File]::WriteAllLines($sums, [string[]]$lines)
}
Reject-Evidence 'Missing checksum entry for build-info.json'
[IO.File]::WriteAllText((Join-Path $pkg 'fixture.zip'), 'fixture')
$record.build.source = @{commit=('0' * 40); clean=$true}
$fixtureManifest = @{schema=1; builds=@($record)*4; source_archive=@{name='fixture.zip'}; source=$identity}
$fixtureManifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath
Update-Checksums
Reject-Evidence 'Source identity mismatch'
$record.build.source = $identity
$fixtureManifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath
Update-Checksums
Reject-Evidence 'Missing checksum-covered test report'
$xml = Join-Path $pkg "evidence/$report"
[IO.File]::WriteAllText($xml, '<testsuite tests="1" failures="1"><testcase name="example" status="run"/></testsuite>')
Update-Checksums
Reject-Evidence 'Test results disagree'
$record.build.version_output = 'NSSM fixture'
$record.build.validation.tests = 1
$record.build.validation.scm_enabled = $true
$manifest = @{schema=2; builds=@($record)*4; source_archive=@{name='fixture.zip'}; source=@{commit=$commit; clean=$true}}
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath
[IO.File]::WriteAllText($xml, '<testsuite tests="1" failures="0"><testcase name="nssm.smoke" status="run"><system-out>NSSM fixture</system-out></testcase></testsuite>')
Update-Checksums
Reject-Evidence 'Missing upgrade/rollback evidence'
$record.build.validation.scm_enabled = $false
$record.build.artifacts = @(@{name='nssm.exe'})
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath
Update-Checksums
Reject-Evidence 'Package must declare an executable and its symbols'
$record.archive = 'subdir/fixture.zip'
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath
Update-Checksums
Reject-Evidence 'top-level ZIP'
$record.archive = 'absent.zip'
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $manifestPath
Update-Checksums
Reject-Evidence 'Missing checksum-covered archive'
Write-Output 'Revision, cleanliness, incomplete test evidence and missing artifact rejections passed.'
