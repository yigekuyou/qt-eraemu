# GETFONT

- **类别**：式中函数
- **签名**：str GETFONT()
- **文档来源**：`ecd/Command.md`「### GETFONT」；`ecd/Expression.md`（表达式内函数签名列表）

## 语义

返回当前使用的字体名称（字符串）。返回值与 `SETFONT` 指令参数格式相同。

若没有使用过 `SETFONT` 指令，则返回设置中的默认字体名。

无参数。只能在表达式中使用。注意与 `CHKFONT`（检查某字体是否可用，返回 int）区分。

## 用法

### str GETFONT()
- 无参数。
- 返回值：当前字体名称字符串。
```erb
F = GETFONT()
IF F == ""
    PRINTL 使用默认字体
ELSE
    PRINTL 当前字体：{F}
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:49`（`["GETFONT"] = new GetFontMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2647`（`GetFontMethod`）

```text
构造：返回类型 = string；参数 = []；CanRestructure = false。

GetStrValue(exm, args):
    返回 GlobalStatic.Console.StringStyle.Fontname    ; 当前控制台字体名
```

## 备注

- ecd/Command.md 以「将当前使用的字体名称返回到 `RESULT:0` 中」描述；实际注册形态是式中函数。
- 文档说"若没有使用过 SETFONT 指令则返回设置中的默认字体"与实现一致：`SETFONT` 未指定时 `StringStyle.Fontname` 即配置中的字体名。
- 返回类型为 string；当字体重置为默认时实现可能返回空串（与 `SETFONT` 无参调用恢复默认对应），文档未提及此细节。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
