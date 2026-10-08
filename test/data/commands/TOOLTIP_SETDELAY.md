# TOOLTIP_SETDELAY

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令（EE 扩展命令，Eramaker 无）
- **签名**：TOOLTIP_SETDELAY `<数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「## 工具提示系 / ### TOOLTIP_SETDELAY」；Era-Chinese-Documentation 无对应小节

## 语义

以毫秒为单位设置工具提示显示之前的等待时间（即鼠标停留多久后才弹出提示）。默认值是 500（毫秒），文档说明最大值为 32767。设置立即生效，作用于控制台全局的工具提示，无返回值。

## 用法

### TOOLTIP_SETDELAY `<毫秒>`
- 唯一参数：延迟毫秒数，整数表达式。

```erb
;鼠标停留 1 秒后才显示工具提示
TOOLTIP_SETDELAY 1000
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:393`（`new TOOLTIP_SETDELAY_Instruction()`，flag = METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2136`（`TOOLTIP_SETDELAY_Instruction`）；参数构造用 `FunctionArgType.INT_EXPRESSION`（单个整数表达式）

```text
DoInstruction:
  arg = (ExpressionArgument)指令行.参数
  若 arg 是编译期常量: delay = arg.ConstInt
  否则:               delay = arg.Term.GetIntValue(exm)
  若 delay < 0 或 delay > int.MaxValue:
      抛 CodeEE("参数超出有效范围")
  控制台.SetToolTipDelay((int)delay)
  返回
```

## 备注

- **文档与源码存在差异**：ecd 文档称最大值为 32767，但本仓库源码只检查 `0 ≤ delay ≤ int.MaxValue`，并未在指令层截断到 32767（对比 `TOOLTIP_SETDURATION` 实现中有显式的 `short.MaxValue` 截断）。32767 的上限若存在，也是最终交给 WinForms `ToolTip.AutoPopDelay/InitialDelay` 等底层时的行为，与本指令实现无关。
- 参数为 0～2147483647 之间任意整数都会被接受并转交给控制台。
