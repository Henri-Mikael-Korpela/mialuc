# Links editors/vscode into the VS Code extensions folder of the current user, so that VS Code
# loads the Mialu extension from this project. A link and not a copy, so that changes to the
# extension need only a reload of VS Code.
#
#   powershell -File scripts/install_vscode_extension.ps1              install
#   powershell -File scripts/install_vscode_extension.ps1 -Uninstall   remove the link again

param(
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$source = (Resolve-Path (Join-Path $PSScriptRoot '..\editors\vscode')).Path
$package = Get-Content (Join-Path $source 'package.json') -Raw | ConvertFrom-Json

# VS Code expects the folder of an extension to be named publisher.name-version.
$extensionsFolder = Join-Path $env:USERPROFILE '.vscode\extensions'
$target = Join-Path $extensionsFolder "$($package.publisher).$($package.name)-$($package.version)"

$item = Get-Item $target -Force -ErrorAction SilentlyContinue

if ($Uninstall) {
    if ($null -eq $item) {
        Write-Host "Nothing to remove, $target does not exist."
        exit 0
    }
    if ($item.LinkType -ne 'Junction') {
        throw "$target is not a link made by this script, so it is left alone."
    }
    # Deleting without -Recurse removes only the link and not the files that it points to.
    [System.IO.Directory]::Delete($target, $false)
    Write-Host "Removed $target. Reload VS Code."
    exit 0
}

if ($null -ne $item) {
    if ($item.LinkType -eq 'Junction' -and $item.Target -contains $source) {
        Write-Host "Already installed: $target"
        exit 0
    }
    throw "$target already exists and is not a link to $source, so it is left alone."
}

if (-not (Test-Path $extensionsFolder)) {
    throw "The VS Code extensions folder $extensionsFolder does not exist. Is VS Code installed?"
}

# A junction needs no administrator rights, unlike a symbolic link.
New-Item -ItemType Junction -Path $target -Target $source | Out-Null
Write-Host "Linked $target -> $source"
Write-Host 'Run "Developer: Reload Window" in VS Code.'
