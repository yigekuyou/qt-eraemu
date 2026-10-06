# CVARSET

- **类别**：命令
- **签名**：`CVARSET <角色变量>{, <数值表达式>, <表达式>, <初始角色编号>, <终止角色编号+1>}`
- **文档来源**：`ecd/docs/translation/Command.md`「CVARSET」小节；`Era-Chinese-Documentation` 未收录本命令

## 语义

将某个角色变量在指定角色范围内的指定元素统一赋值。即把第 2 参数指定元素号的 `变量:角色编号:元素` 在第 4～5 参数指定范围（省略时为全部角色）内的每个登录角色都赋成第 3 参数的值。等同于 `REPEAT CHARANUM ... REND` 循环赋值，但效率远高于脚本循环。第 1 参数必须是角色变量（`IsCharacterData`），否则报错；字符串变量填字符串、数值变量填数值。

## 用法

### `CVARSET <角色变量>`
- 只写变量名：全部角色的 0 号元素被赋默认值（数值 0 或空字符串）。

### `CVARSET <角色变量>, <元素编号>`
- 第 2 参数：角色变量的元素号，默认 0；对 `NAME`、`ISASSI` 等一元角色变量该参数被忽略。

### `CVARSET <角色变量>, <元素编号>, <填充值>`
- 第 3 参数：要赋的值，省略时为 0 或空字符串（字符串变量为空串）。

### `CVARSET <角色变量>, <元素编号>, <填充值>, <初始角色编号>{, <终止角色编号+1>}`
- 第 4、5 参数：限定赋值的登录角色范围 `[start, end)`；`start > end` 时自动交换。
```erb
CVARSET CFLAG, 10, 123
;等同于 REPEAT CHARANUM 内 CFLAG:COUNT:10 = 123
CVARSET CSTR, 0, "", 0, 5     ; 只给 0～4 号角色的 CSTR:0 赋空串
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:318` → `new CVARSET_Instruction()`（ArgBuilder = SP_CVAR_SET，flag = METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1706`（`CVARSET_Instruction`）

```text
DoInstruction(exm, func, state):
    arg = (SpCVarSetArgument)func.Argument
    p     = arg.VariableDest.GetFixedVariableTerm(exm)   // 第 1 参数解析出的角色变量（含默认下标）
    index = arg.Index.GetValue(exm)                      // 第 2 参数：元素号
    charaNum = (int)VEvaluator.CHARANUM
    start = 0
    若 arg.Start != null:
        start = (int)arg.Start.GetIntValue(exm)
        若 start < 0 或 start >= charaNum: 抛出 CodeEE("CVARSET第4参数越界")
    若 arg.End != null:
        end = (int)arg.End.GetIntValue(exm)
        若 end < 0 或 end > charaNum: 抛出 CodeEE("CVARSET第5参数越界")   // 注意：报错文本中用的是 start 的值（源码小瑕疵）
    否则 end = charaNum
    若 start > end: 交换 start、end
    若 !p.Identifier.IsCharacterData: 抛出 CodeEE("CVARSET的参数不是角色变量")
    若 index 是字符串 且 变量是一元数组（如 NAME 等以 CSV key 索引）:
        若 该 key 未在 ConstantData 中定义: 抛出 CodeEE("变量中不存在指定的键")
    若 p.Identifier.IsString:
        src = arg.Term.GetStrValue(exm)
        VEvaluator.SetValueAllEachChara(p, index, src, start, end)   // 字符串版本批量赋值
    否则:
        src = arg.Term.GetIntValue(exm)
        VEvaluator.SetValueAllEachChara(p, index, src, start, end)   // 数值版本批量赋值
```

## 备注

- 与 `VARSET` 的区别：`VARSET` 是"对**一个角色**的整个数组（或指定索引范围）赋值"，`CVARSET` 是"对**一组角色**的同一个元素赋值"。
- 源码第 5 参数越界报错信息中误用了 `start` 的值（`trerror.OoRCvarsetArg.Text, "5", start.ToString()`），属实现小瑕疵，仅影响报错文本。
- ecd 文档示例与源码语义一致；zh 文档套件未收录该命令。
