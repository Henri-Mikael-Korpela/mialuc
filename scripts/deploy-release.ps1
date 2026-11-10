# Releases the compiler: reads the version from src/version.h, tags the current commit of main as
# v<version>, and pushes main and the tag to GitHub. Pushing the tag starts the release workflow in
# .github/workflows/release.yml, which checks the commit and publishes the release.
#
#   powershell -File scripts/deploy-release.ps1          ask before pushing
#   powershell -File scripts/deploy-release.ps1 -Yes     do not ask
#
# Set the version first with scripts/bump-version.ps1 and commit it.

param(
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Runs git and stops the script when it fails, because `$ErrorActionPreference` does not cover
# native programs.
function Invoke-Git {
    $output = & git @args
    if ($LASTEXITCODE -ne 0) {
        throw "git $($args -join ' ') failed."
    }
    return $output
}

function Get-VersionPart([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, "(?m)^#define MIALU_VERSION_$Name (\d+)\s*$")
    if ($match.Success -eq $false) {
        throw "src/version.h has no MIALU_VERSION_$Name."
    }
    return $match.Groups[1].Value
}

$header = Get-Content -Raw (Join-Path $root 'src/version.h')
$version = "$(Get-VersionPart $header 'MAJOR').$(Get-VersionPart $header 'MINOR').$(Get-VersionPart $header 'PATCH')"
$tag = "v$version"

# The workflow only releases a tag that is on main.
$branch = (Invoke-Git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    throw "The current branch is '$branch'. A release is made from main."
}

# What is not committed would not be in the release. Untracked files do not matter.
$changes = Invoke-Git status --porcelain --untracked-files=no
if ($changes) {
    throw "There are uncommitted changes. Commit them first:`n$($changes -join "`n")"
}

Invoke-Git fetch origin main --tags | Out-Null

$behind = [int](Invoke-Git rev-list --count 'HEAD..origin/main')
if ($behind -gt 0) {
    throw "main is $behind commit(s) behind origin/main. Pull first."
}

if (Invoke-Git tag --list $tag) {
    throw "The tag $tag already exists. Bump the version first with scripts/bump-version.ps1."
}

$ahead = [int](Invoke-Git rev-list --count 'origin/main..HEAD')
$commit = (Invoke-Git log -1 --format='%h %s').Trim()
Write-Host "Version: $version"
Write-Host "Commit:  $commit"
Write-Host "Pushes:  main ($ahead new commit(s)) and tag $tag to origin"

if ($Yes -eq $false) {
    $answer = Read-Host "Create the tag and push? [y/N]"
    if ($answer -notmatch '^[yY]') {
        Write-Host 'Cancelled.'
        exit 1
    }
}

# The commits go first, so that the tag is never on a commit that GitHub does not have on main.
Invoke-Git push origin main | Out-Null
Invoke-Git tag -a $tag -m "Release $version" | Out-Null
Invoke-Git push origin $tag | Out-Null

Write-Host "Pushed $tag. The release workflow is running: see the Actions tab of the repository."
