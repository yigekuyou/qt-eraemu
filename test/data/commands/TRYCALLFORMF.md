# TRYCALLFORMF

- **类别**：EE 扩展命令
- **签名**：
  - `TRYCALLFORMF <FORM格式文本>` (, 参数1, 参数2……)
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`（・TRYCALLFORMF 関数名，约 99 行）；`EmueraEE_readme (English).txt:70`；`EmueraEE_changelog.txt:162`。ecd `Command.md` 未单列本命令；形态参照其 `TRYCALLFORM`（Command.md:1930）与 `CALLFORMF` 小节；zh 套件未收录。

## 语义

`CALLFORMF` 的 TRY 版本：用 `PRINTFORM` 风格的格式化字符串指定**式中函数**名并调用（返回值被丢弃，与 `CALLF`/`CALLFORMF` 相同），函数不存在时不报错、静默跳过。与 `TRYCALLF` 的唯一区别是函数名可以含 `{……}` 展开，运行时才确定。

EE readme 补充：与 `TRYCALLF` 一样，它不触发 `TRYC`～`CATCH`（TRYC 系没有 TRYCALLFORMF 变体），需要条件判断时配合 `EXISTFUNCTION` 使用。

## 用法

### `TRYCALLFORMF <FORM格式文本>{, <参数>……}`
- `<FORM格式文本>`：格式化字符串形式的函数名，如 `"COM_ABLE{X}"`。
- `<参数>`（可省略）：与 `CALLFORMF` 相同的实参格式。
```erb
X = 300
;尝试调用 COM_ABLE300，不存在也不报错
TRYCALLFORMF COM_ABLE{X}
;也可以整体拼出来
TRYCALLFORMF @"PROCESS_%TOSTR(MODE)", FLAG:0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:368`（enum `TRYCALLFORMF`）；`Runtime/Script/Statements/FunctionIdentifier.cs:415`（`addFunction(FunctionCode.TRYCALLFORMF, new TRYCALLF_Instruction(true))`）
- 实现：与 `TRYCALLF` 共用同一个指令类——`Runtime/Script/Statements/Instraction.Child.cs:1284-1339`（`TRYCALLF_Instruction`，构造参数 `form=true` 时参数构造器取 `SP_CALLFORMF`，见 `Runtime/Script/Statements/ArgumentBuilder.cs:204`）

```text
构造(form = true):
    ArgBuilder = SP_CALLFORMF 参数构造器      # <書式付文字列>,<引数>...（参数可省略）
    flag = EXTENDED | METHOD_SAFE | FORCE_SETARG

SetJumpTo(...):
    参数非常量 → useCallForm = true（运行时解析）
    常量时解析函数，失败/为 null 均静默返回

DoInstruction(exm, func, state):
    若（参数非常量）或（RunERBFromMemory）:
        labelName = 参数.FuncnameTerm.GetStrValue(exm)   # 先展开 {……} 得到函数名
        mToken = GetFunctionMethod(标签字典, labelName, 实参, true)
    否则:
        用预编译期解析好的 FuncTerm
    若 mToken == null: 返回          # 函数不存在 → 静默跳过
    mToken.GetValue(exm)             # 执行，返回值丢弃
```

## 备注

- 与 `TRYCALLF` 共享实现，仅参数形态不同（普通字符串名 vs FORM 格式串）；语义差异与 `CALLF`/`CALLFORMF` 的关系一致。
- ecd 文档未收录本命令；`TRYCALLFORM`（普通函数版，本仓库有实现）在 ecd Command.md:1930 有小节，可作为形态参照。
- 本仓库注册位置 `Runtime/Script/Statements/FunctionIdentifier.cs:415` 紧跟 `TRYCALLF`（:414），实现为同一类的两个实例。
