# Sets the version of the compiler everywhere it is written down: the macros and the example in
# src/version.h, and the example output of `mialuc --version` in the language specification.
# The version is a semantic version of the form MAJOR.MINOR.PATCH, such as 0.2.0.
#
#   powershell -File scripts/bump-version.ps1 0.2.0

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Version
)

$ErrorActionPreference = 'Stop'

# No leading zeros, as in the semantic versioning specification. Pre-release and build metadata
# are not supported, because the version is also three numbers in the preprocessor.
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    Write-Error "'$Version' is not a semantic version of the form MAJOR.MINOR.PATCH, such as 0.2.0."
    exit 2
}
$major = $Matches[1]
$minor = $Matches[2]
$patch = $Matches[3]

$root = Split-Path -Parent $PSScriptRoot
$utf8 = New-Object System.Text.UTF8Encoding($false)

# Replaces `Pattern` in the file, which must match exactly once, and keeps the line endings.
function Update-File([string]$RelativePath, [string]$Pattern, [string]$Replacement) {
    $path = Join-Path $root $RelativePath
    $text = [System.IO.File]::ReadAllText($path, $utf8)
    $count = [regex]::Matches($text, $Pattern, 'Multiline').Count
    if ($count -ne 1) {
        throw "${RelativePath}: expected one match of '$Pattern', found $count."
    }
    $text = [regex]::Replace($text, $Pattern, $Replacement, 'Multiline')
    [System.IO.File]::WriteAllText($path, $text, $utf8)
}

Update-File 'src/version.h' '^#define MIALU_VERSION_MAJOR \d+' "#define MIALU_VERSION_MAJOR $major"
Update-File 'src/version.h' '^#define MIALU_VERSION_MINOR \d+' "#define MIALU_VERSION_MINOR $minor"
Update-File 'src/version.h' '^#define MIALU_VERSION_PATCH \d+' "#define MIALU_VERSION_PATCH $patch"
Update-File 'src/version.h' '^// "\d+\.\d+\.\d+"' "// `"$Version`""
Update-File 'docs/mialu-programming-language.md' '(?<=such as `mialuc )\d+\.\d+\.\d+(?=`)' $Version

Write-Host "Version is now $Version. Commit the change and push the tag v$Version to release it."
