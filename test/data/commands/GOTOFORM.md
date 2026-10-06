# GOTOFORM

- **类别**：命令（EE 扩展命令）
- **签名**：`GOTOFORM <FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系」`### GOTOFORM` 小节；`Era-Chinese-Documentation`（zh 套件）未收录该命令

## 语义

与 `GOTO` 相同，但可以像 `PRINTFORM` 那样用带 FORM 格式（`{表达式}`、`%字符串表达式%`、`@式中文本@`）的字符串指定要跳转的 `$` 标签名，实现运行期决定跳转目标。目标标签仍必须在当前函数内；标签不存在时报错（容错版为 `TRYGOTOFORM`，TRYC 版为 `TRYCGOTOFORM`，均可省 CATCH 与否见各自条目）。用 `GOTOFORM` 直接跳入循环、分支语法内时的行为与 `GOTO`／`TRYGOTO` 相同，参阅「循环·分支语法」相关章节。

## 用法

### GOTOFORM <FORM格式文本>
- `<FORM格式文本>`：FORM 格式字符串，求值结果为当前函数内的 `$` 标签名（不含 `$`）。

```erb
IF A > 0
  GOTOFORM LABEL_{A}
ELSE
  GOTOFORM LABEL_DEFAULT
ENDIF
;A=5 时跳到 $LABEL_5

$LABEL_5
PRINTL 在 LABEL_5
$LABEL_DEFAULT
PRINTL 兜底标签
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:358`（`new GOTO_Instruction(true, false, false)`，flag = METHOD_SAFE | FLOW_CONTROL | FORCE_SETARG；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:148`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3746`（`GOTO_Instruction`，form=true 使参数构建器改用 `FunctionArgType.SP_CALLFORM`；isTry=false、isTryCatch=false）

```text
# GOTO_Instruction.DoInstruction（form=true 路径）
label ← ((SpCallArgment)func.Argument).FuncnameTerm.GetStrValue(exm)
        # FORM 字符串在运行期求值，得到标签名
jumpto ← state.CurrentCalled.CallLabel(当前进程, label)
        # 只在当前正在执行的函数内查找 $ 标签

若 jumpto == null:
    因 isTry == false: throw CodeEE("标签 {label} 未定义")
否则若 jumpto.IsError: throw CodeEE("标签 {label} 不合法")
否则: state.JumpTo(jumpto)

# 与 GOTO(常量) 的区别：FORM 文本含 {}/%…% 插值时不是编译期常量
# （只有不含插值的纯字面量 FORM 文本才会退化为常量、走 SetJumpTo 的静态绑定），
# 此时无法在静态解析期绑定 JumpTo，每次执行都要经 CallLabel 动态查找。
```

## 备注

- ecd 文档签名仅 `GOTOFORM <FORM格式文本>`，未提及参数；源码中 GOTO_Instruction 采用 `SP_CALLFORM` 参数构建器，技术上 FORM 串之外还可携带子名/实参语法（`SpCallArgment.SubNames/RowArgs`），但 DoInstruction 只使用 `FuncnameTerm`，多余部分被忽略；`TRYGOTOLIST` 的 FUNC 校验甚至禁止它们。本文按文档只列标签名一种用法。
- 跳转目标限于当前函数：由 `CallLabel` 的查找范围保证，与 ecd 文档"与 GOTO 相同"一致。
- 其余无冲突。
