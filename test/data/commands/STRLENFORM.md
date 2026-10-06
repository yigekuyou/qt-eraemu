# STRLENFORM

- **类别**：命令
- **签名**：STRLENFORM `<FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### STRLENFORM <FORM格式文本>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

`STRLEN` 的 FORM 格式文本版：先展开参数中的 FORM 语法（`%表达式%`、`{数值表达式}`、`#对齐#` 等），再以 Shift-JIS 的字节数测量展开后的字符串长度，赋值给 `RESULT:0`（全角算 2、半角算 1）。

## 用法

### STRLENFORM `<FORM格式文本>`

- `<FORM格式文本>`：可含 FORM 语法的文本。
- 副作用：`RESULT:0` = 展开后字符串的字节长度。

```erb
STRLENFORM 名字是%CALLNAME:MASTER%   ;RESULT:0 = 展开后字符串的 S-JIS 字节数
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:262`（`new STRLEN_Instruction(true, false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:692`（类 `STRLEN_Instruction`，`argisform=true, unicode=false`）；字节长度计算在 `Runtime/Utils/LangManager.cs:44`（`LangManager.GetStrlenLang`）

```text
构造:
    argisform == true → ArgBuilder = FORM_STR_NULLABLE 参数解析器
                        // 解析期用 LexicalAnalyzer.AnalyseFormattedString 解析 FORM 语法并生成求值项

DoInstruction:
    str = 参数为常量 ? func.Argument.ConstStr : ((ExpressionArgument)func.Argument).Term.GetStrValue(exm)
          // 非 STRINGFORM 常量时，GetStrValue 内部完成 FORM 展开
    若 unicode: RESULT = str.Length
    否则:       RESULT = LangManager.GetStrlenLang(str)   // 纯 ASCII 取字符数，否则按语言编码计字节
```

## 备注

- 与 `STRLEN`、`STRLENU`、`STRLENFORMU` 共用类 `STRLEN_Instruction`，由 `(argisform, unicode)` 区分：本命令为 `(true, false)`。
- 文档称「以 Shift-JIS 的字节数测量」；本仓库 `GetStrlenLang` 实为语言相关字节计数（见 STRLEN.md 备注），已并列记录。
- Era-Chinese-Documentation 套件未收录本命令。
