# Releasing

The scripts for versioning and releasing use PowerShell, so they need nothing to install on Windows.

## Versioning

`scripts/bump-version.ps1 <version>` sets the version of the compiler, a semantic version such as `0.2.0`, in [src/version.h](../src/version.h) and in the example output of `mialuc --version` in [mialuc](mialu-cli.md#print-the-version-of-the-compiler).

```
powershell -File scripts/bump-version.ps1 0.3.0
```

## Releasing

After the version is bumped and committed, `scripts/deploy-release.ps1` tags the commit of main as `v<version>` and pushes it, which starts the release workflow in [.github/workflows/release.yml](../.github/workflows/release.yml). It refuses to run with uncommitted changes, off main, behind `origin/main`, or when the tag exists, and asks before pushing unless it is given `-Yes`.

```
powershell -File scripts/deploy-release.ps1
```
