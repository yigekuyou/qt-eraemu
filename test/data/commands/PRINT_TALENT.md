# PRINT_TALENT

- **类别**：命令
- **签名**：`PRINT_TALENT <数值表达式（已登录角色编号）>`
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节；语义见 `Era-Chinese-Documentation/docs/ERB_File_Format.md`（「显示训练专用的数据」小节）与 `ecd/ERB_File_Format.md:861`（「`PRINT_TALENT`：显示角色的素质」）；实现细节来自源码。

## 语义

显示指定角色（第 1 参数为登录角色编号）的素质（`TALENT` 数组）。

对角色的每个 `TALENT` 槽位，若 `TALENT:角色:槽位` 的值非 0 且对应的 `TALENTNAME`（CSV 名称）非空，则输出形如 `[素质名]` 的文本（各素质之间无分隔符）；值为 0 或名称为空的槽位被跳过。全部输出完后换行。

与 PRINT_ABL / PRINT_MARK / PRINT_EXP 不同，TALENT 只显示名字（带方括号），不显示等级数值。角色编号越界时抛出运行时错误。

## 用法

### `PRINT_TALENT <角色编号>`
- `<角色编号>`：数值表达式，已登录角色的编号（0 起）。

```erb
PRINT_TALENT 0        ; 显示主角（0 号角色）的素质，如「[胆怯][体质差]」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:181`（`argb[FunctionArgType.INT_EXPRESSION], METHOD_SAFE`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:176-188`（switch-case `FunctionCode.PRINT_TALENT`，与 PRINT_ABL / PRINT_MARK / PRINT_EXP 共用）；字符串构造在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:906-990`（`GetCharacterDataString`）

```text
若处于 skipPrint 状态：什么都不做。

target = 参数表达式求值（整数）。
s = GetCharacterDataString(target, PRINT_TALENT):
    若 target < 0 或 target >= 登录角色数：抛出 CodeEE（角色编号越界）。
    chara = 角色列表[target]
    array = chara.TALENT 数组; arrayName = CSV 中的 TALENTNAME 列表
    builder = 空串
    对 i 从 0 到 array.Length-1：
        若 i >= arrayName.Length：跳出
        若 array[i] == 0：跳过该槽位
        若 arrayName[i] 为空：跳过该槽位
        builder += "[" + arrayName[i] + "]"
    返回 builder
控制台.Print(s)；控制台.NewLine()
```

## 备注

- `ecd/Command.md` 无独立小节；`[名字]` 的输出格式与「跳过 0 值/无名槽位」仅见于源码（`GetCharacterDataString` 的 PRINT_TALENT 分支）。
- `zh/Command.md` 未收录；`zh/ERB_File_Format.md:1185` 仅一句「`PRINT_TALENT`：显示角色的素质」。
- 注意 `TALENT` 的值可为负（eramaker 系 CSV 中负值表示「没有某素质」），源码只跳过恰好为 0 的槽位，负值槽位同样会以名字显示——文档未提及此行为。
