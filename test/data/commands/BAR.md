# BAR

- **类别**：命令
- **签名**：
  - `BAR <变量>, <最大值>, <长度>`
- **文档来源**：`ecd/docs/translation/Command.md` 无本命令专节（仅在 `BARSTR` 小节提及「与 `BAR` 指令类似」）；`ecd ERB_File_Format.md`「其他基本命令」与 `Replace_CSV.md`（BAR 用字符）有简要说明；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「其他基本命令」同段（含示例）。

## 语义

在画面上绘制一个条形（BAR）控件，形如 `[*****.....]`：以 `<最大值>` 为满格基准，按 `<变量>` 的当前值决定填充格数，总长度为 `<长度>` 格。填充字符与空置字符由 ERB 配置文件中 `BAR` 项（对应 `Config.BarChar1` / `Config.BarChar2`）指定。绘制的字符串直接打印到控制台（不换行）。值超过最大值按满格处理，为负则按 0 格处理。

参数不合法时抛错：`<最大值>` <= 0、`<长度>` <= 0、`<长度>` >= 100（防失控）均为 CodeEE。此命令与 `BARSTR`（把条形字符串写入 `RESULTS:0`）和 `BARL`（绘制后换行）相对应。

## 用法

### `BAR <变量>, <最大值>, <长度>`
- `<变量>`：数值表达式，当前值。
- `<最大值>`：数值表达式，满格基准值，必须为正。
- `<长度>`：数值表达式，总格数（1～99），超出部分条格数会被截到 `<长度>`。
```erb
MONEY = 500
DRAWLINE
BAR MONEY, 1000, 20
PRINTL
DRAWLINE
;输出形如：[**********..........]
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:189`（`new BAR_Instruction(false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1341`（`BAR_Instruction`，构造参数 `newline=false`）；条形字符串生成在 `Runtime/Script/Statements/ExpressionMediator.cs:118`（`CreateBar`）

```text
指令类 BAR_Instruction(newline=false):
    flag = IS_PRINT | METHOD_SAFE | EXTENDED
    参数由 SP_BAR 专用参数构造器解析为三个整数项

    DoInstruction(exm, func, state):
        barArg = func.Argument（SpBarArgument）
        var    = barArg.Terms[0].GetIntValue(exm)
        max    = barArg.Terms[1].GetIntValue(exm)
        length = barArg.Terms[2].GetIntValue(exm)
        exm.Console.Print(CreateBar(var, max, length))
        # newline=false → 不调用 NewLine()

CreateBar(var, max, length):
    若 max <= 0:    抛 CodeEE（BAR 最大值须为正）
    若 length <= 0: 抛 CodeEE（BAR 长度须为正）
    若 length >= 100: 抛 CodeEE（BAR 过长，防失控）
    count = var * length / max   # unchecked 整数运算
    若 count < 0:    count = 0
    若 count > length: count = length
    返回 "[" + BarChar1 × count + BarChar2 × (length - count) + "]"
```

## 备注

- `BARL` 是同一实现类的 `newline=true` 变体（`Runtime/Script/Statements/FunctionIdentifier.cs:189`）；`BARSTR` 则用同一算法把结果字符串存入 `RESULTS:0` 而不打印。
- 文档（`ERB_File_Format.md`）把 `BAR` 写在「其他基本命令」中，且示例里实际用的是 `BARL`；`BAR` 本身不换行，常需紧跟 `PRINTL` 换行，这与源码 `newline=false` 一致。
- 填充/空置字符来自 ERB 配置 `BAR` 项（`Replace_CSV.md`），对应 `Config.BarChar1/BarChar2`，文档与源码一致。
