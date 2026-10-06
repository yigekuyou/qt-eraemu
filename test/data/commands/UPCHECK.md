# UPCHECK

- **类别**：命令
- **签名**：`UPCHECK`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节；语义散见于 `ecd/ERB_File_Format.md`（训练专用命令一览）、`ecd/Command.md` 的 `CUPCHECK` 小节（"UPCHECK 会显示结果"）、`ecd/Variable.md` 的 UP/DOWN 表格与 `ecd/Flow.md` 的 TRAIN 流程说明；`Era-Chinese-Documentation/docs/`（zh/Command.md）同样未收录独立小节，仅 `zh/ERB_File_Format.md`、`zh/Flow.md`、`zh/Variable.md` 有相同描述。

## 语义

训练（TRAIN）专用指令。把 `UP`、`DOWN` 数组中累计的增减量应用到 `TARGET` 角色的 `PALAM` 数组上：对每个元素执行 `PALAM:i += UP:i - DOWN:i`。应用前若该元素有变化（UP 或 DOWN 大于 0），会打印一行形如 `パラメタ名 当前值+增加量-减少量=结果值` 的变化报告。处理结束后无论是否成功应用，`UP` 与 `DOWN` 全部元素都会被清零。`UP`、`DOWN` 的元素可通过 `PALAMNAME` 以名字索引。与 `CUPCHECK`（针对任意角色的 `CUP`/`CDOWN` 版本，不显示结果）相对。`PRINT_PALAM` 显示的是 `PALAM` 当前值，而 `UPCHECK` 显示并应用的是本回合的参数变动。

## 用法

### UPCHECK
无参数。作用于 `TARGET` 指定的角色与全局 `UP`/`DOWN` 数组。

```erb
;训练命令处理结束时结算参数变化（典型流程）
UPCHECK
;输出示例：
;快楽 1500+300=1800
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:209`（`argb[FunctionArgType.VOID], METHOD_SAFE`，注释"パラメータの変動"）
- 实现：`Runtime/Script/Process.ScriptProc.cs:248`（switch-case）→ `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1552`（`VariableEvaluator.UpdateInUpcheck`）

```text
函数 UpdateInUpcheck(window, skipPrint):
    paramname <- PALAMNAME 的 CSV 名字表
    up   <- 全局 UP 数组
    down <- 全局 DOWN 数组
    target <- TARGET
    若 target 越界（<0 或 >= 角色数）: 跳到 end（不应用任何值，只清零）
    chara <- 角色列表[target]
    param <- chara 的 PALAM 数组
    length <- min(param.Length, up.Length, down.Length)
    对 i = 0 .. length-1:
        若 up[i] <= 0 且 down[i] <= 0: continue   // 本家规格：非正的增减量无效
        builder <- ""
        若 !skipPrint:
            builder <- paramname[i] + " " + param[i]
            若 up[i] > 0:  builder += "+" + up[i]
            若 down[i] > 0: builder += "-" + down[i]
        param[i] += up[i] - down[i]   // unchecked 溢出不报错
        若 !skipPrint:
            builder += "=" + param[i]
            window.Print(builder); window.NewLine()
    end:
    将 up 的全部元素清 0
    将 down 的全部元素清 0
```

调用处的 `skipPrint` 来自系统文本跳过状态（`Runtime/Script/Process.ScriptProc.cs` 中 `skipPrint && func.Function.IsPrint()` 同一机制）：跳过显示时只结算数值、不打印报告。

## 备注

- ecd 与 zh 两套文档均未给 UPCHECK 独立小节，本档语义由 `CUPCHECK` 小节、`ERB_File_Format.md`、`Flow.md`、`Variable.md` 的多处描述拼合；源码与这些描述一致（应用增减、打印报告、清零 UP/DOWN）。
- 文档（zh/Flow.md）说"UP 和 DOWN 的值都被分配为 0"，与源码一致；且源码表明即使 `TARGET` 无效也会清零。
- 源码细节文档未提及：变化量为 0 或负的元素不打印也不应用（`up[i] <= 0 && down[i] <= 0` 直接跳过）；溢出按 unchecked 处理不报错。
