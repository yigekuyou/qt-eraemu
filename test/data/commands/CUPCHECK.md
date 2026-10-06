# CUPCHECK

- **类别**：命令
- **签名**：`CUPCHECK <已登录角色编号>`
- **文档来源**：`ecd/docs/translation/Command.md`「CUPCHECK」小节；`Era-Chinese-Documentation/docs/Variable.md` 仅在 CUP/CDOWN 处提及（"使用 CUPCHECK 指令而不是 UPCHECK 指令"），无独立条目

## 语义

对参数指定的角色执行与 `CUP`、`CDOWN` 对应的 `UPCHECK`：遍历该角色的 `CUP`/`CDOWN` 数组，把每个参数的变动量（`CUP - CDOWN`）一次性累加到该角色的 `PALAM` 上，并把 `CUP`/`CDOWN` 清零。它直接操作指定角色的角色变量，与全局 `UP`/`DOWN` 无关，也不需要设置 `TARGET`。有效变动会按"参数名 当前值+增量-减量=结果"的格式逐行打印（受 `SKIPDISP` 影响）。角色编号不在已登录范围（<0 或 ≥CHARANUM）时静默返回，不报错也不做任何事。

## 用法

### `CUPCHECK <已登录角色编号>`
- 参数：要结算参数变动的角色登录编号（0 ～ CHARANUM-1）。
```erb
CUP:TARGET:2 += 1    ; 用 CUP:角色编号:参数编号 累积变动
CUPCHECK TARGET      ; 把 TARGET 的 CUP/CDOWN 结算进其 PALAM 并清零
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:210` → `argb[FunctionArgType.INT_EXPRESSION]`, METHOD_SAFE | EXTENDED
- 实现：`Runtime/Script/Process.ScriptProc.cs:251`（switch-case）；核心逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1608`（`VariableEvaluator.CUpdateInUpcheck`）

```text
case FunctionCode.CUPCHECK:
    target = ((ExpressionArgument)func.Argument).Term.GetIntValue(exm)
    vEvaluator.CUpdateInUpcheck(exm.Console, target, skipPrint)   // skipPrint 由 SKIPDISP 决定

CUpdateInUpcheck(window, target, skipPrint):
    若 target < 0 或 target >= 角色列表.Count: return    // 静默返回，不报错
    chara = 角色列表[target]
    up   = chara 的 CUP 数组
    down = chara 的 CDOWN 数组
    param= chara 的 PALAM 数组
    length = 三者中最小的长度
    对 i = 0 .. length-1:
        若 up[i] <= 0 且 down[i] <= 0: continue        // 沿袭本家仕様：负值/无变动无效
        若 !skipPrint:
            构造文本 "PALAMNAME[i] param[i]"，up[i]>0 追加 "+up[i]"，down[i]>0 追加 "-down[i]"
        param[i] += up[i] - down[i]                    // 不检查溢出（unchecked）
        若 !skipPrint:
            追加 "=param[i]"，window.Print(文本); window.NewLine()
    将 up、down 数组全部清零                            // 无论是否有变动都清零
```

## 备注

- **文档与源码差异**：ecd 文档称"`UPCHECK` 会显示结果，而 `CUPCHECK` 不会显示结果"，但源码中 `CUpdateInUpcheck` 与 `UpdateInUpcheck`（`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1552`）的打印逻辑完全相同，只要未 `SKIPDISP` 就会显示结算过程。此处以源码为准，两者仅差别在操作的是"指定角色的 CUP/CDOWN"还是"全局 UP/DOWN + TARGET"。
- 编号越界不抛错（与 `COPYCHARA` 等的严格检查不同），是静默无操作。
- ecd 文档称"当然不会受 UP、DOWN 的影响"，与源码一致：`CUpdateInUpcheck` 完全不读 UP/DOWN 数组。
