param([Parameter(Mandatory=$true)][string]$SourceDirectory, [Parameter(Mandatory=$true)][string]$WorkDirectory)
$ErrorActionPreference = 'Stop'
$repo = Join-Path $WorkDirectory ('package-reject-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $repo 'scripts') | Out-Null
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
$record = @{build=@{architecture='x64'; configuration='Release'; validation=@{tests=37; failures=0; report=$report}}}
@{builds=@($record)*4; source=@{commit=$commit; clean=$true}} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $pkg 'build-info.json')
$sums = Join-Path $pkg 'SHA256SUMS.txt'
[IO.File]::WriteAllText($sums, '')
function Reject-Evidence($reason) {
    try { & (Join-Path $SourceDirectory 'tests/package.ps1') -PackageDirectory $pkg -WorkDirectory $WorkDirectory }
    catch { if ($_.Exception.Message -like "*$reason*") { return }; throw }
    throw "Expected evidence rejection: $reason"
}
Reject-Evidence 'Missing checksum-covered test report'
$xml = Join-Path $pkg "evidence/$report"
[IO.File]::WriteAllText($xml, '<testsuite tests="1" failures="1"><testcase name="example" status="run"/></testsuite>')
[IO.File]::WriteAllText($sums, ((Get-FileHash -LiteralPath $xml).Hash.ToLowerInvariant() + "  evidence/$report"))
Reject-Evidence 'Test results disagree'
Write-Output 'Revision, cleanliness and incomplete release evidence rejections passed.'
