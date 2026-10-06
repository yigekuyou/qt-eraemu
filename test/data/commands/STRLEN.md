# STRLEN

- **类别**：命令
- **签名**：STRLEN `<文本>`
- **文档来源**：`ecd/docs/translation/Command.md`（`### STRLEN <文本>` 小节）；Era-Chinese-Documentation 未收录本命令（仅 `Header_File.md` 示例中出现用法 `X = STRLEN(HOGE)`）

## 语义

测量字符串长度并赋值给 `RESULT:0`。长度以 Shift-JIS 的字节数为准，即全角字符算 2、半角字符算 1。

参数缺失时按空字符串处理（长度 0），不报错。

## 用法

### STRLEN `<文本>`

- `<文本>`：字符串（按普通文本解析，也接受常量字符串）。
- 副作用：`RESULT:0` = 字节长度。

```erb
STRLEN abc       ;RESULT:0 = 3
STRLEN あいう    ;RESULT:0 = 6（全角按 2 字节计）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:261`（`new STRLEN_Instruction(false, false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:692`（类 `STRLEN_Instruction`，`argisform=false, unicode=false`）；字节长度计算在 `Runtime/Utils/LangManager.cs:44`（`LangManager.GetStrlenLang`）

```text
构造:
    argisform == false → ArgBuilder = STR_NULLABLE 参数解析器（参数可省略，缺省为 ""）

DoInstruction:
    str = 参数为常量 ? func.Argument.ConstStr : ((ExpressionArgument)func.Argument).Term.GetStrValue(exm)
    若 unicode: RESULT = str.Length                       // 字符数
    否则:       RESULT = LangManager.GetStrlenLang(str)   // 本实例：语言相关字节长

LangManager.GetStrlenLang(str):
    若 Ascii.IsValid(str) → str.Length                    // 纯 ASCII 快速路径（整串都是 ASCII 时）
    否则 → GetByteCountLang(str)                          // 逐字符累计字节数：
                                                          // 该字符在当前语言编码下能往返（编码后再解码等于原字符）时取其字节数，
                                                          // 否则回退按 Shift-JIS(932) 的字节数，两者都不成立时取当前语言编码的字节数
```

## 备注

- 本命令与 `STRLENFORM`／`STRLENU`／`STRLENFORMU` 共用类 `STRLEN_Instruction`，由 `(argisform, unicode)` 两个布尔参数区分（见 STRLENFORM.md、STRLENU.md、STRLENFORMU.md）。
- 文档称长度「以 Shift-JIS 的字节数为准」；本仓库实现改为「纯 ASCII 时取字符数，否则按当前语言编码逐字符计字节（该编码表示不了的字符回退 Shift-JIS 字节数）」，在日文环境下与 Shift-JIS 字节数等价，其他语言环境可能不同。已并列记录。
- ecd 文档另列有 `STRLENS`／`STRLENSU` 两个命令后缀；本仓库中它们不是命令，而是式中函数（`Runtime/Script/Statements/Function/Creator.cs:113-114`，`StrlenMethod`／`StrlenuMethod`，返回值语义相同）。
- Era-Chinese-Documentation 套件未收录本命令的语义小节。
