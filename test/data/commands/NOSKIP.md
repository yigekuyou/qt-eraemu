# NOSKIP

- **类别**：命令
- **签名**：
  - `NOSKIP`
- **文档来源**：`ecd/docs/translation/Command.md`「SKIPDISP、NOSKIP、ISSKIP、MOUSESKIP」小节（`### NOSKIP`）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

与 `ENDNOSKIP` 成对使用，指定一个「忽略显示忽略开关」的区间。被 `NOSKIP`～`ENDNOSKIP` 包围的区间，即使处于 `SKIPDISP 1`（忽略 `PRINT` 等输出）状态也会正常显示。主要用于区间内需要 `INPUT`／`INPUTS` 等输入处理的场合。

该指令不改变 `SKIPDISP` 本身的状态（执行前后 `ISSKIP()` 的取值不受 `NOSKIP` 区间影响），因此在可能打开 `SKIPDISP` 开关的代码（例如有显示/不显示两种分支的口上代码）中，用它就能确保必须显示的地方正常显示。当前是否处于忽略状态可用 `ISSKIP()` 获取。

从 ver1.808 起，`NOSKIP` 放在 `SIF` 语句之后也能正常工作。

## 用法

### `NOSKIP`
- 无参数。与 `ENDNOSKIP` 成对出现；`NOSKIP` 开始区间，`ENDNOSKIP` 结束区间。缺少对应的 `ENDNOSKIP` 时报错。
```erb
SKIPDISP 1
;此处处于显示忽略状态
NOSKIP
PRINTL 这一段即使 SKIPDISP 1 也会显示
INPUT
ENDNOSKIP
;恢复显示忽略状态
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:326`（`argb[FunctionArgType.VOID]`，`METHOD_SAFE | EXTENDED | PARTIAL`）；`funcMatch[FunctionCode.NOSKIP] = "ENDNOSKIP"`（同文件 485 行）
- 实现：`Runtime/Script/Process.ScriptProc.cs:581`（`case FunctionCode.NOSKIP` 分支，`doNormalFunction` 内）；`ENDNOSKIP` 在同文件 590 行

```text
case NOSKIP:
    若 func.JumpTo == null:              # 解析期没找到配对的 ENDNOSKIP
        抛出 CodeEE（"缺少 ENDNOSKIP"）
    saveSkip = skipPrint                 # 记住进入区间前的忽略状态
    若 skipPrint:
        skipPrint = false                # 区间内强制显示

case ENDNOSKIP:
    若 func.JumpTo == null:              # 没找到配对的 NOSKIP
        抛出 CodeEE（"缺少 NOSKIP"）
    若 saveSkip:
        skipPrint = true                 # 恢复进入区间前的忽略状态
```

`skipPrint` 是 `ScriptProc` 的输出开关：为真时所有 `PRINT` 系指令（包括 `PRINTBUTTON` 等）直接跳过输出；`SKIPDISP` 指令（`Runtime/Script/Process.ScriptProc.cs:573`）设置它，并顺带把 `RESULT:0` 重置为 0。

## 备注

- ecd 文档提到「执行 `SKIPDISP` 后，无论参数如何都会把 `RESULT:0` 重置为 0，这是规格」——与源码 `Runtime/Script/Process.ScriptProc.cs:578`（`vEvaluator.RESULT = skipPrint ? 1L : 0L`）一致。
- `NOSKIP`/`ENDNOSKIP` 标记为 `PARTIAL`（部分指令），即不能作为 `SIF` 的从句（但 ecd 文档说明 ver1.808 后 `SIF` 之后的 `NOSKIP` 能正常工作，解析期仅告警不阻断）。
- 省略参数不存在（无参数指令）；错误行为：区间不配对时在执行到该行时抛 `CodeEE`。
- zh 套件未收录本命令。
