# TONEINPUT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令（EE 扩展命令，Eramaker 无）
- **签名**：TONEINPUT `<限制时间>`, `<默认值>`{, `<显示剩余时间>`, `<超时显示字符串>`{, `<鼠标输入标志>`, `<可跳过标志>`}}
- **文档来源**：`ecd/docs/translation/Command.md`「### TONEINPUT」；Era-Chinese-Documentation 无对应小节

## 语义

同时具备 `ONEINPUT` 与 `TINPUT` 性质、带时间限制的单字符数值输入指令：只接受一个字符，输入后（或超时后）自动进入下一步处理，无需回车。参数含义分别与 `TINPUT` 相同——第 1 参数为限制时间（毫秒），第 2 参数为超时默认值（赋给 `RESULT`），第 3 参数控制是否显示剩余时间（省略为 1），第 4 参数为超时显示字符串。

使用该指令时，即使 Emuera 的 CONFIG 设置为使用键盘宏，也可能无法正常工作，这是规格。

## 用法

### TONEINPUT `<限制时间毫秒>`, `<超时默认值>`{, `<显示剩余时间>`, `<超时字符串>`}
- 第 1 参数：限制时间（毫秒）。
- 第 2 参数：超时默认值，赋给 `RESULT`。
- 第 3 参数：是否显示剩余时间，0 不显示，其他显示，省略为 1。
- 第 4 参数：超时时显示的字符串。
- 第 5/6 参数（本仓库扩展）：鼠标输入标志 / 可跳过标志，语义同 `TINPUT`。

```erb
PRINTL 3 秒内按 Y 或 N：
TONEINPUT 3000, 0
SELECTCASE RESULT
	CASE asc("Y")
		PRINTL 是
	CASEELSE
		PRINTL 否/超时
ENDSELECT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:198`（`new TINPUT_Instruction(true)`，flag = IS_PRINT | IS_INPUT | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1046`（`TINPUT_Instruction`，与 TINPUT 共用，`isOne = true`）；参数构造 `Runtime/Script/Statements/ArgumentBuilder.cs:1559`（`SP_TINPUT_ArgumentBuilder`）

```text
与 TINPUT 完全相同的流程，唯一区别是构造时 isOne = true：
  req.OneInput = true
  → 控制台.WaitInput(req) 时按“单字符输入”模式处理：
    任意一个字符按键/鼠标点击即视为完成输入并继续，
    超时则把 req.DefIntValue（第 2 参数）写入 RESULT。
  其余（MouseInput、DisplayTime、TimeUpMes、可跳过分支）与 TINPUT 一致。
```

## 备注

- ecd 文档对 `TONEINPUT` 仅写「参数分别与 TINPUT 相同」；第 5/6 扩展参数文档未收录。
- 实现中有一段被注释掉的 `EM_私家版` 代码（原本打算对默认值取绝对值/截取个位，以及 `GlobalStatic.Process.InputInteger(1, 0)`），现均未启用。
