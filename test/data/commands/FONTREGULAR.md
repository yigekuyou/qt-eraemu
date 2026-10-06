# FONTREGULAR

- **类别**：命令
- **签名**：`FONTREGULAR`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「文字样式相关」`### FONTREGULAR` 小节（含 FONTBOLD/FONTITALIC 共用说明）；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

把当前文字样式重置为 Regular（普通），即同时取消加粗、倾斜等已设置的样式。与按位叠加的 `FONTBOLD`/`FONTITALIC` 相对，`FONTREGULAR` 是整体替换样式。等效于 `FONTSTYLE 0`。无参数、无返回值，只影响之后的 PRINT 系输出。

## 用法

### FONTREGULAR
无参数。

```erb
FONTBOLD
FONTITALIC
PRINTL 加粗＋倾斜
FONTREGULAR
PRINTL 这里恢复为普通样式
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:282`（`new FONTREGULAR_Instruction()`，flag = METHOD_SAFE | EXTENDED；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:182`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1648`（`FONTREGULAR_Instruction`，VOID 参数）

```text
构造：ArgBuilder = VOID（无参数）；flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    若 当前操作系统不是 Windows:
        直接 return（什么都不做）
    否则:
        exm.Console.SetStringStyle(FontStyle.Regular)
        # 注意：不是按位清除，而是把整个样式替换为 Regular
```

其中 `SetStringStyle` 定义于 `UI/Game/EmueraConsole.Print.cs:91`（`userStyle.FontStyle = fs`）。

## 备注

- 与同系的 `FONTBOLD`/`FONTITALIC` 一样带非 Windows 平台短路判断；`FONTSTYLE` 没有该判断。
- `FONTSTYLE 0` 与本命令效果相同（ecd 文档明示）。
- 其余无特别差异。
