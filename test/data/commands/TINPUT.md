# TINPUT

- **类别**：命令（EE 扩展命令，Eramaker 无）
- **签名**：TINPUT `<限制时间>`, `<默认值>`{, `<显示剩余时间>`, `<超时显示字符串>`{, `<鼠标输入标志>`, `<可跳过标志>`}}
- **文档来源**：`ecd/docs/translation/Command.md`「### TINPUT」；Era-Chinese-Documentation 无对应小节（仅 `Replace_CSV.md` 提及 TIMEUP 超时字符串、`Variable.md` 提及 ISTIMEOUT 与 TINPUT 的关联）

## 语义

带时间限制的数值输入指令。在限制时间内等待玩家从控制台输入一个整数并赋给 `RESULT`；超时则把第 2 参数的默认值赋给 `RESULT` 并继续执行。第 3 参数控制是否在画面上显示剩余时间（0 不显示，非 0 显示，省略时视为 1）；第 4 参数是超时时显示的字符串，为空字符串时会清除计时器显示并进入下一步；未指定第 4 参数时使用 `replace.csv` 的 TIMEUP 字符串（实现中为 `Config.TimeupLabel`）。

本仓库实现还扩展了两个可选参数（EE 扩展）：第 5 参数为鼠标输入标志（1 表示接受鼠标输入），第 6 参数为可跳过标志（在文本快进 `MesSkip` 生效时直接用默认值填充 `RESULT` 而不等待输入）。

由于受 `AWAIT`/计时器精度限制，限制时间设置得比 100 毫秒更细也无法准确动作。

## 用法

### TINPUT `<限制时间毫秒>`, `<超时默认值>`{, `<显示剩余时间>`, `<超时字符串>`}
- 第 1 参数：限制时间（毫秒），数值表达式。
- 第 2 参数：超时时的默认返回值，赋给 `RESULT`。
- 第 3 参数：是否显示剩余时间，0 为不显示，其他为显示，省略时为 1。设置了第 4 参数时本参数不能省略（位置参数须按序给出）。
- 第 4 参数：超时时显示的字符串；空字符串会清除计时器显示。
- 第 5 参数（本仓库扩展）：鼠标输入标志，为 1 时 `InputRequest.MouseInput = true`。
- 第 6 参数（本仓库扩展）：可跳过标志；文本快进中且此参数被指定时，不等待输入，直接按第 5 参数把默认值写入 `RESULT`（鼠标模式写 `RESULT:1`）。

```erb
PRINTL 5 秒内输入一个数：
TINPUT 5000, 100
PRINTFORML 输入结果 = {RESULT}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:196`（`new TINPUT_Instruction(false)`，flag = IS_PRINT | IS_INPUT | EXTENDED）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1046`（`TINPUT_Instruction`，私有嵌套类）；参数构造 `Runtime/Script/Statements/ArgumentBuilder.cs:1559`（`SP_TINPUT_ArgumentBuilder`，参数类型 long,long[,long,string[,long,long]]，最少 2 个）；参数存放 `Runtime/Script/Statements/Argument.cs:208`（`SpTInputsArgument`）

```text
构造时: isOne = false（TONEINPUT 为 true）
参数解析(SP_TINPUT): 至少 2 个参数
  terms[0]=时间(long), terms[1]=默认值(long),
  terms[2]=显示标志(long,可省), terms[3]=超时字符串(string,可省),
  terms[4]=鼠标标志(long,可省), terms[5]=可跳过(long,可省)

DoInstruction:
  req = 新 InputRequest { InputType=IntValue, HasDefValue=true, OneInput=isOne }
  x = 时间表达式求值
  y = 默认值表达式求值
  若 第5参数(鼠标) 已给出: req.MouseInput = (其值 == 1)
  z = 第3参数已给出 ? 其值 : 1
  req.Timelimit   = x
  req.DefIntValue = y
  req.DisplayTime = (z != 0)
  req.TimeUpMes   = 第4参数已给出 ? 其字符串值 : Config.TimeupLabel
  若 第6参数(可跳过) 已给出 且 控制台正处于文本快进(MesSkip):
      若 第5参数(鼠标) 求值 == 0: RESULT  = 默认值
      否则:                      RESULT:1 = 默认值
      （不等待输入，直接继续）
  否则:
      控制台.WaitInput(req)   // 挂起脚本，等待输入或超时后继续
```

## 备注

- ecd 文档只描述前 4 个参数；第 5/6 参数是本仓库 `EM_私家版_INPUT系機能拡張` / `EE_INPUT機能拡張` 补丁引入的扩展，文档未收录。
- ecd 文档称「设置了第 4 参数时第 3 参数不能省略」，这是位置参数的自然结果；源码本身并不校验（第 3 参数为 null 时按 1 处理）。
- 超时字符串默认值来自 `Config.TimeupLabel`（`replace.csv` 的 TIMEUP 配置项），与 zh 文档 `Replace_CSV.md` 的描述一致。
- 与 `TONEINPUT` 共用同一实现类，仅 `isOne` 构造参数不同。
