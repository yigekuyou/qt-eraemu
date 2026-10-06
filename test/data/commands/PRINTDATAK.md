# PRINTDATAK

- **类别**：命令（EXTENDED，块结构指令）
- **签名**：`PRINTDATAK <数值变量>`（数值变量可省略）……块体……`ENDDATA`
- **文档来源**：`ecd/docs/translation/Command.md` 之 `### PRINTDATA(|K|D)(|L|W)`；`Era-Chinese-Documentation/docs/Command.md` 之 `### # PrintData(|K|D)(|L|W)`（zh 文档该节内容残缺，仅格式骨架）

## 语义

PRINTDATA 的 `K` 变体：在 `PRINTDATAK` 与 `ENDDATA` 之间的候选中随机选一个显示，**显示时应用 `FORCEKANA` 指令的假名转换设置**（平假名↔片假名、半全角等）。显示后不换行不等待。其余语义与 PRINTDATA 完全相同：带数值变量参数时把随机选中的编号（0 起）回写该变量；无候选时直接继续；块内只允许 DATA 系语法，不能嵌套 PRINTDATA 系或 STRDATA，块内不能定义标签。

## 用法

### PRINTDATAK（省略数值变量）
随机显示一个候选，绘制时应用 FORCEKANA 设置；不换行。

```erb
FORCEKANA 1
PRINTDATAK
	DATA ようこそ。
ENDDATA
```

### PRINTDATAK <数值变量>
同上，并把随机选中的编号（0 起）写入 `<数值变量>`。

```erb
PRINTDATAK LOCAL:0
	DATA あ
	DATA い
ENDDATA
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:165`（`addPrintDataFunction(FunctionCode.PRINTDATAK)`）；块配对表 `Runtime/Script/Statements/FunctionIdentifier.cs:477`（`funcMatch[PRINTDATAK] = "ENDDATA"`）；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:308`
- 实现类：`Runtime/Script/Statements/Instraction.Child.cs:225-310`（`PRINT_DATA_Instruction`，与 PRINTDATA 共用）
- 参数解析：`Runtime/Script/Statements/ArgumentBuilder.cs:1733-1752`（`VAR_INT_ArgumentBuilder` → `PrintDataArgument`）；候选收集与加载期检查：`Runtime/Script/Loader/ErbLoader.cs:1269-1296`、`1345-1378`

```text
构造 PRINT_DATA_Instruction("PRINTDATAK"):
    flag = EXTENDED | IS_PRINT | IS_PRINTDATA | PARTIAL
    参数构造器 = VAR_INT（可选单个可赋值数值变量）
    跳过 "PRINTDATA" 后剩余 "K":
        匹配 "K" → flag |= ISPRINTKFUNC | EXTENDED
        不匹配 D、不匹配 L/W → flag |= METHOD_SAFE（无换行不等待）
    名字解析完，正常

执行期 DoInstruction（与 PRINTDATA 相同，仅 K 标志生效）:
    若全局 SkipPrint → 返回
    Console.UseUserStyle = true; UseSetColorStyle = true   # 无 D，应用 SETCOLOR
    若 dataList 为空 → JumpTo(ENDDATA) 返回
    choice = GetNextRand(dataList.Count)
    若带数值变量参数: 写入 choice
    对候选内每行:
        str = 求值结果
        若 IsPrintKFunction: str = exm.ConvertStringType(str)  # K 变体：依 FORCEKANA 做假名/半全角转换
        Console.Print(str)
        候选内部行间 NewLine()
    无 L/W → 不追加换行、不等待
    JumpTo(ENDDATA)
```

## 备注

- zh 文档该节正文残缺，语义以 ecd 与源码为准。
- `K` 的实现是执行期对显示字符串调用 `ExpressionMediator.ConvertStringType`：当设置了 `forceHiragana`/`forceKatakana`/`halftoFull`（由 `FORCEKANA` 指令设置）时进行假名转换，否则原样返回（`Runtime/Script/Statements/ExpressionMediator.cs:64`）。
- ecd 引用的旧 Readme 称数值变量「根据变量值进入指定编号的 DATA」，与源码（随机后回写编号）不符，见 PRINTDATA.md 备注。
