# STRDATA

- **类别**：命令
- **签名**：STRDATA `<赋值目标字符串变量>`（可省略，省略时为 `RESULTS:0`）
　　　　　DATA / DATAFORM / DATALIST～ENDLIST 候选区
　　　　　ENDDATA
- **文档来源**：`ecd/docs/translation/Command.md`（「未整理项目」章 `### STRDATA <赋值目标字符串变量>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

`PRINTDATA` 的「不显示、改为返回所选字符串」版本。在 `STRDATA`～`ENDDATA` 之间用 `DATA`、`DATAFORM`、`DATALIST`～`ENDLIST` 语法书写候选字符串，执行时从中**随机**选取一个，把选中的字符串赋值给第 1 参数指定的字符串变量，然后跳到 `ENDDATA` 继续执行。

`DATA`／`DATAFORM` 每行各构成一个候选；`DATALIST`～`ENDLIST` 之间的一组 `DATA` 系行用换行符连接后整体构成一个候选。

参数可省略，省略时赋值给 `RESULTS:0`。候选区为空时什么都不选，直接跳到 `ENDDATA`。

使用限制：`STRDATA` 不能嵌套自身；`PRINTDATA` 系不能嵌套在 `STRDATA` 内；这些在脚本装载时以警告报出。

## 用法

### STRDATA `<赋值目标字符串变量>` ～ ENDDATA

- `<赋值目标字符串变量>`：接收结果的字符串变量（`STRDATA` 系为 PARTIAL 命令，候选区必须以 `ENDDATA` 结束）。

```erb
STRDATA LOCALS
    DATALIST
        DATA 朝
        DATA 昼
    ENDLIST
    DATAFORM 夜は{RAND:10}時
ENDDATA
PRINTFORML 选中的是： %LOCALS%
```

### STRDATA（省略参数）

省略赋值目标时写入 `RESULTS:0`。

```erb
STRDATA
    DATA A
    DATA B
ENDDATA
PRINTL %RESULTS:0%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:309`（`argb[FunctionArgType.VAR_STR]`，METHOD_SAFE | EXTENDED | PARTIAL）
- 实现：`Runtime/Script/Process.ScriptProc.cs:756`（`case FunctionCode.STRDATA`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:1754`（`VAR_STR_ArgumentBuilder`）；候选区装载在 `Runtime/Script/Loader/ErbLoader.cs:1298` 起

```text
参数解析（VAR_STR_ArgumentBuilder）:
    无参数时 → 目标 = 系统变量 RESULTS:0
    有参数时 → 第 1 参数必须是可赋值字符串变量

装载期（ErbLoader）:
    STRDATA 嵌套 STRDATA → 警告；PRINTDATA 嵌套于 STRDATA 内 → 警告
    每个 DATA/DATAFORM 行 → 作为单元素候选加入 func.dataList
    DATALIST～ENDLIST 之间的 DATA 系行 → 收集到同一临时列表，ENDLIST 时作为整体候选加入 func.dataList
    ENDDATA → 闭合，func.JumpTo 指向 ENDDATA 行

case STRDATA:
    若 func.dataList.Count == 0:
        state.JumpTo(func.JumpTo)      // 无候选，直接跳到 ENDDATA
        return
    count  = func.dataList.Count
    choice = GetNextRand(count)                     // 随机选取 0..count-1
    iList  = func.dataList[choice]
    str = ""
    i = 0
    对 iList 中每个 InstructionLine:
        state.CurrentLine = 该行
        若该行 Argument == null → 先用 ArgumentParser.SetArgumentTo 补解析
        str += 该行参数项求值(GetStrValue)           // DATAFORM 在此做 FORM 展开
        若未到 iList 末尾 → str += "\n"              // DATALIST 候选用换行连接
    ((StrDataArgument)func.Argument).Var.SetValue(str, exm)
    state.JumpTo(func.JumpTo)                       // 跳到 ENDDATA，保证流程连续
```

## 备注

- ecd 文档把本命令放在「未整理项目」中，说明仅有简述；其签名未写明「参数可省略」，本仓库 `VAR_STR_ArgumentBuilder`（`minArg = 0`）支持省略并默认 `RESULTS:0`，已补记。
- 本命令与 `PRINTDATA` 共用同一套 DATA/DATAFORM/DATALIST/ENDDATA 装载逻辑与 `dataList` 结构（`List<List<InstructionLine>>`）。
- Era-Chinese-Documentation 套件未收录本命令。
