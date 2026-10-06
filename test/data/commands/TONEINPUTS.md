# TONEINPUTS

- **类别**：命令（EE 扩展命令，Eramaker 无）
- **签名**：TONEINPUTS `<限制时间>`, `<默认字符串表达式>`{, `<显示剩余时间>`, `<超时显示字符串>`{, `<鼠标输入标志>`, `<可跳过标志>`}}
- **文档来源**：`ecd/docs/translation/Command.md`「### TONEINPUTS」；Era-Chinese-Documentation 无对应小节

## 语义

同时具备 `ONEINPUTS` 与 `TINPUTS` 性质、带时间限制的单字符字符串输入指令：只接受一个字符，输入后（或超时后）自动进入下一步处理，无需回车，结果赋给 `RESULTS`。参数含义分别与 `TINPUTS` 相同。

与 `INPUTS` 一样可以使用宏表达式；要把 `(` `)` 作为普通字符使用，请用 `\` 转义。使用该指令时，即使 CONFIG 设置为使用键盘宏，也可能无法正常工作，这是规格。

## 用法

### TONEINPUTS `<限制时间毫秒>`, `<默认字符串表达式>`{, `<显示剩余时间>`, `<超时字符串>`}
- 第 1 参数：限制时间（毫秒）。
- 第 2 参数：超时默认字符串（FORM 格式），赋给 `RESULTS`。
- 第 3 参数：是否显示剩余时间，0 不显示，其他显示，省略为 1。
- 第 4 参数：超时时显示的字符串。
- 第 5/6 参数（本仓库扩展）：鼠标输入标志 / 可跳过标志，语义同 `TINPUTS`。

```erb
PRINTL 3 秒内按任意字母键：
TONEINPUTS 3000, "z"
PRINTFORML 你按下了 %RESULTS%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:199`（`new TINPUTS_Instruction(true)`，flag = IS_PRINT | IS_INPUT | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1103`（`TINPUTS_Instruction`，与 TINPUTS 共用，`isOne = true`）；参数构造 `Runtime/Script/Statements/ArgumentBuilder.cs:1585`（`SP_TINPUTS_ArgumentBuilder`）

```text
与 TINPUTS 完全相同的流程，唯一区别是构造时 isOne = true：
  req.OneInput = true
  → 控制台.WaitInput(req) 时按“单字符输入”模式处理：
    任一字符输入即完成并继续，超时把 req.DefStrValue（第 2 参数）写入 RESULTS。
  其余（MouseInput、DisplayTime、TimeUpMes、可跳过分支）与 TINPUTS 一致。
```

## 备注

- ecd 文档对 `TONEINPUTS` 仅写「参数分别与 TINPUTS 相同」；第 5/6 扩展参数文档未收录。
- 旧版对 OneInput 情形截取默认串首字符的代码（`strs.Remove(1)`）在本仓库中已被注释禁用。
