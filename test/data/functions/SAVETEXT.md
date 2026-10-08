# SAVETEXT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（EE 扩展函数，与 LOADTEXT 成对）
- **签名**：int SAVETEXT(str text, int fileNo, int forceSavdir = 0, int forceUTF8 = 0)
- **签名**：int SAVETEXT(str text, str filepath, int forceSavdir = 0, int forceUTF8 = 0)
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录（仅依据源码注释 `int SAVETEXT str text, int fileNo{, int force_savdir, int force_UTF8}` 撰写）

## 语义

把字符串 `text` 写入文本文件，成功返回 `1`，任何失败（文件号越界、非法路径、写入 IO 异常等）返回 `0`（不抛错）。

第 2 参数有两种形态：
- 数值 `fileNo`：写入存档目录下 `txtNN.txt`（NN 为两位文件号）。此时会按需创建存档目录：`forceSavdir` 非 0 时用 `ForceSavDir` 配置目录并强制创建，否则用普通 `SavDir` 配置目录。
- 字符串 `filepath`：写入指定路径（相对路径基于 Emuera 所在目录）。拒绝绝对路径，路径中的 `..\` 会被剔除；若扩展名不在配置 `ValidExtension` 列表中则强制改为 `.txt`；父目录不存在时会自动创建。

`forceUTF8` 参数在当前源码中实际上不生效（详见备注）。

## 用法

### int SAVETEXT(str text, int fileNo{, int forceSavdir, int forceUTF8})
- `text`：要写入的文本内容（整体作为文件内容，不做追加）。
- `fileNo`：0 ～ int.MaxValue 的文件号，对应存档目录下的 `txtNN.txt`。
- `forceSavdir`：非 0 时强制使用/创建 `ForceSavDir` 指定目录（可省略，默认 0）。
- `forceUTF8`：本应强制 UTF-8 编码（可省略，默认 0；当前实现未生效，见备注）。
- 返回值：成功 `1`，失败 `0`。
```erb
	IF SAVETEXT("写一些文本", 0)
		PRINTL 已写入 txt00.txt。
	ENDIF
```
### int SAVETEXT(str text, str filepath{, ...})
- `filepath`：目标文件路径字符串；绝对路径与含 `..\` 的路径会被判为非法而返回 0；非法扩展名会改为 `.txt`。
- 其余参数同上。
- 返回值：成功 `1`，失败 `0`。
```erb
	IF SAVETEXT("记录", "log/memo.txt")
		PRINTL 已写入 memo.txt。
	ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:169`（`["SAVETEXT"] = new SaveTextMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6898`（`SaveTextMethod`，EM_私家版_LoadText＆SaveText機能拡張）；辅助函数 `Runtime/Script/Statements/Function/Creator.Method.cs:7099`（`GetSaveDataPathText`）、`Runtime/Utils/EvilMask/Utils.cs:214`（`GetValidPath`）

```text
构造：返回类型 = long；参数 = [String, Any, Int, Int]，第 2 参数起可省略（OmitStart = 2，即 2～4 参数）；CanRestructure = false。

GetIntValue(exm, args):
    savText ← args[0].GetStrValue(exm)
    forceSavdir ← args.Count > 2 且 args[2].GetIntValue(exm) != 0
    forceUTF8   ← args.Count > 3 且 args[3].GetIntValue(exm) != 0
    若 args[1] 的类型是 long（数值形态）:
        i64 ← args[1].GetIntValue(exm)
        若 i64 < 0 或 i64 > int.MaxValue: 返回 0
        filepath ← forceSavdir ? GetSaveDataPathText((int)i64, Config.ForceSavDir)
                               : GetSaveDataPathText((int)i64, Config.SavDir)
        ; GetSaveDataPathText → "txtNN.txt"（NN 两位编号）拼接在目录后
    否则（字符串路径形态）:
        filepath ← Utils.GetValidPath(args[1].GetStrValue(exm))
        ; 剔除 "..\"、拒绝绝对路径（有盘符根则返回 null），拼接到程序目录
        若 filepath == null: 返回 0
        若扩展名不在 Config.ValidExtension 中: filepath ← 改扩展名为 ".txt"
        （此形态下 forceUTF8 被置为 true，但实际编码仍取 Config.SaveEncode）
    try:
        若 i64 >= 0（数值文件号形态）:
            forceSavdir ? Config.ForceCreateSavDir() : Config.CreateSavDir()
        否则:
            若 filepath 含 '\'，创建其父目录
        File.WriteAllText(filepath, savText, Config.SaveEncode)
    catch:
        返回 0
    返回 1
```

## 备注

- ecd 两份主文档均未收录 SAVETEXT（EE 扩展函数），签名取自实现类的 XML 注释；本文档语义完全依据源码撰写。
- 文档/注释与源码的不一致：第 4 参数 `forceUTF8` 的编码选择逻辑（`forceUTF8 ? UTF-8 : Config.SaveEncode`）整段被注释掉，实际写入永远使用 `Config.SaveEncode`，因此该参数目前没有任何实际效果。
- 第 2 参数允许字符串路径是 EM 私家版扩展（原版只允许文件号），被注释掉的旧版实现只处理数值形态。
- 与之成对的是 `LOADTEXT`（同文件族，读出文本）。
- zh 套件未收录本函数。
