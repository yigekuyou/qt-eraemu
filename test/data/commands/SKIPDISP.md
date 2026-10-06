# SKIPDISP

- **类别**：命令
- **签名**：SKIPDISP `<数值>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### SKIPDISP <数值>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

设置「显示忽略开关」：参数为 `0` 时不忽略画面输出，为 `0` 以外的值时忽略。开关打开后，`PRINT` 等输出指令完全不进行画面输出。

在开关打开期间执行到 `INPUT`、`INPUTS` 时，用户无从得知该输入什么，且直接跳过很可能进入无限循环，因此会显示警告与处理方法并产生错误。若必须在此状态下输入，可用 `NOSKIP`～`ENDNOSKIP` 包围输入区间，或先 `SKIPDISP 0`、输入结束后再 `SKIPDISP 1`（推荐前者）。

典型用途：在不显示的状态下调用口上等可能改变行为的代码，使显示与不显示两种情形行为一致。当前是否处于忽略状态可用 `ISSKIP()` 获取。

从 ver1.808 起，`SKIPDISP` 放在 `SIF` 之后也能正常工作。

## 用法

### SKIPDISP `<数值>`

- `<数值>`：数值表达式。`0` = 不忽略输出；非 `0` = 忽略输出。
- 副作用：设置解释器的 `skipPrint` / `userDefinedSkip` 状态，并写入 `RESULT:0`。

```erb
SKIPDISP 1
;这里的 PRINT 不会显示
PRINTL 不可见
SKIPDISP 0
PRINTL 可见
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:324`（`argb[FunctionArgType.INT_EXPRESSION]`，METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Process.ScriptProc.cs:573`（`case FunctionCode.SKIPDISP`，Process.ScriptProc 内 switch 分发）

```text
case SKIPDISP:
    iValue = 参数为常量 ? ConstInt : 对参数项求值(GetIntValue)
    skipPrint       = (iValue != 0)   // 当前显示忽略开关
    userDefinedSkip = (iValue != 0)   // 用户定义的忽略状态（供 INPUT 等检查）
    VEvaluator.RESULT = skipPrint ? 1 : 0
```

## 备注

- **文档与源码不一致**：ecd 文档称「执行 SKIPDISP 后，无论参数如何都会把 `RESULT:0` 重置为 0，这是规格」；但本仓库源码把 `RESULT` 写为 `skipPrint ? 1 : 0`，即忽略时为 1、不忽略时为 0。以本仓库实现为准，两者已如实并列。
- `NOSKIP`／`ENDNOSKIP` 的实现（`Runtime/Script/Process.ScriptProc.cs:581` 起）依赖 `saveSkip`/`skipPrint` 字段：`NOSKIP` 保存并暂时关闭 `skipPrint`，`ENDNOSKIP` 恢复；两者缺失配对指令时抛 CodeEE。
- Era-Chinese-Documentation 套件未收录本命令。
