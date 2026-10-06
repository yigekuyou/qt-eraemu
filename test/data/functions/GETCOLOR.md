# GETCOLOR

- **类别**：式中函数
- **签名**：int GETCOLOR()
- **文档来源**：`ecd/Command.md`「### GETCOLOR」；`ecd/Expression.md`（表达式内函数签名列表）

## 语义

返回当前使用的文字颜色，数值格式为 `0xRRGGBB`（即红绿蓝各 8 位的合成值）。

例如当前文字颜色为橙色（RGB `255,128,0`）时，返回 `16744448`（十六进制 `0xFF8000`）。与 `SETCOLOR` 设置的值对应。

无参数。只能在表达式中使用（如 `A = GETCOLOR()`）。

## 用法

### int GETCOLOR()
- 无参数。
- 返回值：当前文字颜色，`0xRRGGBB` 格式的整数。
```erb
C = GETCOLOR()
SETCOLOR 0xFF8000
PRINTL 橙色文字
SETCOLOR C          ; 用 GETCOLOR 的返回值恢复原颜色
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:43`（`["GETCOLOR"] = new GetColorMethod(false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2572`（`GetColorMethod`）

```text
构造（isDef = false）：返回类型 = long；参数 = []；CanRestructure = false（结果随运行状态变化，不可常量折叠）。

GetIntValue(exm, args):
    color ← GlobalStatic.Console.StringStyle.Color    ; 当前控制台文字颜色
    返回 color.ToArgb() & 0xFFFFFF                    ; 取低 24 位，即 0xRRGGBB
```

## 备注

- ecd/Command.md 以「将当前使用的文字颜色返回到 `RESULT:0` 中」的命令式口吻描述；实际注册形态是式中函数，在表达式中调用并返回值，语义一致。
- 同类函数：`GETDEFCOLOR`（默认文字色，同一实现类的 `isDef = true` 变体）、`GETBGCOLOR`/`GETDEFBGCOLOR`（背景色，`GetBGColorMethod`）、`GETFOCUSCOLOR`。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
