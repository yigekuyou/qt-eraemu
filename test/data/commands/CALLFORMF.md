# CALLFORMF

- **类别**：命令
- **签名**：
  - `CALLFORMF <FORM格式文本>(, 参数1, 参数2……)`
- **文档来源**：`ecd/docs/translation/Command.md`「CALL·JUMP·GOTO 系」→「CALLFORMF `<FORM格式文本>` (, 参数1, 参数2……)」；`Era-Chinese-Documentation/docs/Custom_Expression.md`「`#FUNCTION(S)` 的调用」节（与 `CALLF` 并列提及）。

## 语义

`CALLF` 的带格式字符串版：以忽略返回值的方式调用用 `#FUNCTION`（式中函数）定义的函数，但函数名可以像 `PRINTFORM` 那样用带格式的字符串（`{表达式}`、`%字符串表达式%`）指定，先展开再调用。返回值被丢弃；参数书写格式与普通函数调用相同。目标函数不存在（或不是式中函数）时抛 CodeEE。

## 用法

### `CALLFORMF <FORM格式文本>(, 参数1, 参数2……)`
- `<FORM格式文本>`：格式字符串，展开后必须是已用 `#FUNCTION` 声明的函数名（不带 `@`）。
- `参数1, 参数2……`：按被调函数声明的形参传入。
```erb
CALLFORMF INIT_{GROUP_NAME}
;展开后等价于 CALLF INIT_GROUPA 之类
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:353`（`new CALLF_Instruction(true)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1153`（`CALLF_Instruction`，构造参数 `form=true`）

```text
指令类 CALLF_Instruction(form=true):
    form=true → 参数构造器 SP_CALLFORMF（函数名为 FORM 格式文本）
    flag = EXTENDED | METHOD_SAFE | FORCE_SETARG   # 无 FLOW_CONTROL

    SetJumpTo(...):
        若参数非常量（FORM 文本含插值）→ useCallForm = true（每次运行期解析）
        否则编译期解析 GetFunctionMethod 并缓存 FuncTerm；失败记警告

    DoInstruction(exm, func, state):    # 与 CALLF 共用执行逻辑
        若 (!参数为常量) 或 exm.Console.RunERBFromMemory:
            labelName = FuncnameTerm.GetStrValue(exm)   # 展开 {..} %..% 得到函数名
            mToken = IdentifierDictionary.GetFunctionMethod(
                        LabelDictionary, labelName, RowArgs, true)
        否则:
            mToken = 编译期缓存的 FuncTerm
        若 mToken == null:
            抛 CodeEE（"调用了不存在的式中函数" + labelName）
        mToken.GetValue(exm)            # 当场求值，返回值丢弃
```

## 备注

- 与 `CALLF` 是同一个 `CALLF_Instruction` 类的两个构造变体，仅参数构造器不同（`SP_CALLF` vs `SP_CALLFORMF`），完整执行逻辑见 `CALLF.md` 的伪代码。
- ecd 文档仅一句「`CALLF` 的带格式字符串版」，zh 文档仅在 `Custom_Expression.md` 并列提及；两者均未描述错误行为，错误语义以源码为准。
- 由于函数名每次都需展开并查表（FORM 非常量时无法编译期缓存），性能上比 `CALLF` 常量名形式多一次字符串格式化与函数解析。
