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
- tray and TSF language bar menus expose WebDAV sync settings

## WebDAV settings

Open the tray menu or the TSF language bar menu, then choose
`WebDAV sync settings`. The dialog stores settings under the Rime user folder:

```text
.qiwo-sync/webdav.ini
```

The saved fields are:

```ini
[webdav]
server_url=https://dav.example.com
remote_path=qiwo-rime-sync
username=name
password=password
device_id=windows-main
```

Environment variables still override saved settings when present:

```powershell
$env:QIWO_WEBDAV_URL = "https://dav.example.com/qiwo-rime-sync"
$env:QIWO_WEBDAV_USERNAME = "name"
$env:QIWO_WEBDAV_PASSWORD = "password"
$env:QIWO_DEVICE_ID = "windows-main"
```

The username/password fields are optional if the WebDAV endpoint does not need
Basic auth.

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
