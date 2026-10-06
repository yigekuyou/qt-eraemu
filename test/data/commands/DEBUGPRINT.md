# DEBUGPRINT

- **类别**：命令（DEBUG 系，调试模式专用）
- **签名**：
  - `DEBUGPRINT <字符串>`
- **文档来源**：`ecd/docs/translation/Command.md`「DEBUG系」小节；zh 套件 `Debug_Mode.md`（「DebugPrint……的功能与Print语句相同，只是输出到调试窗口」）。

## 语义

只在以调试模式启动时动作，把字符串输出到**调试控制台**（不是主控制台）。非调试模式下什么都不做，也不会解析参数，因此即使参数是格式串写错的表达式也不会出错。

与 `PRINT` 基本相同，区别在于：输出目标是调试控制台而非主控制台；不受 `SKIPDISP` 指令影响；不能使用 `PRINT` 的 `n` 后缀（本命令族没有对应的 `N` 变体，如 `DEBUGPRINTN`；需要换行时用 `DEBUGPRINTL`）。它是 `DEBUGPRINT`/`DEBUGPRINTL`/`DEBUGPRINTFORM`/`DEBUGPRINTFORML` 四兄弟中最基本的形式，输出后不换行。

## 用法

### `DEBUGPRINT <字符串>`
- `<字符串>`：普通字符串（不能用 FORM 格式）。输出到调试控制台，不换行。
```erb
DEBUGPRINT 调试开始：X=
DEBUGPRINTL X ;两行拼接后换行
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:372`（`new DEBUGPRINT_Instruction(false, false)`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:259`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:541`（`DEBUGPRINT_Instruction`，构造参数 `form=false, newline=false`）；实际输出在 `UI/Game/EmueraConsole.cs:2056`（`DebugPrint()`）

```text
指令类 DEBUGPRINT_Instruction(form=false, newline=false):
    参数构造器 = STR_NULLABLE（非 FORM 的字符串，可省略）
    flag = METHOD_SAFE | EXTENDED | DEBUG_FUNC

解析期:
    非调试模式下跳过参数解析（同 DEBUGCLEAR）

执行期 DoInstruction:
    若 func.Argument.IsConst: str = 常量串
    否则:                    str = 表达式项求值得到的字符串
    exm.Console.DebugPrint(str)
    若 func.Function.IsNewLine():          # 本命令为 false，不换行
        exm.Console.DebugNewLine()

DebugPrint(str):
    若 !Program.DebugMode: return          # 非调试模式直接丢弃
    dConsoleLog.Append(str)                # 追加到调试窗口缓冲
```

## 备注

- 四个 DEBUGPRINT 变体共用同一个指令类：`DEBUGPRINT(false,false)`、`DEBUGPRINTL(false,true)`、`DEBUGPRINTFORM(true,false)`、`DEBUGPRINTFORML(true,true)`；`form` 决定参数构造器（`STR_NULLABLE` / `FORM_STR_NULLABLE`），`newline` 决定 flag 中的 `PRINT_NEWLINE` 及执行后是否调用 `DebugNewLine()`。
- 源码中换行判断是 `func.Function.IsNewLine()`（读取 flag 中的 `PRINT_NEWLINE`，本命令族的 L 变体在注册时由构造参数 `newline=true` 置位），与文档「DEBUGPRINT 的换行版」一致。
- 参数类型是 NULLABLE：`DEBUGPRINT` 不带参数也能通过解析（什么也不输出）。
- zh 套件无独立小节，`Debug_Mode.md` 用一句话概括四个变体，与 ecd 一致；zh 文档中写的 `DebugPrintFormL` 名称拼写与实际命令名 `DEBUGPRINTFORML` 略有差异（L 的位置），以 ecd/源码为准。
