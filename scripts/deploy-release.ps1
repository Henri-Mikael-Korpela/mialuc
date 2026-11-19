# Releases the compiler: reads the version from the file VERSION, tags the current commit of main as
# v<version>, and pushes main and the tag to GitHub. Pushing the tag starts the release workflow in
# .github/workflows/release.yml, which checks the commit and publishes the release.
#
#   powershell -File scripts/deploy-release.ps1          ask before pushing
#   powershell -File scripts/deploy-release.ps1 -Yes     do not ask
#
# When the tag exists already, the script asks whether to replace it, which moves the tag to the
# current commit, also on GitHub. `-Yes` does not answer that question, because it cannot be undone.
#
# Set the version first with scripts/bump-version.mu and commit it.

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

# The version is the one line of the file VERSION, a semantic version of the form MAJOR.MINOR.PATCH.
$version = (Get-Content -Raw (Join-Path $root 'VERSION')).Trim()
if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw "VERSION says '$version', which is not of the form MAJOR.MINOR.PATCH, such as 0.2.0."
}
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

# The fetch above brought the tags of GitHub, so a tag that is only there is found too.
$isReplacing = $false
if (Invoke-Git tag --list $tag) {
    $tagCommit = (Invoke-Git log -1 --format='%h %s' "$tag^{commit}").Trim()
    Write-Host "The tag $tag already exists, on: $tagCommit"
    $answer = Read-Host "Replace the tag with a new release of the current commit? [y/n]"
    if ($answer -notmatch '^[yY]') {
        Write-Host 'Cancelled. To release a new version, bump it first with scripts/bump-version.mu.'
        exit 1
    }
    $isReplacing = $true
}

$ahead = [int](Invoke-Git rev-list --count 'origin/main..HEAD')
$commit = (Invoke-Git log -1 --format='%h %s').Trim()
Write-Host "Version: $version"
Write-Host "Commit:  $commit"
Write-Host "Pushes:  main ($ahead new commit(s)) and tag $tag to origin"

# Replacing the tag was confirmed above, so it is not asked a second time.
if ($Yes -eq $false -and $isReplacing -eq $false) {
    $answer = Read-Host "Create the tag and push? [y/N]"
    if ($answer -notmatch '^[yY]') {
        Write-Host 'Cancelled.'
        exit 1
    }
}

# The commits go first, so that the tag is never on a commit that GitHub does not have on main.
Invoke-Git push origin main | Out-Null
if ($isReplacing) {
    Invoke-Git tag -f -a $tag -m "Release $version" | Out-Null
    Invoke-Git push --force origin $tag | Out-Null
} else {
    Invoke-Git tag -a $tag -m "Release $version" | Out-Null
    Invoke-Git push origin $tag | Out-Null
}

Write-Host "Pushed $tag. The release workflow is running: see the Actions tab of the repository."
