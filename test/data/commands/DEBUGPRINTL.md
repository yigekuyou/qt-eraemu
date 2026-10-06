# DEBUGPRINTL

- **类别**：命令（DEBUG 系，调试模式专用）
- **签名**：
  - `DEBUGPRINTL <字符串>`
- **文档来源**：`ecd/docs/translation/Command.md`「DEBUG系」小节（「DEBUGPRINT 的换行版，输出后换行」）；zh 套件 `Debug_Mode.md`。

## 语义

`DEBUGPRINT` 的换行版：把普通字符串（不支持 FORM 格式）输出到调试控制台，并在输出后换行。

只在调试模式下动作；非调试模式下什么都不做、不解析参数。不能使用 `n` 后缀；需要 FORM 格式时用 `DEBUGPRINTFORM` / `DEBUGPRINTFORML`。

## 用法

### `DEBUGPRINTL <字符串>`
- `<字符串>`：普通字符串（无 FORM 展开）。输出到调试控制台并换行。
```erb
DEBUGPRINTL 现在开始检查变量
DEBUGPRINTL X = 以上字符串原样输出，花括号{0}也不展开
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:373`（`new DEBUGPRINT_Instruction(false, true)`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:260`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:541`（与 `DEBUGPRINT` 共用 `DEBUGPRINT_Instruction` 类，构造参数 `form=false, newline=true`）；实际输出在 `UI/Game/EmueraConsole.cs:2056`（`DebugPrint()`）、`UI/Game/EmueraConsole.cs:2068`（`DebugNewLine()`）

```text
指令类 DEBUGPRINT_Instruction(form=false, newline=true):
    参数构造器 = STR_NULLABLE（普通字符串，可省略）
    flag = METHOD_SAFE | EXTENDED | DEBUG_FUNC | PRINT_NEWLINE

解析期:
    非调试模式下跳过参数解析

执行期 DoInstruction:
    若 func.Argument.IsConst: str = 常量串
    否则:                    str = 表达式项求值得到的字符串
    exm.Console.DebugPrint(str)
    若 func.Function.IsNewLine():   # 本变体注册时 newline=true（flag 含 PRINT_NEWLINE）
        exm.Console.DebugNewLine()

DebugPrint(str):   非调试模式 return；否则 dConsoleLog.Append(str)
DebugNewLine():    非调试模式 return；否则 dConsoleLog.Append(Environment.NewLine)
```

## 备注

- 与 `DEBUGPRINT` 的唯一区别是构造参数 `newline=true`（flag 加上 `PRINT_NEWLINE`，执行后追加换行）。
- 参数为 NULLABLE，可无参调用（仅输出一个换行）。
- zh 套件无独立小节，`Debug_Mode.md` 一句话带过，语义与 ecd 一致。
