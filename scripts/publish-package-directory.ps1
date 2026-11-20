param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true)][scriptblock]$WriteContents
)

$ErrorActionPreference = 'Stop'
$target = [IO.DirectoryInfo]::new([IO.Path]::GetFullPath($Destination))
$parent = $target.Parent
if (!$parent) { throw 'The package directory must have a parent.' }
[void][IO.Directory]::CreateDirectory($parent.FullName)
$stageName = '.nssm-package-' + [guid]::NewGuid().ToString('N')
$stage = (New-Item -ItemType Directory -Path (Join-Path $parent.FullName $stageName)).FullName
try {
    & $WriteContents $stage
    [IO.Directory]::Move($stage, $target.FullName)
} finally {
    if (Test-Path -LiteralPath $stage) {
        $owned = Get-Item -LiteralPath $stage -Force
        if ($owned.Parent.FullName -ne $parent.FullName -or $owned.Name -ne $stageName) {
            throw "Unexpected package staging path: $stage"
        }
        Remove-Item -LiteralPath $owned.FullName -Recurse -Force
    }
}
