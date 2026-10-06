# BARL

- **类别**：命令
- **签名**：
  - `BARL <变量>, <最大值>, <长度>`
- **文档来源**：`ecd/docs/translation/Command.md` 无本命令专节；`ecd ERB_File_Format.md`「其他基本命令」（示例中使用 `BARL MONEY , 1000 , 20`）与 `Replace_CSV.md`（BAR 用字符）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「其他基本命令」同段（含完整示例与输出）。

## 语义

`BAR` 的换行版：绘制形如 `[*****.....]` 的条形控件后自动换行。以 `<最大值>` 为满格基准，按 `<变量>` 当前值决定填充格数，总长 `<长度>` 格；填充/空置字符由 ERB 配置的 `BAR` 字符（`Config.BarChar1/BarChar2`）指定。参数错误行为与 `BAR` 相同（最大值或长度 <= 0、长度 >= 100 时抛 CodeEE）。

## 用法

### `BARL <变量>, <最大值>, <长度>`
- `<变量>`：数值表达式，当前值。
- `<最大值>`：数值表达式，满格基准值，必须为正。
- `<长度>`：数值表达式，总格数（1～99）。
```erb
MONEY = 500
DRAWLINE
BARL MONEY, 1000, 20
PRINTFORMW 我有{MONEY}元钱。
TIMES MONEY, 1.25
BARL MONEY, 1000, 20
PRINTFORMW 我有{MONEY}元钱，游戏结束。
;输出：
;[**********..........]
;[************........]
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:190`（`new BAR_Instruction(true)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1341`（`BAR_Instruction`，构造参数 `newline=true`）；条形字符串生成在 `Runtime/Script/Statements/ExpressionMediator.cs:118`（`CreateBar`）

```text
指令类 BAR_Instruction(newline=true):
    flag = IS_PRINT | METHOD_SAFE | EXTENDED
    参数由 SP_BAR 专用参数构造器解析为三个整数项

    DoInstruction(exm, func, state):
        var    = Terms[0].GetIntValue(exm)
        max    = Terms[1].GetIntValue(exm)
        length = Terms[2].GetIntValue(exm)
        exm.Console.Print(CreateBar(var, max, length))
        若 newline:                     # BARL 时为 true
            exm.Console.NewLine()       # 与 BAR 唯一的差别

CreateBar(var, max, length):
    若 max <= 0 / length <= 0 / length >= 100: 抛 CodeEE
    count = clamp(var * length / max, 0, length)
    返回 "[" + BarChar1 × count + BarChar2 × (length - count) + "]"
```

## 备注

- 与 `BAR` 共用 `BAR_Instruction`，仅构造参数 `newline` 不同（见 `BAR.md` 备注）。
- 两套文档都没有 `BARL` 独立小节，仅以示例形式出现；zh 文档示例的输出（`[**********..........]` 等）与 `CreateBar` 的 `var*length/max` 截断算法一致。
