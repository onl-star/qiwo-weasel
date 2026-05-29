# Qiwo Weasel

This directory is the Qiwo working copy of upstream `weasel`.

The upstream submodule at `../weasel` is kept as a reference only. Qiwo-specific
changes happen here.

## Current Qiwo changes

- display name is changed to `Qiwo Input Method` / `齐我输入法`
- bundled icons are replaced with Qiwo Windows icon assets
- `default.custom.yaml` defaults to `rime_frost` on first setup
- installer staging copies bundled `rime-frost` into `data/rime-frost`
- installer staging publishes `qiwo-rime-sync` into `qiwo-sync`
- `WeaselDeployer.exe /sync` runs Qiwo WebDAV sync and redeploys Rime afterward

## WebDAV environment variables

First version keeps configuration outside the UI:

```powershell
$env:QIWO_WEBDAV_URL = "https://dav.example.com/qiwo-rime-sync"
$env:QIWO_WEBDAV_USERNAME = "name"
$env:QIWO_WEBDAV_PASSWORD = "password"
$env:QIWO_DEVICE_ID = "windows-main"
```

`QIWO_WEBDAV_URL` is required for sync. The username/password variables are
optional if the WebDAV endpoint does not need Basic auth.

## Build notes

`build.bat` expects the monorepo layout:

```text
../qiwo-sync-core
../rime-frost
```

When building Weasel, it publishes the sync CLI and stages `rime-frost` resources
before creating an installer.

For a self-contained Qiwo build on Windows, run:

```batch
build-qiwo.bat
```

The wrapper downloads/stages Boost, prebuilt librime, portable NSIS, Qiwo sync,
and rime-frost data under this working copy before invoking the normal Weasel
build.
