param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference = 'Stop'
if ([Console]::In.ReadLine() -ne 'upgrade stdin' -or $env:NSSM_UPGRADE_VALUE -ne 'persistent value') { exit 11 }
if ([Environment]::CurrentDirectory.TrimEnd('\') -ne $Directory.TrimEnd('\')) { exit 12 }
$statePath = Join-Path $Directory 'state.txt'
$generation = 1
if (Test-Path -LiteralPath $statePath) { $generation += [int]([IO.File]::ReadAllText($statePath)) }
Remove-Item -LiteralPath (Join-Path $Directory 'stop') -ErrorAction SilentlyContinue
[IO.File]::WriteAllText($statePath, [string]$generation)
[Console]::Out.WriteLine("worker-ready-$generation")
[IO.File]::WriteAllText((Join-Path $Directory 'ready.txt'), "$PID $generation")
$watch = [Diagnostics.Stopwatch]::StartNew()
while ($watch.Elapsed.TotalSeconds -lt 60) {
    if (Test-Path -LiteralPath (Join-Path $Directory 'crash')) {
        Remove-Item -LiteralPath (Join-Path $Directory 'crash')
        exit 17
    }
    if ((Test-Path -LiteralPath (Join-Path $Directory 'stop')) -or (Test-Path -LiteralPath (Join-Path $Directory 'release'))) { break }
    [IO.File]::WriteAllText((Join-Path $Directory 'heartbeat.txt'), "$generation $($watch.ElapsedMilliseconds)")
    Start-Sleep -Milliseconds 100
}
[Console]::Out.WriteLine("worker-final-$generation")
[Console]::Error.WriteLine("worker-stderr-$generation")
