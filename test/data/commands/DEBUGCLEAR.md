# DEBUGCLEAR

- **类别**：命令（DEBUG 系，调试模式专用）
- **签名**：
  - `DEBUGCLEAR`
- **文档来源**：`ecd` 套件的 `Command.md` **没有本命令的独立小节**（DEBUG 系小节只列了 DEBUGPRINT 四变体）；语义取自 `ecd/Debug_Mode.md`（「DebugClear指令清除调试窗口的所有字符。没有参数」）。zh 套件 `Debug_Mode.md` 有相同表述。

## 语义

清除调试控制台（调试窗口）中的全部字符。无参数。

本命令只在以调试模式（`-Debug` 参数）启动 Emuera 时才实际动作；非调试模式下什么都不做，且**参数解析也被跳过**（解析器对 DEBUG 系指令在非调试模式下不解析参数，因此写在参数里的错误表达式也不会报错）。它是 DEBUG 系中唯一清除调试窗口的指令，也可以在脚本中主动调用以整理调试输出。

## 用法

### `DEBUGCLEAR`
- 无参数。
```erb
DEBUGPRINTFORML 当前 CHARANUM = {CHARANUM}
DEBUGCLEAR ;清空调试窗口，上面一行输出被抹掉
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:376`（`new DEBUGCLEAR_Instruction()`，枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:263`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:566`（`DEBUGCLEAR_Instruction`）；实际清除在 `UI/Game/EmueraConsole.cs:2063`（`DebugClear()`）

```text
指令类 DEBUGCLEAR_Instruction:
    参数构造器 = VOID（不允许任何参数）
    flag = METHOD_SAFE | EXTENDED | DEBUG_FUNC

解析期（ArgumentParser.SetArgument）:
    若 非调试模式 且 指令带 DEBUG_FUNC 标志:
        不做参数解析，Argument 置 null（错误表达式也不会暴露）

执行期 DoInstruction:
    exm.Console.DebugClear()

DebugClear():
    dConsoleLog.Remove(0, dConsoleLog.Length)   # 清空调试窗口日志缓冲
```

## 备注

- 与 DEBUGPRINT 系不同，`DebugClear()` 本身没有再判 `Program.DebugMode`：非调试模式下本指令根本不会执行到 `DoInstruction`（解析期即被短路成空指令、执行期跳过），效果等同「什么都不做」。
- ecd `Command.md` 的「DEBUG系」小节未收录本命令，需到 `ecd/Debug_Mode.md` 才能查到语义；zh 套件在 `Debug_Mode.md` 有对应描述，两者一致。
- 文档称调试窗口可用 `Ctrl+R` 从主控制台刷新，`DEBUGCLEAR` 则清空全部字符——与源码仅操作 `dConsoleLog` 一致（不影响主控制台）。
