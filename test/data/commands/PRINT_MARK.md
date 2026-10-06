# PRINT_MARK

- **类别**：命令
- **签名**：`PRINT_MARK <数值表达式（已登录角色编号）>`
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节；语义见 `Era-Chinese-Documentation/docs/ERB_File_Format.md`（「显示训练专用的数据」小节）与 `ecd/ERB_File_Format.md`（同节）；实现细节来自源码。

## 语义

显示指定角色（第 1 参数为登录角色编号，即 `TARGET` 等使用的编号）的刻印（`MARK` 数组）。

对角色的每个 `MARK` 槽位（0～数组长度），若 `MARK:角色:槽位` 的值非 0 且对应的 `MARKNAME`（CSV 名称）非空，则输出形如 `刻印名LV值` 的文本，各段之间以空格分隔；值为 0 或名称为空的槽位被跳过。全部输出完后换行。

角色编号越界（< 0 或 ≥ 登录角色数）时抛出运行时错误。

## 用法

### `PRINT_MARK <角色编号>`
- `<角色编号>`：数值表达式，已登录角色的编号（0 起）。

```erb
PRINT_MARK 0        ; 显示主角（0 号角色）的刻印，如「刻印阴LV2」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:182`（`argb[FunctionArgType.INT_EXPRESSION], METHOD_SAFE`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:177-188`（switch-case `FunctionCode.PRINT_MARK`，与 PRINT_ABL / PRINT_TALENT / PRINT_EXP 共用）；字符串构造在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:906-990`（`GetCharacterDataString`）

```text
若当前处于 skipPrint（SKIPDISP）状态：什么都不做。

target = 参数表达式求值（整数）。
s = GetCharacterDataString(target, PRINT_MARK):
    若 target < 0 或 target >= 登录角色数：抛出 CodeEE（角色编号越界）。
    chara = 角色列表[target]
    array = chara.MARK 数组; arrayName = CSV 中的 MARKNAME 列表
    builder = 空串
    对 i 从 0 到 array.Length-1：
        若 i >= arrayName.Length：跳出
        若 array[i] == 0：跳过该槽位
        若 arrayName[i] 为空：跳过该槽位
        builder += arrayName[i] + "LV" + array[i] + " "
    返回 builder
控制台.Print(s)；控制台.NewLine()
```

## 备注

- `ecd/Command.md` 中没有本命令的独立 `###` 小节，只能从 `ERB_File_Format.md` 的训练数据命令列表得到「显示角色的刻印、需指定角色编号」的语义；输出格式（`名LV值`、跳过 0 值、与 PRINT_ABL/PRINT_EXP 共用实现）以源码为准。
- `zh/Command.md` 未收录；`zh/ERB_File_Format.md:1187` 仅有一句「`PRINT_MARK`：显示角色的刻印」。
- 与 PRINT_ABL 的差异：ABL 输出 `名LV值 `，MARK 同样是 `名LV值 `（都带空格分隔）；TALENT 输出 `[名]`、EXP 输出 `名值 `，由同一函数按 FunctionCode 分支。
