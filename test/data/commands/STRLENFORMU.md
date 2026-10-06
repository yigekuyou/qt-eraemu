# STRLENFORMU

- **类别**：命令
- **签名**：STRLENFORMU `<FORM格式文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### STRLENFORMU <FORM格式文本>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

`STRLENFORM` 的 Unicode 版：先展开参数中的 FORM 语法，再按 Unicode 字符数测量长度并赋值给 `RESULT:0`。区别在于全角字符也按 1 个字符计算。

## 用法

### STRLENFORMU `<FORM格式文本>`

- `<FORM格式文本>`：可含 FORM 语法的文本。
- 副作用：`RESULT:0` = 展开后字符串的 Unicode 字符数。

```erb
STRLENFORMU 名字是%CALLNAME:MASTER%   ;RESULT:0 = 展开后字符串的字符数（全角也算 1）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:264`（`new STRLEN_Instruction(true, true)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:692`（类 `STRLEN_Instruction`，`argisform=true, unicode=true`）

```text
构造:
    argisform == true → ArgBuilder = FORM_STR_NULLABLE 参数解析器（解析期处理 FORM 语法）

DoInstruction:
    str = 参数为常量 ? func.Argument.ConstStr : ((ExpressionArgument)func.Argument).Term.GetStrValue(exm)
    unicode == true → RESULT = str.Length    // 直接取 .NET 字符串长度（UTF-16 码元数）
```

## 备注

- 与 `STRLEN`、`STRLENFORM`、`STRLENU` 共用类 `STRLEN_Instruction`，本命令为 `(true, true)` 组合。
- 实现中的 `str.Length` 是 .NET 字符串长度（UTF-16 码元数）；对 BMP 内字符与「字符数」一致，含代理对（如某些 emoji）时一个字符会计 2。文档未涉及此细节，照实补记。
- Era-Chinese-Documentation 套件未收录本命令。
