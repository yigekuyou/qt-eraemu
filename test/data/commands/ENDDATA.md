# ENDDATA

- **类别**：命令
- **签名**：`ENDDATA`
- **文档来源**：`ecd/docs/translation/Command.md`（「PRINT系列」节 `### PRINTDATA(|K|D)(|L|W)` 内的语法框架，以及「未整理项目」节 `### STRDATA` 的语法框架）；`Era-Chinese-Documentation` 在 `zh/Command.md:176` 出现 `EndData` 字样但无语义说明。

## 语义

`PRINTDATA` 系（及 `STRDATA`）随机文本块的结束标记，与 `PRINTDATA`（含 K/D/L/W 后缀变体）、`STRDATA` 配对使用。块内只能出现 `DATA`、`DATAFORM`、`DATALIST`～`ENDLIST` 语法。装载期 `ENDDATA` 负责收尾：检查 `DATALIST` 是否闭合、块内是否至少有一条 DATA 系指令，并把 `PRINTDATA`/`STRDATA` 行的跳转目标设为 `ENDDATA` 行（运行期没有任何候选文本时直接跳到这里继续）。`ENDDATA` 运行期是空操作。

## 用法

### `ENDDATA`

无参数，位于 `PRINTDATA`（或 `STRDATA`）块的末尾。

```erb
PRINTDATAL
    DATA 文本甲
    DATAFORM 文本乙\@ FLAG:0 ? （变体） ! （变体） \@
    DATALIST
        DATA 第一行
        DATAFORM 第二行
    ENDLIST
ENDDATA
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:306` → `new DO_NOTHING_Instruction()`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2017`（`DO_NOTHING_Instruction`）；装载期处理在 `Runtime/Script/Loader/ErbLoader.cs:1377`（`case FunctionCode.ENDDATA`）

```text
装载期（ErbLoader）:
    遇到 ENDDATA:
        pline = nestStack 栈顶
        若栈空，或栈顶不是 PRINTDATA 系 / STRDATA 行:
            发出解析警告（MissingPrintdataStrdata：缺少对应的
                        PRINTDATA/STRDATA），跳过
        若栈顶是 DATALIST:
            发出警告（DatalistNotClosed：DATALIST 未用 ENDLIST 闭合）
        若栈顶行的 dataList 为空:
            发出警告（InstructionDataIsMissing：块内没有任何 DATA）
        pline.JumpTo = 本 ENDDATA 行
            ;（运行期 dataList 为空时 PRINTDATA 直接跳到 ENDDATA 之后，
              与文档「没有任何 DATA 系指令的话程序会直接继续执行」一致）
        弹出 nestStack

运行期（DO_NOTHING_Instruction.DoInstruction）:
    空方法体：什么都不做（注释：事实上是 ENDIF 的非流控制版）
```

## 备注

- `ENDDATA` 与 `ENDLIST` 都注册为 `PARTIAL` 标记（仅作语法结构、在装载期处理），`ENDDATA` 的实现类注释自述「事实是 ENDIF 的非流控制版」。
- ecd 文档规定块内「不能使用上述以外的语法」，实现上装载器对块内出现 `DATA`/`DATAFORM`/`DATALIST`/`ENDLIST`/`ENDDATA` 以外的指令都会警告并忽略（ErbLoader.cs:985）；块内定义标签（`$`）也会被警告。
- `PRINTDATA` 系与 `STRDATA` 互相嵌套、`PRINTDATA` 嵌套自身均被禁止（NestedPrintdata / StrdataInsidePrintdata 等警告）。
