# Android game library

Select a scan root in the native folder picker. Its immediate children containing
CSV and ERB directories appear in the library. Selecting a game passes its
original content URI to the asynchronous engine loader. No game directory,
resource, audio file, CSV or ERB source is imported or copied to app storage.
Revoked grants require selecting the scan root again.

Existing SAF paths are resolved by enumerating QFileInfo entries and preserving
absoluteFilePath(), including opaque document IDs and mixed-case display names.
Content URIs bypass local filesystem normalization and QDir::cleanPath. Resource
atlas CSVs retain their real containing directory, including nested atlases.
AudioPlayers passes content URLs directly to MediaPlayer.

Qt 6.11.2's folder picker persists read permission only. If an existing persisted
write grant covers the selected tree, the engine probes writing a uniquely named
file in sav and removes it. A successful probe uses the original game directory
for all saves. Otherwise saves use AppDataLocation/save-data/<SHA256 URI>/sav;
SAVEGLOBAL uses the same storage root. No game files are copied during fallback.
The fallback remains stable for the same URI, including across restarts. Changing
providers/URIs creates a separate save identity. Existing saves in a read-only
source are not imported automatically. Local games retain their original save
locations. Android uninstallation may remove local saves.

AST disk caching has been removed. SAF loads and local game loads use the in-memory
parsed AST cache; no parsed AST is persisted to app storage or the game directory.

## On-device logging

Qt's default message handler writes to stderr. An app started from the launcher has no
stderr attached to anything, so **no engine log reaches logcat**: on a clean start
`adb logcat -b all` contains zero hits for `EraEngine`, `ErbLoader`, `异步装载` or
`GameBase`. `setprop log.redirect-stdio true` is rejected on Android 16, so main.cpp
installs its own handler under `Q_OS_ANDROID` and forwards every Qt message to
`__android_log_print` (tag `Emuera`; the logging category, e.g. `era.trace`, is prefixed
to the message body exactly like Qt's `%{category}: %{message}` pattern).

```sh
adb logcat -s Emuera          # engine lifecycle, load stages, heartbeat, warnings
adb logcat -s Emuera | grep 装载心跳
```

Release builds that define `QT_NO_DEBUG_OUTPUT`/`QT_NO_INFO_OUTPUT` still lose qDebug/qInfo
at compile time; the debug APK keeps everything.

## Loading a large tree is latency-bound, not bandwidth-bound

Measured on OnePlus/Android 16 with `eraH` (920 ERB/ERH, 114 subdirectories, ~7 MB of ERB
text) behind a tree URI: a full reload takes ~64 s, of which ~53 s goes to the directory
walk - one worker thread, ~0.4 core, no meaningful data transfer, flat RSS. The walk
issues roughly one `DocumentsContract` round trip per directory listing plus one
`QFileInfo::exists` per file (~1034 calls, ~51 ms each); Android documents this as
"traversing a large number of files ... performance may decrease". The actual parallel
work (read+decode+AST, 7 threads, up to 4.8 cores) takes only ~10 s and raises RSS from
210 MB to 527 MB.

`ErbLoader::collectFiles` therefore reuses the `QFileInfo` returned by `entryInfoList`
(`absoluteFilePath()`) instead of re-joining and re-probing every child, which removes
about 920 round trips per load. The UI shows stage/progress/elapsed/stall from the load
heartbeat, so the walk is visible instead of looking like a hang. Do not let the screen
turn off during a load: the OS freezes the process (observed `do_freezer_trap`), which
pauses the load mid-way.

Source references verified against the v6.11.2 tag:
- qtbase/src/plugins/platforms/android/androidcontentfileengine.cpp:
  fileName(), DocumentFile::parseFromAnyUri(), iterator currentFilePath().
- qtbase/src/plugins/platforms/android/qandroidplatformfiledialoghelper.cpp:
  takePersistableUriPermission() adds WRITE only for AcceptSave.

Desktop tests cover selection/persistence, rejected paths, original-path
identity, local resources and save routing, URI preservation, and QML metadata.
Actual provider behavior, grant revocation, restart persistence and multimedia
content playback require Android device tests. Some providers cannot create new
files from Qt's appended document-path convention; these use local saves.
SAF resource paths containing parent traversal (..) are rejected; use paths under
the resource directory. No Android SDK or emulator is assumed by desktop tests.

## `_Rename.csv` must load on content:// too (936 spurious warnings)

eraH ships `CSV/_Rename.csv` (2030 entries) mapping `[[口上:主人Ａ射精]]` to `0101`,
and its `_fixed.config` enables it with the **Chinese** key `启用_Rename.csv:YES`.
`ConfigLoader` matches the original Japanese keys (`_Rename.csvを利用する`) exactly,
so the engine-level switch stays off for this game on every platform. On desktop that
did not matter: `ErbLoader::tryAutoLoadRename()` rescans the ERB directory's sibling
`CSV/` folder and loads `_Rename.csv` regardless of the config flag. That function used
to bail out early for `content://` with "EraEngine loads sibling CSV explicitly" - which
relied on the config switch, i.e. on a key the game never writes. Result: on device the
rename table never loaded, `[[...]]` stayed a string literal, and every one of the 468
`C_FIRSTTIME_KOJO([[口上:主人Ａ射精]])` call sites parsed as Str-into-Int: **936
warnings on device vs 0 on desktop** (plus a duplicated-warning artifact: each site is
listed 2-4 times because an instruction's condition AST is validated both as
`line.condition` and as an operand - see `finalizeValidation`).

confirmed by hiding `CSV/_Rename.csv` on desktop: `dump_warnings /tmp/eraH` then prints
exactly the same 936 warnings, so this is not Android-specific parser behaviour, only
the content:// early return in the rename auto-detection. `tryAutoLoadRename` now
handles `content://` the same way as local paths (string-level path split, `QDir`
listing through the Android content plugin, `GamePaths::join` for the percent-encoded
child URI) and logs `[load] _Rename.csv 自动装载: <path> 词条 N 条`.

## Launcher icon and app label

The manifest placeholder `%%INSERT_APP_ICON%%` resolves to no resource of our own
(aapt2 reports `icon=''`), so the APK ships the Qt default. `android/res` now carries
an adaptive icon (`mipmap-anydpi-v26/ic_launcher.xml` over `drawable/ic_launcher_*.xml`
vectors) with a 66dp-safe `>_` console glyph, PNG buckets for API 24/25 generated by
`test/make_android_icon.py`, and `android:label="Emuera"` / `android:icon`/`roundIcon`
set explicitly in `android/AndroidManifest.xml`.

## Mobile chrome: drawer instead of a popup menu

`Main.qml` used a `Menu` popup for the mobile toolbar's hamburger button. On touch
screens that menu is a small, dim, hard-to-hit surface; it is now a modal `Drawer`
(`appDrawer`, right edge) with 48dp rows that closes after any entry triggers, and
its content scrolls in a `Flickable` because in landscape the rows exceed the screen
height. Once a game has presented, the toolbar itself is hidden - it is only chrome
between the user and the stage - and a semi-transparent floating handle at the same
top-right corner (plus a right-edge swipe) opens the drawer; the loading mask owns
the screen while loading, so the drawer, its toolbar button and the handle are all
disabled during a load.

A pre-existing QtQuick-on-Android warning (`QObject: Cannot create children for a
parent that is in a different thread`) appears for popup content (ToolBar, the game
picker delegates, the drawer). It was always there - it only became visible once Qt
messages got bridged into logcat. The dialogs render fine; treat it as noise until it
is proven to break something.
