# DATA

- **类别**：命令（语法块成分，PARTIAL 指令）
- **签名**：`DATA <文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「PRINTDATA(|K|D)(|L|W)」小节（含 DATA/DATAFORM/DATALIST 语法块定义）；`Era-Chinese-Documentation/docs/Command.md`「# PrintData(|K|D)(|L|W)」小节有对应语法骨架（内容较简略）

## 语义

`DATA` 不是一条独立执行的指令，而是 `PRINTDATA～ENDDATA`、`STRDATA～ENDDATA` 及 `DATALIST～ENDLIST` 块内的候选数据行：声明一个候选字符串（原始文本，不做 FORM 展开，不解析表达式）。运行期由所属的 `PRINTDATA`/`STRDATA` 从全部候选中等概率随机选一组使用。`DATA` 行本身永远不会被"执行"，出现在这些块之外会在解析期报错（警告级别 2，该行被标记为错误行，执行到它时抛出 CodeEE）。

## 用法

### `PRINTDATA ...` 块内的 `DATA <文本>`
- 参数：候选文本，原样取用（不展开 FORM、不求值表达式）。
- 在 `DATALIST～ENDLIST` 内的多个 `DATA` 行会被换行连接成一组候选。
```erb
PRINTDATA
	DATA 你好
	DATA 欢迎光临
	DATA 再见
ENDDATA
;每次随机显示三句之一
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:304` → `argb[FunctionArgType.STR_NULLABLE]`, METHOD_SAFE | EXTENDED | PARTIAL
- 实现：无运行期指令体（PARTIAL）；解析逻辑在 `Runtime/Script/Loader/ErbLoader.cs:1345-1363`（`case FunctionCode.DATA`），使用方为 `Runtime/Script/Statements/Instraction.Child.cs:263`（`PRINT_DATA_Instruction.DoInstruction`）与 `Runtime/Script/Process.ScriptProc.cs:756`（`STRDATA` case）

```text
解析期（ErbLoader）:
    上层（栈顶）必须是 PRINTDATA 系 / STRDATA / DATALIST，否则
        报错（警告级别 2）"DATA 出现在缺少 PRINTDATA/STRDATA 的位置"，
        并把该行标记为错误行（IsError=true，执行到该行时抛 CodeEE）
    取出 DATA 行的原始文本参数（STR_NULLABLE：字面字符串）
    若上层不是 DATALIST:
        pdata.dataList.Add([本行])          // 单行即一组候选
    否则:
        tempLineList.Add(本行)              // 暂存，等 ENDLIST 时整组加入上层 dataList

使用方运行期（PRINT_DATA_Instruction / STRDATA）:
    若 dataList 为空: 跳到 ENDDATA，什么都不显示
    choice = GetNextRand(dataList.Count)    // 均匀随机选组
    （PRINTDATA 带变量参数时把 choice 存入该变量）
    对选中组内的每个 DATA 行:
        惰性求值其字符串参数（DATAFORM 才做 FORM 展开；DATA 直接取原文本）
        逐行打印（组内多行时以换行分隔）/ 或连接后赋给字符串变量（STRDATA）
    state.JumpTo(ENDDATA 行)
```

## 备注

- ecd 文档明确"`PRINTDATA~ENDDATA` 以及 `DATALIST~ENDLIST` 内不能使用上述以外的语法"，与 ErbLoader 中的白名单检查（`Runtime/Script/Loader/ErbLoader.cs:985-996`）一致：块内只允许 DATA/DATAFORM/DATALIST/ENDLIST/ENDDATA。
- `DATA` 的参数在注册处标注 `STR_NULLABLE`，即文本可省略（空串候选）。
- 注册行上 `PARTIAL | PARTIAL` 重复写了两次，属索引/源码小瑕疵，不影响语义。
- `STRDATA` 的文档（ecd「STRDATA」小节）说明 `DATALIST` 中的 `DATA` 系以换行连接返回，与 `STRDATA` 实现中 `str += "\n"` 一致。
