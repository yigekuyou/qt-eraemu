# LOADTEXT

- **类别**：式中函数
- **签名**：str LOADTEXT(int fileNo{, int force_savdir, int force_UTF8})（EM 私家版扩展：第 1 参数也可以是 str filepath）
- **文档来源**：ecd/zh 两套文档均未收录本函数；语义依据源码（含源码内注释掉的旧实现）推定

## 语义

读取文本文件内容并作为字符串返回。文件不存在或读取失败时返回空字符串 `""`，不抛错。

两种取文件方式：

1. **文件编号形态**（原始形态）：读取存档目录下名为 `txt<fileNo:00>.txt` 的文件（如 `txt00.txt`）。第 2 参数 `force_savdir` 非 0 时使用 `Config.ForceSavDir`（强制存档目录）代替 `Config.SavDir`；第 3 参数在旧实现中用于强制 UTF-8 编码，现行实现已不再使用该参数。
2. **文件路径形态**（EM_私家版_LoadText＆SaveText機能拡張）：第 1 参数传字符串路径时，把 `/` 替换为 `\`、删除 `..\` 片段后拼接到 Emuera 可执行目录下（拒绝绝对路径，返回 null 则得到 `""`）；扩展名必须在 `Config.ValidExtension` 允许列表内，否则返回 `""`。

读取时通过 `EncodingHandler.DetectEncoding(filepath)` 自动检测编码；读入后把所有 `\r` 删除（注释原文："一貫性の観点で\rには死んでもらう"）。

## 用法

### str LOADTEXT(int fileNo{, int force_savdir, int force_UTF8})
- `fileNo`：整数，文件编号（0～int.MaxValue）；负数或超范围返回 `""`。
- `force_savdir`：整数，省略时为 0；非 0 时改用强制存档目录 `Config.ForceSavDir`。
- `force_UTF8`：整数，省略时为 0；现行实现中**无效**（旧实现中非 0 强制按 UTF-8 读取）。
- 返回值：文件全部文本内容；文件不存在或读取异常时 `""`。
```erb
S = LOADTEXT(0)            ; 读取 SavDir\txt00.txt
S = LOADTEXT(1, 1)         ; 读取 ForceSavDir\txt01.txt
```

### str LOADTEXT(str filepath)
- `filepath`：字符串，相对 Emuera 目录的相对路径（不得是绝对路径，不得含 `..\`；`/` 会先转为 `\`）。
- 扩展名不在允许列表（`Config.ValidExtension`）内时返回 `""`。
- 返回值：文件全部文本内容（`\r` 已被删除）；不存在或异常时 `""`。
```erb
S = LOADTEXT("data/memo.txt")
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:170`（`["LOADTEXT"] = new LoadTextMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7007`（`LoadTextMethod`）；路径辅助 `Utils.GetValidPath` 在 `Runtime/Utils/EvilMask/Utils.cs:214`

```text
构造：返回类型 = string；参数表 = [Any, int, int]，从第 2 参数（下标 1）起可省略；CanRestructure = false。

GetStrValue(exm, args):
    forceSavdir ← args.Count > 1 且 args[1].GetIntValue(exm) != 0
    forceUTF8   ← args.Count > 2 且 args[2].GetIntValue(exm) != 0
    若 args[0] 是数值（long）:
        i64 ← args[0].GetIntValue(exm)
        若 i64 < 0 或 i64 > int.MaxValue: 返回 ""
        filepath ← forceSavdir ? Format("{0}txt{1:00}.txt", Config.ForceSavDir, i64)
                               : Format("{0}txt{1:00}.txt", Config.SavDir, i64)
    否则（第 1 参数为字符串）:
        filepath ← Utils.GetValidPath(args[0].GetStrValue(exm))
            ; '/'→'\'，删除 "..\"，绝对路径返回 null
        若 filepath == null: 返回 ""
        ext ← filepath 有扩展名 ? 扩展名小写 : ""
        若 ext 不在 Config.ValidExtension 中: 返回 ""
    若 !File.Exists(filepath): 返回 ""
    try: ret ← File.ReadAllText(filepath, EncodingHandler.DetectEncoding(filepath))
    catch: 返回 ""
    返回 ret.Replace("\r", "")     ; 删除全部回车符
```

## 备注

- ecd/Command.md、ecd/Expression.md、zh 套件均未收录本函数（同族的 `SAVETEXT` 也未收录）；签名来自实现类源码注释 `/// str LOADTEXT int fileNo{, int force_savdir, int force_UTF8}`。
- 源码内保留了旧实现（注释掉）：旧实现第 3 参数 `force_UTF8` 非 0 时强制按 UTF-8 读取；现行实现改用编码自动检测，`forceUTF8` 变量虽计算但未被使用——文档（若有）与现行源码在此存在差异。
- 本函数是读取文本；对应的写出函数为 `SAVETEXT`（`Runtime/Script/Statements/Function/Creator.cs:170` 注册）。
