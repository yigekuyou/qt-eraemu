# ASSERT

- **类别**：命令（DEBUG 系）
- **签名**：
  - `ASSERT <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「DEBUG系」小节；`Era-Chinese-Documentation` 的 `Debug_Command.md` 未收录本命令（grep 无命中）。

## 语义

参数为真（非 0）时什么都不做。参数为假（0）时输出错误并停止脚本执行（抛出 CodeEE）。

属于 DEBUG 系指令：与 `DEBUGPRINT` 系一样，只在调试模式下动作，非调试模式下什么都不做。用于在 ERB 中写「此处应当为真」的自检断言。

## 用法

### `ASSERT <数值表达式>`
- `<数值表达式>`：断言条件；非 0 视为通过，0 视为失败。
```erb
;确保角色已登录，否则报错停止
ASSERT CHARANUM > 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:377`（`argb[FunctionArgType.INT_EXPRESSION]`，`METHOD_SAFE | EXTENDED | DEBUG_FUNC`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:747`（`case FunctionCode.ASSERT` 分支）

```text
case ASSERT:
    若 ((ExpressionArgument)func.Argument).Term 求值 == 0:
        抛出 CodeEE（AssertArgIs0，"ASSERT 的参数为 0"）
    否则: 什么都不做
```

## 备注

- 文档说「DEBUG 系指令只在调试模式下动作，非调试模式下什么都不做」。源码 switch 分支本身无调试模式判断，该行为由注册时的 `DEBUG_FUNC` 标志在解析/执行层实现（非调试模式下此类指令不参与执行），与本仓库 `Runtime/Script/Statements/Instraction.Child.cs` 等处的 `DEBUG_FUNC` 旗标机制对应；如需确认屏蔽点，可跟进 `FunctionIdentifier`/解析器对该旗标的处理。
- 报错形式是抛出 CodeEE（与其他运行错误一致，会输出错误信息并中止脚本），文档「输出错误并停止脚本执行」与此相符。
- zh 套件未收录本命令。
