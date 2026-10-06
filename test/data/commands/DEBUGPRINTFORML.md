# DEBUGPRINTFORML

- **类别**：命令（DEBUG 系，调试模式专用）
- **签名**：
  - `DEBUGPRINTFORML <FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「DEBUG系」小节（「DEBUGPRINTFORM 的换行版，输出后换行」）；zh 套件 `Debug_Mode.md`。

## 语义

`DEBUGPRINTFORM` 的换行版：参数按 FORM 语法展开后输出到调试控制台，并在输出后换行。

只在调试模式下动作；非调试模式下什么都不做、不解析参数，格式串错误不会报错。也不能用 `n` 后缀。相当于调试窗口里的 `PRINTFORML`（但输出目标不同、不受 `SKIPDISP` 影响）。

## 用法

### `DEBUGPRINTFORML <FORM格式文本>`
- `<FORM格式文本>`：支持 FORM 语法的字符串，展开后输出到调试控制台并换行。
```erb
FOR X, 0, 3
  DEBUGPRINTFORML TRYCOM:{X} = {TRYCOM:X}, 回数 {TSTR:X}
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:375`（`new DEBUGPRINT_Instruction(true, true)`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:262`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:541`（与 `DEBUGPRINT` 共用 `DEBUGPRINT_Instruction` 类，构造参数 `form=true, newline=true`）；实际输出在 `UI/Game/EmueraConsole.cs:2056`（`DebugPrint()`）、`UI/Game/EmueraConsole.cs:2068`（`DebugNewLine()`）

```text
指令类 DEBUGPRINT_Instruction(form=true, newline=true):
    参数构造器 = FORM_STR_NULLABLE
    flag = METHOD_SAFE | EXTENDED | DEBUG_FUNC | PRINT_NEWLINE

解析期:
    非调试模式下跳过参数解析

执行期 DoInstruction:
    若 func.Argument.IsConst: str = 常量串
    否则:                    str = 表达式项求值（FORM 展开）
    exm.Console.DebugPrint(str)
    若 func.Function.IsNewLine():   # 本变体注册时 newline=true（flag 含 PRINT_NEWLINE）
        exm.Console.DebugNewLine()  # 调试模式下向调试缓冲追加系统换行符

DebugPrint(str):   非调试模式直接 return，否则 dConsoleLog.Append(str)
DebugNewLine():    非调试模式直接 return，否则 dConsoleLog.Append(Environment.NewLine)
```

## 备注

- 四个 DEBUGPRINT 变体共用一个类，本命令是 `form=true, newline=true` 的组合，即「FORM 格式 + 换行」。
- 换行写入的是 `Environment.NewLine`（Windows 上为 CRLF），与主控制台换行相互独立。
- zh 套件把四个变体合并成一句话描述，未单独列小节；名称写作 `DebugPrintFormL`，实际命令名为 `DEBUGPRINTFORML`。
