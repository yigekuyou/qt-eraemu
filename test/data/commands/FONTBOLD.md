# FONTBOLD

- **类别**：命令
- **签名**：`FONTBOLD`
- **文档来源**：`ecd/docs/translation/Command.md`（「显示处理·字体处理·显示方式参考」节 `### FONTBOLD`）；`Era-Chinese-Documentation` 未收录。

## 语义

把当前的文字样式切换为加粗：在现有字体样式上追加 `Bold`（加粗）位。可与 `FONTITALIC`（倾斜）叠加使用；`FONTREGULAR` 会一次性取消加粗与倾斜。只影响之后输出的文本样式。无参数、无返回值。

## 用法

### `FONTBOLD`

无参数。

```erb
FONTBOLD
PRINTL 这行是粗体
FONTITALIC     ;可与斜体叠加
PRINTL 这行是粗体加斜体
FONTREGULAR
PRINTL 这行恢复默认样式
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:280` → `new FONTBOLD_Instruction()`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1618`（`FONTBOLD_Instruction`）

```text
指令执行:
    若 当前操作系统不是 Windows（!OperatingSystem.IsWindows()）:
        直接返回（什么也不做）
    否则:
        exm.Console.SetStringStyle(
            exm.Console.StringStyle.FontStyle | FontStyle.Bold)
        ;在当前样式的 FontStyle 上按位或 Bold，
        ;即保留斜体/下划线等既有位，仅追加加粗

（FONTITALIC 为 | FontStyle.Italic，FONTREGULAR 为直接设为
  FontStyle.Regular，三者同构）
```

## 备注

- **平台差异（文档未记载）**：实现开头有 `if (!OperatingSystem.IsWindows()) return;`，即非 Windows 平台本命令是空操作。ecd 文档只描述了加粗语义，未提平台限制。
- ecd 文档说 `BOLD` 与 `ITALIC` 可以同时使用、`REGULAR` 取消两者，与实现的按位或 / 直接置 Regular 语义一致。
- 相关命令：`FONTSTYLE <数值表达式>` 可用位或组合一次设置（0 默认、1 加粗、2 倾斜、4 删除线、8 下划线）。
