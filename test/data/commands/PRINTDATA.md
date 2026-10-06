# PRINTDATA

- **类别**：命令（EXTENDED，块结构指令）
- **签名**：`PRINTDATA <数值变量>`（数值变量可省略）……块体……`ENDDATA`
- **文档来源**：`ecd/docs/translation/Command.md` 之 `### PRINTDATA(|K|D)(|L|W)`；`Era-Chinese-Documentation/docs/Command.md` 之 `### # PrintData(|K|D)(|L|W)`（zh 文档该节内容残缺，仅有格式骨架，无语义说明）

## 语义

PRINTDATA 系列指令：在 `PRINTDATA` 与 `ENDDATA` 之间的 `DATA`／`DATAFORM`／`DATALIST~ENDLIST` 候选文本中**随机选一个显示**，不需要 IF 和 RAND。所有候选在脚本加载期就被收集为候选列表。

- 不带参数时从全部候选中均匀随机选取一个；**带数值变量参数时，被选中候选的编号（0 起）会存入该变量**（注意：是随机结果回写变量，而不是按变量值选择——zh 文档旧版与私家改造版 Readme 所说「根据变量值进入指定编号的 DATA」与源码不符，见备注）。
- 每个顶层 `DATA`/`DATAFORM` 各是一个候选；`DATALIST~ENDLIST` 中的每个 `DATA`/`DATAFORM` 是候选内部的一行，整个 DATALIST 作为**一个**候选，显示时各行以换行连接。
- 无 `L`/`W` 后缀时显示后不换行不等待；候选列表为空（无任何 DATA）时什么都不显示，直接继续执行。
- 块内不允许出现 DATA 系以外的语句；块内不能定义 GOTO 标签；PRINTDATA 系不能嵌套，块内也不能出现 STRDATA。

## 用法

### PRINTDATA（省略数值变量）
从候选中随机选一个显示，不回写编号。

```erb
PRINTDATA
	DATA 你好。
	DATA 再见。
	DATALIST
		DATA 第一行
		DATAFORM 第二行 {A} %B%
	ENDLIST
ENDDATA
```

### PRINTDATA <数值变量>
随机选中后把编号（0 起）写入 `<数值变量>`，再显示对应候选。

```erb
PRINTDATA LOCAL:0
	DATA 苹果
	DATA 香蕉
ENDDATA
; LOCAL:0 == 0 显示「苹果」，== 1 显示「香蕉」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:162`（`addPrintDataFunction(FunctionCode.PRINTDATA)` → `new PRINT_DATA_Instruction(...)`）；块配对表 `Runtime/Script/Statements/FunctionIdentifier.cs:474`（`funcMatch[PRINTDATA] = "ENDDATA"`）；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:220`
- 实现类：`Runtime/Script/Statements/Instraction.Child.cs:225-310`（`PRINT_DATA_Instruction`）
- 参数解析：`Runtime/Script/Statements/ArgumentBuilder.cs:1733-1752`（`VAR_INT_ArgumentBuilder`，0 个或 1 个可赋值数值变量 → `PrintDataArgument`，`Runtime/Script/Statements/Argument.cs:339`）；候选收集在加载期 `Runtime/Script/Loader/ErbLoader.cs:1269-1296`（PRINTDATA 入栈、嵌套检查）、`Runtime/Script/Loader/ErbLoader.cs:1345-1378`（DATA/DATAFORM/DATALIST/ENDLIST/ENDDATA 处理，`ENDDATA` 时 `pline.JumpTo = func`）

```text
构造 PRINT_DATA_Instruction("PRINTDATA"):
    flag = EXTENDED | IS_PRINT | IS_PRINTDATA | PARTIAL
    参数构造器 = VAR_INT（可选单个可赋值数值变量）
    跳过名字前 9 字符 "PRINTDATA"，剩余为空
    依次匹配 K / D / L / W 后缀（本命令无后缀）:
        无 L/W → flag |= METHOD_SAFE
    参数构造器为空或名字未解析完 → 抛 ExeEE("PRINTDATA异常")

