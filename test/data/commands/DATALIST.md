# DATALIST

- **类别**：命令（语法块成分，PARTIAL 指令）
- **签名**：`DATALIST`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md`「PRINTDATA(|K|D)(|L|W)」小节与「STRDATA」小节；`Era-Chinese-Documentation/docs/Command.md`「# PrintData(|K|D)(|L|W)」小节有对应语法骨架（内容较简略）

## 语义

在 `PRINTDATA～ENDDATA`、`STRDATA～ENDDATA` 块内开启一个多行候选组：`DATALIST`～`ENDLIST` 之间的每个 `DATA` 或 `DATAFORM` 行都相当于一行文本，整组被当作一个候选；随机选中该组时，组内各行按顺序输出（或以换行符连接成一个字符串）。`DATALIST` 必须以 `ENDLIST` 结束，且必须直接位于 `PRINTDATA` 系或 `STRDATA` 内（不能独立出现、不能再嵌套）。块内除 DATA/DATAFORM/ENDLIST 外的语法会在解析期报警告。

## 用法

### `PRINTDATA ...` / `STRDATA ...` 块内的 `DATALIST ... ENDLIST`
- 无参数；内部只能写 `DATA`、`DATAFORM` 行，以 `ENDLIST` 收尾。
```erb
PRINTDATA
	DATA 单行候选A
	DATALIST
		DATA 多行候选B第一行
		DATAFORM 第二行：随机数{RAND:10}
	ENDLIST
ENDDATA
;每次随机输出"单行候选A"，或两行组成的候选B
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:307` → `argb[FunctionArgType.VOID]`, METHOD_SAFE | EXTENDED | PARTIAL
- 实现：无运行期指令体（PARTIAL）；解析逻辑在 `Runtime/Script/Loader/ErbLoader.cs:1319-1344`（`case FunctionCode.DATALIST` / `ENDLIST`），使用方为 `Runtime/Script/Statements/Instraction.Child.cs:263`（`PRINT_DATA_Instruction`）与 `Runtime/Script/Process.ScriptProc.cs:756`（`STRDATA`）

```text
解析期（ErbLoader）:
    case DATALIST:
        栈顶必须存在且为 PRINTDATA 系 或 STRDATA，
        否则报错（警告级别 2）"DATALIST 出现在意外位置" 并把该行标记为错误行
        tempLineList = 空列表            // 开始收集本组候选行
        nestStack.Push(本行)

    case DATA / DATAFORM（上层是 DATALIST 时）:
        tempLineList.Add(该行)           // 不立即成组，先暂存

    case ENDLIST:
        若栈顶不是 DATALIST: 警告 "ENDLIST 出现在意外位置"
        若 tempLineList 为空: 警告（级别 1）"DATALIST 中没有 DATA"
        nestStack.Pop()                  // 弹出 DATALIST 行
        nestStack.Peek().dataList.Add(tempLineList)   // 整组作为 dataList 的一个元素

    case ENDDATA:
        若栈顶仍是 DATALIST: 报错（级别 2）"DATALIST 未被 ENDLIST 关闭"
        （注：该分支在源码中不可达——上方对栈顶「非 PRINTDATA 系且非 STRDATA」的检查已先把
         DATALIST 栈顶按「缺少 PRINTDATA/STRDATA」报错并 break）

运行期（PRINTDATA / STRDATA 使用 dataList 时）:
    dataList.Count 为候选组数（单行 DATA 与 DATALIST 组平权参与随机）
    选中 DATALIST 组时，组内各 DATA/DATAFORM 行依次求值:
        PRINTDATA：逐行 Print，行间插 NewLine
        STRDATA：各行以 "\n" 连接成一个字符串后赋给目标变量
```

## 备注

- `DATALIST～ENDLIST` 中每个 `DATA`/`DATAFORM` 都相当于一行——ecd 文档此说法与实现（组内逐行输出/以 `\n` 连接）一致。
- DATALIST 组与普通单行 DATA 在随机选择时权重相同（都是 dataList 的一个元素），不会因组内行数多而更易被选中。
- `DATALIST` 不能嵌套 `DATALIST`（块内白名单只允许 DATA/DATAFORM/ENDLIST，`Runtime/Script/Loader/ErbLoader.cs:992-996`）。
- zh 文档套件仅在 PrintData 小节给出 `DataList ... EndList` 的骨架，无详细语义。
