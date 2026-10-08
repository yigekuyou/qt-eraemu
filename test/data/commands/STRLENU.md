# STRLENU

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：STRLENU `<文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### STRLENU <文本>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

`STRLEN` 的 Unicode 版：测量字符串长度并赋值给 `RESULT:0`。区别在于全角字符也按 1 个字符计算，即按 Unicode 字符数而非 Shift-JIS 字节数计数。

## 用法

### STRLENU `<文本>`

- `<文本>`：字符串。
- 副作用：`RESULT:0` = Unicode 字符数。

```erb
STRLENU あいう   ;RESULT:0 = 3（全角也算 1）
STRLENU abc      ;RESULT:0 = 3
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:263`（`new STRLEN_Instruction(false, true)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:692`（类 `STRLEN_Instruction`，`argisform=false, unicode=true`）

```text
构造:
    argisform == false → ArgBuilder = STR_NULLABLE 参数解析器（参数可省略，缺省为 ""）

DoInstruction:
    str = 参数为常量 ? func.Argument.ConstStr : ((ExpressionArgument)func.Argument).Term.GetStrValue(exm)
    unicode == true → RESULT = str.Length    // 直接取 .NET 字符串长度（UTF-16 码元数）
```

## 备注

- 与 `STRLEN`、`STRLENFORM`、`STRLENFORMU` 共用类 `STRLEN_Instruction`，本命令为 `(false, true)` 组合。
- 实现中的 `str.Length` 是 .NET 字符串长度（UTF-16 码元数）；含代理对字符时一个字符会计 2，文档未涉及此细节。
- ecd 文档另列有 `STRLENSU`（字符串表达式版）；本仓库中它是式中函数（`Runtime/Script/Statements/Function/Creator.cs:114`，`StrlenuMethod`），不是命令。
- Era-Chinese-Documentation 套件未收录本命令。
