# TOOLTIP_SETDURATION

- **类别**：命令（EE 扩展命令，Eramaker 无）
- **签名**：TOOLTIP_SETDURATION `<数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「## 工具提示系 / ### TOOLTIP_SETDURATION」；Era-Chinese-Documentation 无对应小节

## 语义

设置工具提示的最大显示时间（毫秒）。参数须为 0 以上的整数，为 0 时使用默认行为。受计时器特性的影响，极短的时间可能无法按预期工作。设置立即生效，作用于控制台全局的工具提示，无返回值。

## 用法

### TOOLTIP_SETDURATION `<毫秒>`
- 唯一参数：最大显示毫秒数，整数表达式；0 表示恢复默认行为。

```erb
;工具提示最多显示 5 秒
TOOLTIP_SETDURATION 5000
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:394`（`new TOOLTIP_SETDURATION_Instruction()`，flag = METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2158`（`TOOLTIP_SETDURATION_Instruction`）；参数构造用 `FunctionArgType.INT_EXPRESSION`（单个整数表达式）

```text
DoInstruction:
  arg = (ExpressionArgument)指令行.参数
  若 arg 是编译期常量: duration = arg.ConstInt
  否则:               duration = arg.Term.GetIntValue(exm)
  若 duration < 0 或 duration > int.MaxValue:
      抛 CodeEE("参数超出有效范围")
  若 duration > short.MaxValue(32767):
      duration = 32767        // 截断到 short 上限
  控制台.SetToolTipDuration((int)duration)
  返回
```

## 备注

- 源码把超过 32767 的值静默截断为 32767（不报错），与文档「参数为 0 以上的整数」的描述不冲突，但文档未提及该上限。
- 注意 `duration = 0` 不会被截断或特殊处理，直接传给控制台，由控制台/底层把 0 解释为「使用默认行为」。
- 与 `TOOLTIP_SETDELAY` 不同，本指令有显式的 `short.MaxValue` 截断。
