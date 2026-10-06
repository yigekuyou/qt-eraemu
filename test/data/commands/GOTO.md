# GOTO

- **类别**：命令
- **签名**：`GOTO <标签名>`（标签即 `$LABEL` 形式；TRY 系变体 `TRYGOTO <标签名>`、FORM 版 `GOTOFORM <FORM格式文本>` 等见各独立条目）
- **文档来源**：ecd 文档**没有** `### GOTO` 独立小节，语义散见于「CALL·JUMP·GOTO系」「循环·分支语法」各处（如 `### TRYGOTO`、`### GOTOFORM` 均以"与 GOTO 相同"描述）；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

跳转到**当前函数内**定义的 `$` 标签处继续执行。`GOTO` 不能跨越函数边界（目标标签必须位于当前被调用的函数内），跳到不存在的标签会报错。直接跳入 `IF`/`FOR` 等结构内部时的行为：跳入 `IF`～`ENDIF` 内会像正常流一样执行到最近的 `ELSEIF`/`ELSE`/`ENDIF` 为止再跳到 `ENDIF` 之后；跳入 `FOR`～`NEXT`（`REPEAT`～`REND`）内会执行到 `NEXT` 前结束循环；跳入 `WHILE`～`WEND`、`DO`～`LOOP` 内则像通常一样回跳；跳入 `SELECTCASE` 内执行到下一个 `CASE`/`CASEELSE`/`ENDSELECT` 为止。TRY 系变体（`TRYGOTO`/`TRYCGOTO`）在标签不存在时不报错（TRYC 版可被 `CATCH` 捕获）。

## 用法

### GOTO <标签名>
- `<标签名>`：当前函数内以 `$` 定义的标签名（不带 `$` 前缀书写）。

```erb
PRINTL LOOP START
GOTO INPUT_LOOP
PRINTL 这里不会被执行
$INPUT_LOOP
PRINTL 跳转目标
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:356`（`new GOTO_Instruction(false, false, false)`，flag = METHOD_SAFE | FLOW_CONTROL | FORCE_SETARG；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:97`。`TRYGOTO`=(false,true,false) :357、`GOTOFORM`=(true,false,false) :358、`TRYCGOTO`=(false,true,true) :360 等）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3746`（`GOTO_Instruction`，全家族共用，构造参数 form/isTry/isTryCatch 区分）

```text
# 静态解析期（SetJumpTo，仅当参数为常量字符串时优化）
若 func.Argument 是常量:
    jumpto ← LabelDictionary.GetLabelDollar(标签名, 当前函数标签行)
    若 jumpto == null:
        若本指令不是 TRY 系: 告警 "标签未定义"（非 TRY 仍会在运行期报错）
        否则: 保留 null（运行期处理）
    否则若 jumpto.IsError: 告警 "标签名不合法"
    否则: func.JumpTo ← jumpto          # 常量标签直接绑定，免去运行期查找

# 运行期（DoInstruction）
若 func.Argument 是常量:
    label ← 常量字符串
    jumpto ← func.JumpTo
    若 jumpto == null: return           # 静态期已确定不存在且是 TRY 系 → 什么都不做
否则:                                    # GOTOFORM / 变量标签
    label ← 求值 FuncnameTerm
    jumpto ← state.CurrentCalled.CallLabel(当前进程, label)
    # CallLabel 只在"当前正在执行的函数"内查找 $ 标签 → 天然不能跨函数

若 jumpto == null:
    若本指令不是 TRY 系: throw CodeEE("标签 {label} 未定义")
    若 func.JumpToEndCatch != null: state.JumpTo(func.JumpToEndCatch)   # TRYC 系进 CATCH
    return
否则若 jumpto.IsError: throw CodeEE("标签 {label} 不合法")
state.JumpTo(jumpto)                     # 执行跳转
```

## 备注

- ecd 文档未提供 GOTO 的独立小节与签名，本文语义由 ecd 的 `TRYGOTO`/`GOTOFORM` 小节（"与 GOTO 相同……"）及源码推定补全。
- `GOTO` 参数只能是常量标签或 FORM 字符串（`SP_CALL`/`SP_CALLFORM` 参数构建器），不支持运行期拼接普通字符串的变量标签——运行期求值分支只服务 GOTOFORM 家族（`SpCallArgment.FuncnameTerm`）。
- 跳入循环/分支结构内部的特殊行为由行结构（PARTIAL 标记与 JumpTo 关系）保证，ecd「循环·分支语法」一节有逐结构描述（见 :1666、:1678、:1692、:1724 行附近），与实现相符。
- 其余无冲突。