加载期（ErbLoader.nestCheck）:
    遇到 PRINTDATA:
        若嵌套栈中已有 PRINTDATA 系 → 警告「PRINTDATA系命令被嵌套」
        若嵌套栈中已有 STRDATA → 警告「PRINTDATA系中包含STRDATA」
        func.dataList = 新建 List<List<InstructionLine>>；入栈
    在 PRINTDATA 块内只允许 DATA / DATAFORM / DATALIST / ENDLIST / ENDDATA，否则警告
    块内出现 GOTO 标签行 → 警告「不能在 PRINTDATA 内定义标签」
    DATA/DATAFORM: 顶层 → 自成一个候选 [该行] 追加进 dataList；
                   在 DATALIST 内 → 追加进当前 tempLineList
    DATALIST: 新建 tempLineList，入栈
    ENDLIST: DATALIST 出栈，tempLineList 作为一个候选追加进 dataList（空 DATALIST 警告）
    ENDDATA: 必须与 PRINTDATA/STRDATA 配对；若 dataList 为空 → 警告「指令没有DATA」；
             PRINTDATA 行的 JumpTo = ENDDATA 行；出栈

执行期 DoInstruction（PRINT_DATA_Instruction）:
    若全局 SkipPrint（SKIPDISP 生效）→ 返回
    Console.UseUserStyle = true
    Console.UseSetColorStyle = !IsPrintDFunction()   # 无 D → 应用 SETCOLOR
    若 func.dataList 为空 → JumpTo(ENDDATA 行) 返回  # 无候选直接跳过
    count = dataList.Count
    choice = exm.VEvaluator.GetNextRand(count)        # 均匀随机 0..count-1
    若参数带了数值变量 iTerm: iTerm.SetValue(choice)  # 回写随机编号
    iList = dataList[choice]                          # 一个候选 = 一组 InstructionLine
    i = 0
    对 iList 中每行 selectedLine:
        state.CurrentLine = selectedLine
        若该行尚未建立参数 → 现场解析
        str = 该行表达式求值结果（DATA 为字符串字面量，DATAFORM 为 FORM 展开结果）
        若 IsPrintKFunction: str = ConvertStringType(str)  # 无 K 不转换
        Console.Print(str)
        若 ++i < iList.Count → Console.NewLine()   # 候选内部各行之间换行（DATALIST 情形）
    若 IsNewLine() 或 IsWaitInput():               # 无 L/W 则都不成立
        Console.NewLine()
        若 IsWaitInput(): Console.ReadAnyKey()
    Console.UseSetColorStyle = true
    JumpTo(ENDDATA 行)                              # 跳过块体继续执行
```

## 备注

- **文档与源码的差异**：ecd 引用的私家改造版 Readme 及 zh 旧文档称「当指定数值变量参数时，将根据变量值进入指定编号的 DATA」；源码实际是**先随机选编号、再把编号写入变量**（`DoInstruction` 中 `choice = GetNextRand(count)` 后 `iTerm.SetValue(choice)`），变量值并不影响选择。应以源码为准。
- zh 文档的 `### # PrintData(|K|D)(|L|W)` 小节正文残缺（只有书写格式骨架，后接乱码行号），仅能用于确认签名；语义以 ecd 为准。
- `PRINTDATA~ENDDATA` 内没有任何 DATA 时程序直接继续执行下一步——文档与源码一致（`dataList.Count == 0` 时直接 JumpTo）。
- 块内的 `DATA`/`DATAFORM` 行的求值发生在**执行期**（逐行取值），因此 `DATAFORM` 中的表达式取的是运行当时的值；文档提醒「请在使用 PRINTDATA 前就已经确定要显示的文本」与候选结构在加载期固定这一事实一致。
- K/D/L/W 变体见 PRINTDATAK / PRINTDATAD / PRINTDATAL / PRINTDATAW 等各自文档；本文件对应无后缀的基础形式。
