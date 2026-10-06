# DEBUGPRINTFORM

- **类别**：命令（DEBUG 系，调试模式专用）
- **签名**：
  - `DEBUGPRINTFORM <FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「DEBUG系」小节（「DEBUGPRINT 的 FORM 格式版，先展开 FORM 语法再输出」）；zh 套件 `Debug_Mode.md`（与 Print 系语句相同、仅输出目标为调试窗口）。

## 语义

`DEBUGPRINT` 的 FORM 格式版：参数先按 `PRINTFORM` 的规则展开（`{数值表达式}`、`%字符串表达式%`、`\@…#…\@` 等），再把结果输出到调试控制台，输出后不换行。

只在调试模式下动作；非调试模式下什么都不做，也**不解析参数**，因此 FORM 格式串即使写错也不会报错（ecd/zh 文档都特别强调这一点）。与 PRINT 系一样没有对应的 `n` 后缀变体（本命令族无 `DEBUGPRINTFORMN`）。

## 用法

### `DEBUGPRINTFORM <FORM格式文本>`
- `<FORM格式文本>`：支持 FORM 语法的字符串，先展开再输出到调试控制台，不换行。
```erb
A = 3
DEBUGPRINTFORM X = {A * 2}, NAME = %NAME:0%\n ;注意：这里写 \n 不会换行
DEBUGPRINTL ;用换行版结束这一行
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:374`（`new DEBUGPRINT_Instruction(true, false)`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:261`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:541`（与 `DEBUGPRINT` 共用 `DEBUGPRINT_Instruction` 类，构造参数 `form=true, newline=false`）；实际输出在 `UI/Game/EmueraConsole.cs:2056`（`DebugPrint()`）

```text
指令类 DEBUGPRINT_Instruction(form=true, newline=false):
    参数构造器 = FORM_STR_NULLABLE（FORM 格式串，可省略）
    flag = METHOD_SAFE | EXTENDED | DEBUG_FUNC

解析期:
    非调试模式下跳过参数解析 → FORM 串中的错误在非调试模式不会暴露

执行期 DoInstruction:
    若 func.Argument.IsConst: str = 常量串（解析期已展开 FORM）
    否则:                    str = 表达式项求值（内部完成 FORM 展开）
    exm.Console.DebugPrint(str)
    若 func.Function.IsNewLine():  # 本命令为 false
        exm.Console.DebugNewLine()

DebugPrint(str):
    若 !Program.DebugMode: return
    dConsoleLog.Append(str)
```

## 备注

- 与 `DEBUGPRINT` 唯一区别是构造参数 `form=true`（参数构造器换成 `FORM_STR_NULLABLE`）。
- 「非调试模式不解析参数」由 `Runtime/Script/Statements/ArgumentParser.cs:17` 与 `Runtime/Script/Process.ScriptProc.cs:36` 两处对 `IsDebug()` 的检查保证，与 ecd、zh 两套文档的说法一致。
- 参数为 NULLABLE，可无参调用。
