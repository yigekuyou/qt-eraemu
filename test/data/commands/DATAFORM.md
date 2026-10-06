# DATAFORM

- **类别**：命令（语法块成分，PARTIAL 指令）
- **签名**：`DATAFORM <FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「PRINTDATA(|K|D)(|L|W)」小节（含 DATA/DATAFORM/DATALIST 语法块定义）；`Era-Chinese-Documentation/docs/Command.md`「# PrintData(|K|D)(|L|W)」小节有对应语法骨架（内容较简略）

## 语义

`DATA` 的 FORM 格式版本：在 `PRINTDATA～ENDDATA`、`STRDATA～ENDDATA` 及 `DATALIST～ENDLIST` 块内声明一个候选文本，该文本在运行期被选中时才做 FORM 展开（替换 `{数值表达式}`、`%字符串表达式%`、`\@…#…\@` 等）。与 `DATA` 一样，`DATAFORM` 行本身不独立执行，出现在块之外会在解析期报错（警告级别 2，该行被标记为错误行，执行到它时抛出 CodeEE）。

## 用法

### `PRINTDATA ...` 块内的 `DATAFORM <FORM格式文本>`
- 参数：FORM 格式文本，选中时展开。
- 在 `DATALIST～ENDLIST` 内与 `DATA` 混用时，每个 `DATA`/`DATAFORM` 都相当于一行。
```erb
FOR I, 0, 3
	PRINTDATA
		DATAFORM 第{I}次尝试
		DATA 尝试失败
	ENDDATA
NEXT
;FORM 展开在显示时进行，I 的当前值会反映到输出中
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:305` → `argb[FunctionArgType.FORM_STR_NULLABLE]`, METHOD_SAFE | EXTENDED | PARTIAL
- 实现：无运行期指令体（PARTIAL）；解析逻辑在 `Runtime/Script/Loader/ErbLoader.cs:1345-1363`（`case FunctionCode.DATA / DATAFORM` 共用），使用方为 `Runtime/Script/Statements/Instraction.Child.cs:263`（`PRINT_DATA_Instruction.DoInstruction`）与 `Runtime/Script/Process.ScriptProc.cs:756`（`STRDATA` case）

```text
解析期（ErbLoader）:
    与 DATA 完全同路径（DATA 与 DATAFORM 同一 case）:
        上层必须是 PRINTDATA 系 / STRDATA / DATALIST，否则报错并把该行标记为错误行
        上层不是 DATALIST → pdata.dataList.Add([本行])
        上层是 DATALIST   → tempLineList.Add(本行)
    区别仅在参数类型：FORM_STR_NULLABLE，参数被解析为 FORM 项（含 `{…}` / `%…%` / `\@…\@` 项）而非字面串

运行期（PRINT_DATA_Instruction / STRDATA 使用时）:
    选中本行后惰性构造参数:
        若 selectedLine.Argument == null: ArgumentParser.SetArgumentTo(selectedLine)
    term.GetStrValue(exm)   // FORM 项此时才求值展开
    之后流程与 DATA 相同（PRINT 逐行输出或 STRDATA 赋值，组内行以 "\n" 连接）
```

## 备注

- `DATA` 与 `DATAFORM` 在解析期唯一区别是 ArgBuilder（`STR_NULLABLE` vs `FORM_STR_NULLABLE`）；ecd 文档对两者语义的描述与此一致。
- ecd 文档称"请在使用 PRINTDATA 前就已经确定要显示的文本"，但实现上 FORM 展开发生在显示时刻，FORM 版的值取决于显示时的变量状态；纯 `DATA` 行则确实是解析后固定的原始文本。
- zh 文档套件仅在 PrintData 小节给出 `DataForm(Form文字列)` 的骨架，无详细语义。
