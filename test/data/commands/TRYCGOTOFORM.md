# TRYCGOTOFORM

- **类别**：命令
- **签名**：
  - `TRYCGOTOFORM <FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」→「TRYCGOTOFORM」；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

`TRYGOTOFORM` 对应的 TRYC 系版本：与 `GOTO` 相同的 `$` 标签跳转，标签名用 FORM 格式文本给出；展开后的标签不存在时不报错，而是转入 `CATCH`～`ENDCATCH` 分支执行失败处理。

TRYC 系结构：命令之后到 `CATCH` 之前为成功路径（可省略），`CATCH` 到 `ENDCATCH` 之间为失败处理，可嵌套。跳入循环、分支语法内时的行为与 `TRYGOTO`/`GOTO` 相同。

## 用法

### `TRYCGOTOFORM <FORM格式文本>`
- `<FORM格式文本>`：展开后即目标 `$` 标签名。
```erb
TRYCGOTOFORM LABEL_{SELECTCOM}
  ;标签存在时执行到这里后跳到 ENDCATCH 之后
CATCH
  PRINTL 展开后的标签不存在，进入失败处理
ENDCATCH
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:361` → `new GOTO_Instruction(true, true, true), EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3746`（`GOTO_Instruction`，form=true, isTry=true, isTryCatch=true）

```text
构造（GOTO_Instruction(form=true, isTry=true, isTryCatch=true)）:
    参数构造器 = SP_CALLFORM（FORM 格式字符串实参）
    flag = METHOD_SAFE | FLOW_CONTROL | FORCE_SETARG | IS_TRY | IS_TRYC | PARTIAL
    # IS_TRYC | PARTIAL 使解析器把本行（TRYC 块首行）纳入 TRYC～CATCH～ENDCATCH 结构，
    # 并填好 func.JumpToEndCatch = 配对的 CATCH 行

静态解析阶段 SetJumpTo():
    仅当实参恰为编译期常量字符串时做静态标签解析（同 TRYGOTOFORM）；
    FORM 文本含插值时不是常量，静态阶段不做任何事。

运行期 DoInstruction():
    若实参是常量:
        若 func.JumpTo != null → jumpto = func.JumpTo
        否则 → 返回（标签不存在）
    否则（FORM 文本）:
        label = FORM 文本求值展开得到的字符串
        jumpto = state.CurrentCalled.CallLabel(按名查找)
    若 jumpto == null:
        # TRYCGOTOFORM：isTry = true，不抛"标签未定义"
        若 func.JumpToEndCatch != null（本行即 TRYC 块首行，= 配对的 CATCH 行）:
            state.JumpTo(func.JumpToEndCatch)   # 失败 → 跳到 CATCH 行；JumpTo 的落点是目标行的下一行，
                                                # 故实际从 CATCH 的下一行开始执行失败处理分支
        返回
    否则若 jumpto.IsError → 抛 CodeEE("非法标签名")
    否则 state.JumpTo(jumpto)
```

## 备注

- ecd 文档对 TRYCGOTOFORM 仅一句话"与 TRYGOTOFORM 对应的 TRYC 系版本"，行为细节需结合 TRYC 系总述与 TRYGOTOFORM 小节；示例为补写。
- zh 文档未收录本命令。
