# VARSET

- **类别**：命令
- **签名**：
  - `VARSET <变量名>`
  - `VARSET <变量名>, <数值表达式 or 字符串表达式>`
  - `VARSET <变量名>, <数值表达式 or 字符串表达式>, <初始索引>`
  - `VARSET <变量名>, <数值表达式 or 字符串表达式>, <初始索引>, <终止索引+1>`
- **文档来源**：`ecd/docs/translation/Command.md` → `### VARSET <变量名>{, <数值表达式 or 字符串表达式>, <初始索引>, <终止索引+1>}`；`Era-Chinese-Documentation/docs/`（zh/Command.md）未收录独立小节。

## 语义

把变量数组内指定范围的元素全部赋为同一个值。第 2 参数是填充值，省略时数值变量填 0、字符串变量填空字符串；第 3、4 参数指定填充范围 `[初始索引, 终止索引+1)`，省略时填充整个数组。处理角色变量时（如 `VARSET CFLAG:MASTER:0, 0`）只对指定的那个角色生效。对多维数组变量，第 3、4 参数被忽略，总是填充全部元素。与用 `FOR~NEXT` 循环逐个赋值相比性能好得多。

## 用法

### VARSET <变量名>{, <填充值>, <初始索引>, <终止索引+1>}

- `<变量名>`：目标数组变量。写作 `FLAG` 时指整个数组；写作 `CFLAG:MASTER:0` 这类带索引形式时按该索引定位具体角色/元素维。
- `<填充值>`：数值或字符串表达式，类型须与变量一致；省略时为 0 或 `""`。
- `<初始索引>` / `<终止索引+1>`：一维数组的填充范围，半开区间；省略时为整个数组。多维数组下被忽略。

```erb
VARSET FLAG, 0                ;FLAG 全部元素 = 0
VARSET STR, "啊啊啊", 0, 10   ;STR:0 ~ STR:9 = "啊啊啊"
VARSET TA:0:0:0, 5678         ;三维数组 TA 全部元素 = 5678
VARSET CFLAG:MASTER:0, 0      ;仅 MASTER 的 CFLAG 全部置 0
VARSET CSTR, ""               ;所有角色的 CSTR 置空串
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:317`（`new VARSET_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1664`（`VARSET_Instruction`）→ `Runtime/Script/Statements/Variable/VariableEvaluator.cs:87/:119`（`VariableEvaluator.SetValueAll`）

```text
函数 DoInstruction(exm, func, state):
    arg <- (SpVarSetArgument)func.Argument
    var <- arg.VariableDest
    p   <- var.GetFixedVariableTerm(exm)     // 解析变量名及已写明的索引（含角色变量目标）
    start <- 0; end <- 0
    若 arg.End != null:
        end <- arg.End 取整值
    否则若 var 是一维数组:
        end <- var.GetLength()               // 默认填满整个一维数组
    若 arg.Start != null:
        start <- arg.Start 取整值
        若 start > end: 交换 start 与 end    // 文档未记载：范围颠倒时自动交换
    若 var 是字符串型:
        src <- arg.Term 的字符串值
        SetValueAll(p, src, start, end)
    否则:
        src <- arg.Term 的整数值
        SetValueAll(p, src, start, end)

函数 SetValueAll(p, src, start, end):        // 数值/字符串两个重载逻辑相同
    若 p.Identifier.IsCalc: 直接返回（CALC 型只读变量，不赋值）
    若 p 是一维数组:
        若 start != 0 或 end != 数组长度:
            IsArrayRangeValid(start, end, "VARSET", 第3参数, 第4参数)  // 越界则抛 CodeEE
        否则若 p 是角色变量:
            CheckElement(已写出的角色索引)    // 校验角色编号有效
    否则若 p 是角色变量（多维）:
        CheckElement(已写出的全部索引)
    p.Identifier.SetValueAll(src, start, end, p.Index1)  // 对 [start, end) 逐元素赋值
```

## 备注

- 文档"对于多维数组变量，第 3、4 参数将被忽略"与源码相符：多维时 `end` 保持 0、`start` 默认 0，`Identifier.SetValueAll` 对多维变量填满全部元素。
- 源码中有一处文档未记载的行为：第 3、4 参数写反（start > end）时会自动交换而不是报错。
- `VARSET` 标记为 `METHOD_SAFE | EXTENDED`（EE 扩展命令）；字符串版对 `WINDOW_TITLE` 这类 CALC 型变量赋值会被静默忽略。
