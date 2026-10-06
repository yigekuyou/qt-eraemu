# PRINT_EXP

- **类别**：命令
- **签名**：PRINT_EXP <角色编号>
- **文档来源**：`ecd/docs/translation/Command.md`（ecd 命令详解无独立小节）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「显示训练专用的数据」小节有简要说明

## 语义

显示指定角色的经验一览：遍历该角色的 `EXP` 数组，对每个非 0 且对应 `EXPNAME` 非空的元素，输出形如「经验名数值 」的片段，拼成一行后输出并**换行**。与 PRINT_ABL 的区别是数值后**不带**「LV」字样。角色编号越界时抛出运行时错误。受 `SKIPDISP` 影响。

## 用法

### PRINT_EXP <角色编号>

- `<角色编号>`：数值表达式（INT_EXPRESSION），为目标角色的登记编号（0 起）。
- 输出格式：`<EXPNAME[i]><EXP:i>`（以空格分隔各项），最后换行。

```erb
PRINT_EXP 0
; 例：输出「绝顶经验12 精液经验5 …」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:183`（`addFunction(FunctionCode.PRINT_EXP, argb[INT_EXPRESSION], METHOD_SAFE)`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:64`
- 实现：`Runtime/Script/Process.ScriptProc.cs:176-186`（与 PRINT_ABL/PRINT_TALENT/PRINT_MARK 共用分支）；字符串拼装在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:906`（`GetCharacterDataString` 的 `PRINT_EXP` case）

```text
执行（Process.ScriptProc.cs 共用分支）：
    若 skipPrint → break
    target = 参数 Term.GetIntValue(exm)
    str = vEvaluator.GetCharacterDataString(target, FunctionCode.PRINT_EXP)：
        若 target < 0 或 target >= varData.CharacterList.Count
            → 抛 CodeEE（角色编号越界）
        chara = varData.CharacterList[target]
        array     = chara.EXP 的整数数组
        arrayName = 常量 CSV 中 EXPNAME 的名称列表
        对 i = 0 .. array.Length-1：
            若 i >= arrayName.Length → break
            若 array[i] == 0 → continue          ← 值为 0 的经验不显示
            若 arrayName[i] 为空 → continue
            builder += arrayName[i] + array[i] + ' '   ← 注意：无 "LV" 后缀
        返回 builder
    Console.Print(str)
    Console.NewLine()
```

## 备注

- ecd 命令详解（Command.md）未收录独立小节；zh 文档仅在 ERB_File_Format.md 中说明「显示角色的经验，需指定角色编号」。
- 文档未写明、由源码确认的细节：值为 0 的经验不显示；数值直接跟在名称后，中间没有分隔符（也不带 LV）。
