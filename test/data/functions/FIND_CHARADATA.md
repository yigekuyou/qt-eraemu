# FIND_CHARADATA

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：int FIND_CHARADATA(str pattern = "*")
- **文档来源**：`ecd/Expression.md` 未收录（内置函数一览不含本函数）；`ecd/Command.md` 未收录；zh 套件未收录。语义完全依据源码。

## 语义

检索 `ERB/ERBof` 目录（`Program.DatDir`，即保存 `chara_*.dat` / `var_*.dat` 的数据目录）下文件名匹配 `chara_<pattern>.dat` 的文件，把匹配到的「数据文件名」（去掉 `chara_` 前缀和 `.dat` 后缀的部分，即保存时使用的角色存档名）写入 `RESULTS:0` 起的字符串数组，函数返回匹配到的文件个数。

参数为通配符模式（可省略，缺省 `*`，即匹配全部）。本函数用于枚举已存在的角色存档文件。与之对应检索变量存档（`var_*.dat`）的 `FIND_VARDATA` 在本仓库中注册行被注释、未注册，无法使用。

## 用法

### int FIND_CHARADATA(pattern)
- pattern：字符串，文件名通配符模式（如 `"*"`、`"セーブ*"`），匹配 `chara_<pattern>.dat`。省略时为 `"*"`。
- 返回值：匹配到的文件个数；同时把文件名列表写入 `RESULTS:0`、`RESULTS:1`……（超出 `RESULTS` 长度的部分被截断）。
```erb
count = FIND_CHARADATA()
FOR i, 0, count
    PRINTFORML 发现角色存档：{RESULTS:i}
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:61`（`["FIND_CHARADATA"] = new FindFilesMethod(EraSaveFileType.CharVar)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2488`（`FindFilesMethod`）；枚举核心 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1800`（`GetDatFiles`）

```text
FindFilesMethod(type = EraSaveFileType.CharVar):
构造：返回类型 = long；
    参数表 argumentTypeArrayEx = [[str]]，OmitStart = 0（全部可省略）；
    CanRestructure = false。

GetIntValue(exm, args):
    pattern = "*"
    若 args.Count > 0 且 args[0] != null: pattern = args[0].GetStrValue(exm)
    filepathes = VariableEvaluator.GetDatFiles(charadat: true, pattern)
    results = RESULTS 的底层 string[]
    若 filepathes.Count <= results.Length:
        filepathes 全部复制进 results（RESULTS:0 起）
    否则:
        前 results.Length 个复制进 results（其余丢弃）
    返回 filepathes.Count

VariableEvaluator.GetDatFiles(charadat = true, pattern):
    files = []
    若 Program.DatDir 目录不存在: 返回空列表
    searchPattern = charadat ? "chara_" + pattern + ".dat" : "var_" + pattern + ".dat"
    pathes = Directory.GetFiles(DatDir, searchPattern, 仅顶层目录)
    对每个 path:
        若扩展名不是 ".dat"（忽略大小写）: 跳过
        filename = 去掉扩展名
        filename = charadat ? filename[6..]（去掉 "chara_"） : filename[4..]
        若 filename 为空串: 跳过
        files.Add(filename)
    返回 files
```

## 备注

- 本仓库的文档材料（ecd、zh 套件）均未收录本函数，以上语义仅由源码得出。
- 姊妹函数 `FIND_VARDATA` 的注册行 `Runtime/Script/Statements/Function/Creator.cs:61` 被注释（`//methodList["FIND_VARDATA"] = new FindFilesMethod(EraSaveFileType.Var);`），在本仓库中未注册、不可调用；source_index.md 将其标注为 register，与实际不符，特此记录。
- 原版 Emuera（1.8xx）中 FIND_CHARADATA/FIND_VARDATA 是先把文件名列表放入 RESULTS 再由脚本统计；本实现直接返回个数并填充 RESULTS，两者兼容。
- 注意与 `CHKCHARADATA`（检查指定存档文件状态）区分：本函数是枚举，不是状态检查。
