# AST runtime save migration

The flattened ERB AST remains the execution representation. Ordinary, GLOBAL,
SAVEVAR and character persistence use typed documents rather than the former Qt
text/JSON protocol. Old port files are rejected without deletion or migration.

Paths relative to the writable game storage root:

* Normal: `sav/save##.sav`
* GLOBAL: `sav/global.sav`
* Characters: `dat/chara_<name>.dat`

Normal/GLOBAL honor `SystemSaveInBinary` (Japanese configuration key
`セーブデータをバイナリ形式で保存する`, C# default false). False selects the native
EraDataStream 1808 text layout: ordered legacy fields, `__FINISHED` arrays,
`__EMUERA_1808_STRAT__`, and typed `__EMU_SEPARATOR__` extension sections.
Text is written UTF-8 with BOM and CRLF. Current text reading supports UTF-8;
legacy Shift-JIS/GBK text and pre-1808 text sections are outside this verified scope.
Text cannot represent embedded CR/LF or reserved delimiter lines. The reference
text writer does not support multidimensional strings or user character variables;
those cannot be emitted losslessly and produce an explicit save error.

Binary uses C# 1808 headers, UTF-16LE strings, tagged rectangular arrays and compact
integers. Normal/GLOBAL include the EM trailing EOF. `ZipSaveData`
(`セーブデータを圧縮して保存する`) selects the EM ZIP header and gzip payload after
the uncompressed header when binary mode is enabled. Readers detect plain binary,
gzip and native text independent of current write settings. SAVECHARA and the
SAVEVAR extension retain their binary containers, with gzip selected only when
both binary and zip settings are true. zlib is a required codec dependency.

Normal excludes GLOBAL; GLOBAL contains builtins GLOBAL/GLOBALS and declarations
with both GLOBAL and SAVEDATA. User variables require SAVEDATA. All GLOBAL
runtime declarations, including those without SAVEDATA, survive ordinary load.
Builtin save eligibility follows the C# vocabulary. Character blocks map in list
order; repeated template NO values do not alias runtime slots. Array loads copy
per-axis overlap and clear omitted cells. Ordinary load resets LOCAL/ARG/REF and
private STATIC to declaration initializers, including string rank three defaults.
Successful loads update LASTLOAD_NO, LASTLOAD_VERSION and LASTLOAD_TEXT. Both
script LOADDATA and UI LOADGAME notify the runner to clear obsolete call, loop,
function-local and event contexts before SYSTEM_LOADEND/EVENTLOAD. Game code and
version checks use GameBase fields and compatible-minimum rules.

SAVEVAR/LOADVAR syntax remains an EE/port extension using the C# Var container.
EM Map/XML/DT payloads remain rejected atomically. User character rank three is
excluded, matching the reference CharacterData loader. Character special/SP flags
are not binary fields. Android provider integration requires real-provider testing.

`src/eraengine/test/fixtures/csharp-save` includes original C# writer output for
normal/GLOBAL/character plain and gzip files, source hashes and fixture manifests.
The linked original C# EraDataWriter also generates native text fixtures with
integer matrices and cubes. Keys used for codec fixtures are synthetic coverage
schemas, while the bytes themselves come from the original writers. These are
protocol tests, not complete gameplay equivalence fixtures.

Iteration 26 verification logs: `/tmp/emuera-save26-build.log`,
`/tmp/emuera-save26-ctest.log`, and repair/final logs
`/tmp/emuera-save26-final-build.log`, `/tmp/emuera-save26-final-ctest.log`,
`/tmp/emuera-save26-final-interop.log`. The final verification runs the complete
44-test suite, exports six C++ plain/gzip files for the unmodified C# binary
reader, and exports runtime normal/GLOBAL text files for the original C#
EraDataReader. Check the final logs for the actual outcome.
