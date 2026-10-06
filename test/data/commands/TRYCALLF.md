# TRYCALLF

- **类别**：EE 扩展命令
- **签名**：
  - `TRYCALLF <函数名>` (, 参数1, 参数2……)
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`（・TRYCALLF 関数名，约 96 行）；`EmueraEE_readme (English).txt:67`；`EmueraEE_changelog.txt:162`。ecd `Command.md` 未单列 TRYCALLF 小节，相关背景见其「CALL·JUMP·GOTO系」小节（`TRYCALLFORM`（Command.md:1930）与 `CALLF`/`CALLFORMF`（Command.md:1950 前后））；zh 套件未收录。

## 语义

`CALLF` 的 TRY 版本：以「忽略返回值」的方式调用一个**式中函数**（与 `CALLF` 相同，返回值被丢弃），区别在于**函数不存在时不会报错**，而是静默跳过。函数存在时正常执行并丢弃返回值；参数按普通函数的实参格式传入。

EE readme 补充：若想配合 `TRYC`～`CATCH` 语法使用（函数不存在时走 CATCH 分支），应改用 `EXISTFUNCTION("函数名")` 判断——`TRYCALLF` 本身不触发 CATCH（它不参与 TRYC 的错误捕获机制）。

## 用法

### `TRYCALLF <函数名>{, <参数>……}`
- `<函数名>`：被调用的式中函数名（直接写名字，非格式化字符串；格式化字符串版是 `TRYCALLFORMF`）。
- `<参数>`（可省略）：与 `CALLF` 相同的实参格式，可传多个。
```erb
;函数存在时执行，不存在时什么都不发生
TRYCALLF PRINT_ADDITIONAL_INFO
;带参数
TRYCALLF SOME_FUNC, X, Y
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:367`（enum `TRYCALLF`）；`Runtime/Script/Statements/FunctionIdentifier.cs:414`（`addFunction(FunctionCode.TRYCALLF, new TRYCALLF_Instruction(false))`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1284-1339`（`TRYCALLF_Instruction`，`#region EE_TRYCALLF`；`TRYCALLFORMF` 复用同一类，构造参数 `form=true`）

```text
构造(form = false):
    ArgBuilder = SP_CALLF 参数构造器          # ArgumentBuilder.cs:202，
                                              # SP_CALL_ArgumentBuilder：函数名 + 可选实参
    flag = EXTENDED | METHOD_SAFE | FORCE_SETARG

SetJumpTo(...)（解析/预编译期）:
    若参数非常量: useCallForm = true; 返回
    否则尝试 GetFunctionMethod(标签字典, 常量函数名, 实参, true) 解析出 FuncTerm；
    任何异常或解析结果为 null 都直接返回（不报错）→ 相当于「函数不存在」被吞掉

DoInstruction(exm, func, state):
    若（参数非常量）或（RunERBFromMemory）:
        labelName = 参数.FuncnameTerm.GetStrValue(exm)   # 运行时求函数名
        mToken = GetFunctionMethod(标签字典, labelName, 实参, true)
    否则:
        labelName = 常量函数名
        mToken = 预编译期解析好的 FuncTerm
    若 mToken == null: 返回          # ★ 函数不存在 → 静默跳过，这是 TRY 语义
    mToken.GetValue(exm)             # 执行式中函数；返回值无人接收 = 破棄
```

## 备注

- 与 `CALLF` 的差异仅两点：函数不存在不报错；以及「调用目标必须是式中函数」的解析失败被容忍。返回值同样被丢弃。
- ecd 文档没有 TRYCALLF/TRYCALLFORMF 的独立小节（它是 EM/EE 追加命令，不在原版 Emuera 文档内）；语义以 EE readme 为准，与源码一致。
- readme 提到「TRYC～CATCH式に使う場合はEXISTFUNCTIONと併用してください」：源码中 `TRYCALLF_Instruction` 没有设置 TRYC 的 `FunctionoNotFoundName` 跳转目标，所以 `TRYCCALLF` 形态不存在/无效，需手动用 `EXISTFUNCTION` 分支。
