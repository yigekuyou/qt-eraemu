# PRINT_ABL

- **类别**：命令
- **签名**：PRINT_ABL <角色编号>
- **文档来源**：`ecd/docs/translation/Command.md`（ecd 命令详解无独立小节）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「显示训练专用的数据」小节有简要说明

## 语义

显示指定角色登记编号对应的能力一览：遍历该角色的 `ABL` 数组，对每个非 0 且对应 `ABLNAME` 非空的元素，输出形如「能力名LV等级 」的片段，全部拼成一行后输出并**换行**。值会取 CSV 中的 `ABLNAME` 作为名称；等级为 0 的能力不显示。角色编号越界（<0 或 ≥ 当前角色数）时抛出运行时错误。受 `SKIPDISP` 影响。

## 用法

### PRINT_ABL <角色编号>

- `<角色编号>`：数值表达式（INT_EXPRESSION），为目标角色在 `CHARA` 列表中的登记编号（0 起）。
- 输出格式：`<ABLNAME[i]>LV<ABL:i>`（以空格分隔各能力），最后换行。
- 无返回值。

```erb
PRINT_ABL 0
; 例：输出「体力LV3 气力LV5 …」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:180`（`addFunction(FunctionCode.PRINT_ABL, argb[INT_EXPRESSION], METHOD_SAFE)`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:61`
- 实现：`Runtime/Script/Process.ScriptProc.cs:176-186`（PRINT_ABL/PRINT_TALENT/PRINT_MARK/PRINT_EXP 共用分支）；字符串拼装在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:906`（`GetCharacterDataString`）

```text
执行（Process.ScriptProc.cs 共用分支）：
    若 skipPrint → break
    target = 参数 Term.GetIntValue(exm)
    str = vEvaluator.GetCharacterDataString(target, FunctionCode.PRINT_ABL)：
        若 target < 0 或 target >= varData.CharacterList.Count
            → 抛 CodeEE（角色编号越界）
        chara = varData.CharacterList[target]
        array     = chara.ABL 的整数数组
        arrayName = 常量 CSV 中 ABLNAME 的名称列表
        对 i = 0 .. array.Length-1：
            若 i >= arrayName.Length → break
            若 array[i] == 0 → continue          ← 等级 0 不显示
            若 arrayName[i] 为空 → continue
            builder += arrayName[i] + "LV" + array[i] + ' '
        返回 builder
    Console.Print(str)
    Console.NewLine()                            ← 输出后换行
```

## 备注

- ecd 命令详解（Command.md）未收录独立小节；zh 文档仅在 ERB_File_Format.md 中说明「显示角色的能力，需指定角色编号」。
- zh/Difference.md 提到：在 Eramaker 中该数值会引用 `ABL:99999` 并报错，属于两代实现的兼容性差异。
- 文档称显示「能力」，与源码一致：名称取 `ABLNAME`，数值取角色 `ABL`，只显示非 0 项——这是文档未写明的细节。
