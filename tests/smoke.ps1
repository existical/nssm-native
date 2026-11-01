param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'x86')][string]$Architecture,
    [Parameter(Mandatory = $true)][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$VersionHeader
)

$ErrorActionPreference = 'Stop'
$binaryPath = (Resolve-Path -LiteralPath $Executable).Path
$header = [IO.File]::ReadAllText($VersionHeader)
$versionMatch = [regex]::Match($header, '#define NSSM_VERSION _T\("([^"]+)"\)')
$dateMatch = [regex]::Match($header, '#define NSSM_DATE _T\("([^"]+)"\)')
if (-not $versionMatch.Success -or -not $dateMatch.Success) { throw 'Invalid generated version header.' }
$bitness = if ($Architecture -eq 'x64') { '64-bit' } else { '32-bit' }
$debugSuffix = if ($Configuration -eq 'Debug') { ' debug' } else { '' }
$expected = "NSSM $($versionMatch.Groups[1].Value) $bitness$debugSuffix $($dateMatch.Groups[1].Value)"
$output = & $binaryPath version
if ($LASTEXITCODE -ne 0) { throw 'nssm.exe version did not exit successfully.' }
if (($output -join "`n").Trim() -ne $expected) { throw "Unexpected version output: $output; expected: $expected" }

$reader = New-Object IO.BinaryReader([IO.File]::OpenRead($binaryPath))
try {
    $reader.BaseStream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    $reader.BaseStream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550) { throw 'Invalid Windows PE signature.' }
    $machine = $reader.ReadUInt16()
    $expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0x014c }
    if ($machine -ne $expectedMachine) { throw 'The executable machine type does not match its build architecture.' }
}
finally {
    $reader.Dispose()
}
$metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($binaryPath)
if ($metadata.FileVersion -ne $versionMatch.Groups[1].Value) { throw 'The embedded version resource differs from the executable version.' }
if ($metadata.ProductName -ne "NSSM $bitness$debugSuffix") { throw 'The embedded product resource has the wrong architecture or configuration.' }
Write-Output "Passed version, PE architecture and embedded metadata checks: $expected"
