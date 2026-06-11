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
- `Ctrl+grave` / `F4` Rime switcher exposes the Qiwo auto spacing option

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

## Input formatting

The `Ctrl+grave` / `F4` Rime switcher includes `中英数字自动空格`. Toggling it
changes subsequent committed text immediately. Weasel reads the live Rime option
`auto_commit_spacing`; if no switcher state has been saved yet, it falls back to
`input/auto_commit_spacing` in `weasel.yaml`, then defaults to enabled.

`rime-frost` stays an upstream schema resource. Qiwo injects the switcher entry
through generated `default.custom.yaml` and `rime_frost*.custom.yaml` patches;
the committed-text formatting is implemented by `qiwo-input-format-core`.
Existing custom YAML files are not overwritten; Qiwo only merges the missing
switcher/save-options patch entries it owns. When TSF can read the surrounding
document text, the formatter also handles spacing at the cursor boundary; if the
target application does not expose surrounding text, it still formats the current
commit text internally.

## Build notes

`build.bat` expects these repository-local submodules:

```text
qiwo-sync-core
rime-frost
```

When building Weasel, it publishes the sync CLI and stages `rime-frost` resources
before creating an installer.

For a self-contained Qiwo build on Windows, run:

```batch
build-qiwo.bat
build-qiwo.bat -NoDownloads # 如果已经有其他所需的组件
```

The wrapper downloads/stages Boost, prebuilt librime, portable NSIS, Qiwo sync,
and rime-frost data under this working copy before invoking the normal Weasel
build.
