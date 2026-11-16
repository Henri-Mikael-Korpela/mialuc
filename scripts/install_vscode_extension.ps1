# Links the VS Code extensions of editors/ into the VS Code extensions folder of the current user,
# so that VS Code loads them from this project: editors/vscode, the language support,
# editors/vscode-debug, the debugging, and editors/vscode-lsp, the language server. Links and not
# copies, so that changes to the extensions need only a reload of VS Code.
#
#   powershell -File scripts/install_vscode_extension.ps1              install
#   powershell -File scripts/install_vscode_extension.ps1 -Uninstall   remove the links again

param(
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

$extensionsFolder = Join-Path $env:USERPROFILE '.vscode\extensions'
if (-not $Uninstall -and -not (Test-Path $extensionsFolder)) {
    throw "The VS Code extensions folder $extensionsFolder does not exist. Is VS Code installed?"
}

foreach ($name in @('vscode', 'vscode-debug', 'vscode-lsp')) {
    $source = (Resolve-Path (Join-Path $PSScriptRoot "..\editors\$name")).Path
    $package = Get-Content (Join-Path $source 'package.json') -Raw | ConvertFrom-Json

    # VS Code expects the folder of an extension to be named publisher.name-version.
    $target = Join-Path $extensionsFolder "$($package.publisher).$($package.name)-$($package.version)"
    $item = Get-Item $target -Force -ErrorAction SilentlyContinue

    if ($Uninstall) {
        if ($null -eq $item) {
            Write-Host "Nothing to remove, $target does not exist."
            continue
        }
        if ($item.LinkType -ne 'Junction') {
            throw "$target is not a link made by this script, so it is left alone."
        }
        # Deleting without -Recurse removes only the link and not the files that it points to.
        [System.IO.Directory]::Delete($target, $false)
        Write-Host "Removed $target."
        continue
    }

    if ($null -ne $item) {
        if ($item.LinkType -eq 'Junction' -and $item.Target -contains $source) {
            Write-Host "Already installed: $target"
            continue
        }
        throw "$target already exists and is not a link to $source, so it is left alone."
    }

    # A junction needs no administrator rights, unlike a symbolic link.
    New-Item -ItemType Junction -Path $target -Target $source | Out-Null
    Write-Host "Linked $target -> $source"
}

Write-Host 'Run "Developer: Reload Window" in VS Code.'
