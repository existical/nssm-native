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
Write-Output 'Wrong revision, untracked, unstaged and staged changes rejected before package output.'
