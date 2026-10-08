# TINPUTS

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令（EE 扩展命令，Eramaker 无）
- **签名**：TINPUTS `<限制时间>`, `<默认字符串表达式>`{, `<显示剩余时间>`, `<超时显示字符串>`{, `<鼠标输入标志>`, `<可跳过标志>`}}
- **文档来源**：`ecd/docs/translation/Command.md`「### TINPUTS」；Era-Chinese-Documentation 无对应小节

## 语义

带时间限制的字符串输入指令，是 `TINPUT` 的字符串版。在限制时间内等待玩家输入字符串并赋给 `RESULTS`；超时则把第 2 参数（字符串表达式求值结果）赋给 `RESULTS` 并继续。第 3 参数控制是否显示剩余时间（省略时为 1），第 4 参数为超时时显示的字符串。

与 `INPUTS` 一样可以使用宏表达式（即默认值参数按 `PRINTFORM` 的格式字符串解析）；要把 `(` `)` 作为普通字符使用，请用 `\` 转义。

本仓库实现同样扩展了第 5 参数（鼠标输入标志）与第 6 参数（可跳过标志），语义与 `TINPUT` 对应参数一致。

## 用法

### TINPUTS `<限制时间毫秒>`, `<默认字符串表达式>`{, `<显示剩余时间>`, `<超时字符串>`}
- 第 1 参数：限制时间（毫秒）。
- 第 2 参数：超时时的默认字符串（FORM 格式，赋给 `RESULTS`）。
- 第 3 参数：是否显示剩余时间，0 不显示，其他显示，省略为 1。
- 第 4 参数：超时时显示的字符串；空字符串会清除计时器显示。
- 第 5 参数（本仓库扩展）：鼠标输入标志。
- 第 6 参数（本仓库扩展）：可跳过标志；文本快进中直接用默认值填充 `RESULTS`（鼠标模式写 `RESULTS:1`）。

```erb
PRINTL 5 秒内输入名字：
TINPUTS 5000, "无名氏"
PRINTFORML 你好，%RESULTS%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:197`（`new TINPUTS_Instruction(false)`，flag = IS_PRINT | IS_INPUT | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1103`（`TINPUTS_Instruction`）；参数构造 `Runtime/Script/Statements/ArgumentBuilder.cs:1585`（`SP_TINPUTS_ArgumentBuilder`，参数类型 long,string[,long,string[,long,long]]，最少 2 个）；参数存放 `Runtime/Script/Statements/Argument.cs:208`（`SpTInputsArgument`，与 TINPUT 共用）

```text
构造时: isOne = false（TONEINPUTS 为 true）
参数解析(SP_TINPUTS): 至少 2 个参数
  terms[0]=时间(long), terms[1]=默认字符串(string,FORM),
  terms[2]=显示标志(可省), terms[3]=超时字符串(可省),
  terms[4]=鼠标标志(可省), terms[5]=可跳过(可省)

DoInstruction:
  req = 新 InputRequest { InputType=StrValue, HasDefValue=true, OneInput=isOne }
  x    = 时间表达式求值
  strs = 默认字符串表达式求值（GetStrValue，FORM 已在表达式层解析）
  若 第5参数 已给出: req.MouseInput = (其值 == 1)
  z = 第3参数已给出 ? 其值 : 1
  req.Timelimit   = x
  req.DefStrValue = strs
  req.DisplayTime = (z != 0)
  req.TimeUpMes   = 第4参数已给出 ? 其字符串值 : Config.TimeupLabel
  若 第6参数 已给出 且 控制台处于文本快进(MesSkip):
      若 第5参数 求值 == 0: RESULTS  = 默认字符串
      否则:                 RESULTS:1 = 默认字符串
      （不等待输入）
  否则:
      控制台.WaitInput(req)
```

## 备注

- ecd 文档只描述前 4 个参数，第 5/6 参数为本仓库扩展，文档未收录。
- ecd 文档写「第 2 参数：`<字符串表达式>`」，宏表达式解析由表达式层完成，指令实现内不再截断（旧版被注释掉的代码曾对 OneInput 情形 `Remove(1)` 截取首字符，现已禁用）。
- 与 `TONEINPUTS` 共用同一实现类，仅 `isOne` 不同。
