# ENDCATCH

- **类别**：命令
- **签名**：`ENDCATCH`
- **文档来源**：`ecd/docs/translation/Command.md`（「CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)」节 `### ENDCATCH`）；`Era-Chinese-Documentation` 未收录（zh 各文档 grep 无 ENDCATCH 条目）。

## 语义

结束由 `CATCH` 开始的分支。`CATCH`～`ENDCATCH` 与 `TRYC` 系调用指令（`TRYCCALL`、`TRYCJUMP`、`TRYCGOTO`、`TRYCCALLFORM`、`TRYCJUMPFORM`、`TRYCGOTOFORM`）配合使用：`TRYC` 系函数/标签调用失败（不存在）时转入 `CATCH` 分支，`ENDCATCH` 是该分支的终点。整体语法与 `IF`～`ELSE`～`ENDIF` 类似，可以嵌套。运行期 `ENDCATCH` 本身什么也不做，仅作为结构终点；真正的跳转关系在装载期建立（`CATCH` 与 `TRYC` 行都会记住 `ENDCATCH` 的位置）。

## 用法

### `ENDCATCH`

无参数，必须紧跟 `CATCH` 分支的代码块之后。

```erb
TRYCCALL UNKNOWN_FUNC   ;不存在的函数
    PRINTL 函数存在时执行这里
CATCH
    PRINTL 函数不存在时执行这里
ENDCATCH
PRINTL 两种情况之后都继续这里
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:365` → `new ENDIF_Instruction(), METHOD_SAFE | EXTENDED`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:3204`（`ENDIF_Instruction`，空操作）；装载期链接在 `Runtime/Script/Loader/ErbLoader.cs:1242`（`case FunctionCode.CATCH`）与 `:1259`（`case FunctionCode.ENDCATCH`）

```text
装载期（ErbLoader）:
    遇到 CATCH:
        栈顶必须是 TRYC 系指令（TRYCCALL 等 6 种），否则警告
        弹出 TRYC 行，TRYC 行.JumpToEndCatch = CATCH 行
        （TRYC 调用失败时跳到 CATCH）
        把 CATCH 行压回栈
    遇到 ENDCATCH:
        栈顶必须是 CATCH 行，否则警告（UnexpectedEndcatch）
        弹出 CATCH 行，CATCH 行.JumpToEndCatch = ENDCATCH 行
        （顺序执行落入 CATCH 时，CATCH 的运行期动作就是
          无条件跳到 ENDCATCH，从而跳过整个 CATCH 分支体）

运行期（ENDIF_Instruction.DoInstruction）:
    空方法体：什么都不做，顺序流自然通过 ENDCATCH 继续执行下一行
```

## 备注

- `ENDCATCH` 复用 `ENDIF_Instruction`（与 `ENDIF`、`ENDSELECT`、`DO`、`ENDFUNC` 同类；类自身带 `FLOW_CONTROL | PARTIAL | FORCE_SETARG`，注册 `ENDCATCH` 时再附加 `METHOD_SAFE | EXTENDED`），`DoInstruction` 为空方法体，运行期为空操作——这与 ecd 文档「跳到 `ENDCATCH` 的下一行继续」的描述一致（落地行为由 `JumpTo` 的使用方决定，`ENDCATCH` 自己不跳转）。
- 用 `GOTO` 直接跳入 TRYC～ENDCATCH 区间内时，会顺序执行到 `CATCH` 或 `ENDCATCH` 之前，然后跳到 `ENDCATCH` 下一行（因为 CATCH/ENDIF 类行的运行期动作即跳转），与 IF 系行为相同。
- `TRYC` 系指令也把 `JumpToEndCatch` 设为 CATCH 行（ErbLoader.cs:1256），用于调用失败时进入 CATCH 分支。
