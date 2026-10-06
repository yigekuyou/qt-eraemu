# FTOOLTIP_SETDURATION

- **类别**：EE 扩展命令（索引登记名；实为文档提取的字节切片产物，真实命令为 `TOOLTIP_SETDURATION`）
- **签名**：
  - `TOOLTIP_SETDURATION <显示时间(ms)>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/私家改造版Emuera_readme.txt`（Shift-JIS 编码）Emuera1821+v3 条目「ツールチップの表示時間を設定する命令TOOLTIP_SETDURATIONを追加」（原文件该行以全角冒号「：」引导，其二进制字节 `0x81 0x46` 的第二字节 `0x46` 是 ASCII `F`，按字节切取关键词即误得 `FTOOLTIP_SETDURATION`）；`ecd/docs/translation/Command.md`「工具提示系 → TOOLTIP_SETDURATION」。zh 套件未收录；`test/data/_extracted/source_index.md` 将本名标注为 ABSENT。

## 语义

**`FTOOLTIP_SETDURATION` 这一名字本身并不存在于任何发行版的源码或文档中**，它是从 Shift-JIS 编码的 readme 提取关键词时把全角冒号「：」的第二字节误判为 `F` 产生的伪名，实际指向的是 `TOOLTIP_SETDURATION`。

`TOOLTIP_SETDURATION` 设置工具提示的最大显示时间，单位毫秒。参数为 0 以上的整数；为 0 时使用默认行为。负值或超过 int 上限报错；超过 short 上限（32767）的值会被压到 32767（Windows 计时器内部用 short 表示，更大的值会被系统忽略）。受计时器特性影响，极短的时间可能无法按预期工作。

## 用法

### `TOOLTIP_SETDURATION <显示时间(ms)>`
```erb
;工具提示最多显示 5 秒
TOOLTIP_SETDURATION 5000
```

## 源码实现（emuera.em/Emuera）

- 名为 `FTOOLTIP_SETDURATION` 的符号在本仓库（emuera.em/Emuera）源码中不存在（已全文 grep 确认）；`source_index.md` 的 ABSENT 标注对字面名成立，但语义来源其实是已实现的 `TOOLTIP_SETDURATION`。
- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:352`（枚举 `TOOLTIP_SETDURATION`）；`Runtime/Script/Statements/FunctionIdentifier.cs:394`（`addFunction(FunctionCode.TOOLTIP_SETDURATION, new TOOLTIP_SETDURATION_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2158`（`TOOLTIP_SETDURATION_Instruction`）

```text
构造: ArgBuilder = INT_EXPRESSION（1 个整数表达式参数）
      flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    arg = (ExpressionArgument)func.Argument
    duration = arg.IsConst ? arg.ConstInt : arg.Term.GetIntValue(exm)
    if duration < 0 或 duration > int.MaxValue:
        throw CodeEE(「参数超出范围」)
    if duration > short.MaxValue:      # 32767
        duration = short.MaxValue      # Windows 计时器上限，超出部分会被系统忽略
    Console.SetToolTipDuration((int)duration)
```

## 备注

- **索引差异**：`source_index.md` 将 `FTOOLTIP_SETDURATION` 标为「ABSENT（EE 发行版独有，未移植）」，实际情况是该名字为编码伪名；其真实指向 `TOOLTIP_SETDURATION` 在本仓库存在且已实现（还有 ecd 主文档收录，见「工具提示系」分组）。
- 私家改造版 readme 变更史：v3 追加本命令；v3.1 因 Windows 侧 short 上限把超过 32767 的参数改为按 32767 处理（对应源码中的 `short.MaxValue` 钳制）；v5.1 微调了设置了显示时长时工具提示的弹出位置（显示在鼠标光标稍下方）。
- EE 版工具提示扩展（`TOOLTIP_EXTENSION`、`TOOLTIP_IMG` 等）另见 `https://evilmask.gitlab.io/emuera.em.doc/Reference/TOOLTIP_EXTENSION/`，与 `ecd/docs/translation/HTML_PRINT.md` 的工具提示设置说明。
