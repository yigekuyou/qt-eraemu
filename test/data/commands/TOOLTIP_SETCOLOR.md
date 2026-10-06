# TOOLTIP_SETCOLOR

- **类别**：命令（EE 扩展命令，Eramaker 无）
- **签名**：TOOLTIP_SETCOLOR `<前景色数值表达式>`, `<背景色数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「## 工具提示系 / ### TOOLTIP_SETCOLOR」；Era-Chinese-Documentation 无对应小节

## 语义

设置把鼠标光标放在按钮上时显示的工具提示（tooltip）的前景色与背景色。两个参数均为 `0xRRGGBB` 形式的整数：第 1 参数为前景色，第 2 参数为背景色。如果想用 R、G、B 三个分量或颜色名字符串来指定，请配合 `COLOR_FROMRGB`、`COLOR_FROMNAME` 式中函数使用。

任一参数超出 `0x000000`～`0xFFFFFF` 范围时抛出脚本错误（CodeEE）。设置立即生效，作用于控制台全局的工具提示，无返回值。

工具提示的设置方法（如何让按钮带 tooltip）请参阅 HTML_PRINT 相关文档。

## 用法

### TOOLTIP_SETCOLOR `<前景色>`, `<背景色>`
- 第 1 参数：前景色，`0xRRGGBB` 整数表达式。
- 第 2 参数：背景色，`0xRRGGBB` 整数表达式。

```erb
;黑字白底的工具提示
TOOLTIP_SETCOLOR 0x000000, 0xFFFFFF
;等价写法：TOOLTIP_SETCOLOR COLOR_FROMRGB(0,0,0), COLOR_FROMNAME("White")
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:392`（`new TOOLTIP_SETCOLOR_Instruction()`，flag = METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2113`（`TOOLTIP_SETCOLOR_Instruction`）；参数构造用 `FunctionArgType.SP_SWAP`（两个任意表达式，存入 `SpSwapCharaArgument`，见 `Runtime/Script/Statements/Argument.cs:166`）

```text
DoInstruction:
  arg = (SpSwapCharaArgument)指令行.参数
  foreColor = arg.X.GetIntValue(exm)
  backColor = arg.Y.GetIntValue(exm)
  若 foreColor < 0 或 foreColor > 0xFFFFFF:
      抛 CodeEE("第 1 参数不是有效的颜色代码")
  若 backColor < 0 或 backColor > 0xFFFFFF:
      抛 CodeEE("第 2 参数不是有效的颜色代码")
  fc = Color.FromArgb(foreColor >> 16, (foreColor >> 8) & 0xFF, foreColor & 0xFF)
  bc = Color.FromArgb(backColor >> 16, (backColor >> 8) & 0xFF, backColor & 0xFF)
  控制台.SetToolTipColor(fc, bc)
  返回
```

## 备注

- 文档与源码一致。ecd 文档说明的「最大 0xFFFFFF」与源码的越界检查吻合；源码不校验 Alpha 分量，因为 `0xRRGGBB` 本身不超过 24 位。
- 颜色拆分逻辑：R = 值 >> 16，G = (值 >> 8) & 0xFF，B = 值 & 0xFF。
