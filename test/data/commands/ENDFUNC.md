# ENDFUNC

- **类别**：命令
- **签名**：`ENDFUNC`
- **文档来源**：`ecd/docs/translation/Command.md`（「CALL·JUMP·GOTO系2」节 `### FUNC`、`### ENDFUNC` 及 `### TRYCALLLIST` 等）；`Era-Chinese-Documentation` 仅在 `zh/Custom_Variable.md:293` 出现 `#DIM ENDFUNC` 字样，无语义文档。

## 语义

结束由 `TRYCALLLIST`、`TRYJUMPLIST`、`TRYGOTOLIST` 开始的列表式调用。`TRY*LIST`～`ENDFUNC` 之间只能写 `FUNC <函数名>(, 参数...)` 行；运行时会依次尝试调用各 `FUNC` 指定的函数，第一个存在的函数被调用成功后，控制流转到 `ENDFUNC` 之后继续。`ENDFUNC` 运行期是空操作（`ENDIF_Instruction`），仅作为装载期结构终点：装载时把 `TRY*LIST` 行的跳转目标设为 `ENDFUNC` 行。不能嵌套 `TRY*LIST`。

## 用法

### `ENDFUNC`

无参数，位于 `TRYCALLLIST`/`TRYJUMPLIST`/`TRYGOTOLIST` 块末尾。

```erb
TRYCALLLIST
    FUNC 函数1
    FUNC 函数2, 1, 2      ;可带参数
ENDFUNC
;等价于 TRYCCALL 函数1 → CATCH → TRYCCALL 函数2 → CATCH/ENDCATCH
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:370` → `new ENDIF_Instruction(), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3204`（`ENDIF_Instruction`，空操作）；装载期处理在 `Runtime/Script/Loader/ErbLoader.cs:1427`（`case FunctionCode.ENDFUNC`）

```text
装载期（ErbLoader）:
    遇到 ENDFUNC:
        pf = nestStack 栈顶
        若栈空，或栈顶不是 TRYCALLLIST / TRYJUMPLIST / TRYGOTOLIST:
            发出解析警告（MissingTrycalllist），跳过
        pf.JumpTo = 本 ENDFUNC 行
            ;（成功调用某个 FUNC 后，TRY*LIST 的处理直接跳到
              ENDFUNC 之后，不再尝试后面的 FUNC）
        弹出 nestStack

    相关约束（FUNC 行装载时检查，ErbLoader.cs:1400）:
        FUNC 必须直接位于 TRY*LIST 内
        FUNC 必须有实参解析结果（Argument != null）
        TRYGOTOLIST 内的 FUNC 不允许带子函数括号名（如 X@Y）
            也不允许给目标带参数

运行期（ENDIF_Instruction.DoInstruction）:
    空方法体：什么都不做，顺序通过
```

## 备注

- ecd 文档指出 `TRY*LIST`～`ENDFUNC` 内不能书写 `FUNC` 以外的语法；实现上装载器还有对应检查（`InvalidInstructionInSyntax` 警告，ErbLoader.cs:1000-1004），且 `TRY*LIST` 不允许嵌套（`NestedTrycalllist`，ErbLoader.cs:1383-1390）。
- `ENDFUNC` 与 `ENDIF`、`ENDSELECT`、`DO`、`ENDCATCH` 共用 `ENDIF_Instruction`，运行期为空操作；等价展开（TRYCCALL/CATCH 嵌套）见 ecd `### FUNC` 小节。
- `ENDFUNC` 注册时带 `EXTENDED` 但不带 `METHOD_SAFE`，与 `ENDIF`（METHOD_SAFE）、`ENDCATCH`（METHOD_SAFE|EXTENDED）的标志组合略有差别。
