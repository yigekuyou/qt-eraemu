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

The optional disk AST cache is disabled for SAF loads because providers may omit
reliable modification times and QFileInfo::absolutePath() is not a portable parent
operation. In-memory parsed ASTs are used normally. Local game caching is unchanged.

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
