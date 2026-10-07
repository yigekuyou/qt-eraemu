# EXISTFILE

- **类别**：式中函数（EM 私家版扩展，`#region EM_私家版_追加関数`）
- **签名**：int EXISTFILE(str 路径)
- **文档来源**：`emuera.em/Readme/Emuera.EM_readme.txt:214-216`「◆ int EXISTFILE str」（「第一引数をパスとしてファイルの存否をチェックします。Emuera.exeを相対パスで指定(".."は無効)。存在しているなら1を返す，そうでない場合0を返す」）；两套中文文档未收录

## 语义

判断以参数为路径的文件是否存在：存在返回 `1`，不存在返回 `0`。

- 路径规则与 `LOADTEXT`/`SAVETEXT` 的字符串参数一致（**相对 Emuera.exe 所在目录**；`/` 会被换成 `\`；`..\` 被删除；**绝对路径不合法**——`Utils.GetValidPath` 遇到带根路径的输入返回 null → 结果为 0，`Runtime/Utils/EvilMask/Utils.cs:214-225`）。
- 只判「文件」不判目录：走的是 `File.Exists`（`Runtime/Script/Statements/Function/Creator.Method.cs:1266`），因此目录名、非法字符路径一律返回 0，不抛异常。
- 是「存否」的纯查询：不会创建、读取或修改文件内容；也不受游戏存档/`VarExt` 之类机制影响。
- `CanRestructure = false`：即使参数是常量字符串也在运行期求值（与 `EXISTVAR` 的 `true` 不同）。

## 用法

### int EXISTFILE(str 路径)
- 路径：字符串表达式，相对 Emuera.exe 的路径（如 `"erb\\MAIN.ERB"`、`"csv\\Chara.csv"`）。
- 返回值：存在 `1`；不存在 / 绝对路径 / 目录 / 非法 → `0`。
```erb
PRINTFORML {EXISTFILE("erb\\MAIN.ERB")}     ;→ 1（存在时）
PRINTFORML {EXISTFILE("no_such.txt")}       ;→ 0
PRINTFORML {EXISTFILE("C:\\Windows\\notepad.exe")}   ;→ 0（绝对路径不受理）
IF EXISTFILE("savedata\\save01.sav") == 0
	PRINTL 存档文件不存在
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:222`（`["EXISTFILE"] = new ExistFileMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1255`（`ExistFileMethod`）
- 路径工具：`Runtime/Utils/EvilMask/Utils.cs:214`（`GetValidPath`）

```text
构造（Creator.Method.cs:1257-1261）:
    返回类型 = long
    argumentTypeArray = [typeof(string)]      ; 恰好 1 个字符串参数
    CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:1263-1268）:
    filepath = Utils.GetValidPath(args[0].GetStrValue(exm))
        ; GetValidPath：'/'→'\'、"..\"删除、带根路径 → null、否则 Path.Combine(ExeDir, path)
    if filepath != null && File.Exists(filepath): return 1
    return 0
```

## 备注

- readme 与源码一致（含「相对 Emuera.exe」「".." 无效」的说明），无冲突。
- 与 `ENUMFILES`（同区新增）的关系：`ENUMFILES` 枚举目录内容、返回相对路径；本函数只判单个文件的存否。
- 注意 `..\` 的删除发生在字符串层面（`Replace("..\\", "")`）：`"a..\\b"` 会被改成 `"ab"`（推定副作用，源码直接替换），路径安全是真但没有正规化。
