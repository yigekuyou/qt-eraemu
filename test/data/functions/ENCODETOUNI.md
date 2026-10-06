# ENCODETOUNI

- **类别**：式中函数（另有同名命令形态）
- **签名**：
  - int ENCODETOUNI(str value, int position = 0)
  - 命令形态：ENCODETOUNI `<对象字符串(FORM格式字符串)>`
- **文档来源**：`ecd/Command.md`「### ENCODETOUNI `<对象字符串(FORM格式字符串)>`」小节（命令形态）；`ecd/Expression.md`「内置表达式内函数一览」`int ENCODETOUNI(str value, int position = 0)`（式中函数形态）；zh 套件未收录

## 语义

与 Unicode 编码有关的函数有两种形态：

1. **式中函数形态**（本文档主体）：返回字符串 `value` 中第 `position`（默认 0）个字符的 Unicode 码位（UTF-32 码元值）。字符串为空时返回 `-1`。
2. **命令形态**：`ENCODETOUNI <FORM格式字符串>` 把整串字符串逐字符编码为 Unicode 码位，存入 `RESULT` 数组——`RESULT:0` 为字符数，`RESULT:1` 起为各字符的码位值。

错误行为（式中函数形态）：
- 第 2 参数 `position` 为负：抛出 CodeEE「{0}関数: 第{1}引数に負の値({2})が指定されました」。
- `position` 不小于第 1 参数字符串的字符数：抛出 CodeEE「{0}関数: 第2引数({1})が第1引数の文字列({2})の文字数を超えています」。

命令形态下字符串长度超过 `RESULT` 数组容量（`RESULT_ARRAY.Length - 1`）时抛出 CodeEE「ENCODETOUNIの引数が長すぎます（現在{0}文字。最大{1}文字まで）」。

## 用法

### int ENCODETOUNI(str value, int position = 0)
- `value`：被检索的字符串。
- `position`：字符位置，默认 0（首字符）。
- 返回值：该位置字符的 Unicode 码位（如「あ」= 0x3042 = 12354）；空字符串返回 `-1`。
```erb
PRINTV ENCODETOUNI("あ")          ; 输出 12354
PRINTV ENCODETOUNI("AあB", 1)     ; 输出 12354（第 1 个字符，从 0 计）
PRINTV UNICODE(ENCODETOUNI("♡"))  ; 与 UNICODE 互逆，输出 ♡
```

### 命令形态 ENCODETOUNI `<对象字符串(FORM格式字符串)>`
- 结果写入 `RESULT:0`（字符数）与 `RESULT:1` 起（各字符码位）。
```erb
ENCODETOUNI あいう
PRINTFORML 字符数={RESULT:0} 首码位={RESULT:1}
```

## 源码实现（emuera.em/Emuera）

- 注册（式中函数）：`Runtime/Script/Statements/Function/Creator.cs:133`（`["ENCODETOUNI"] = new EncodeToUniMethod()`）
- 实现（式中函数）：`Runtime/Script/Statements/Function/Creator.Method.cs:4809`（`EncodeToUniMethod`）
- 注册（命令形态/标识符）：`Runtime/Script/Statements/FunctionIdentifier.cs:404`（`addFunction(FunctionCode.ENCODETOUNI, argb[FunctionArgType.FORM_STR_NULLABLE], METHOD_SAFE | EXTENDED)`，注释「式中関数版を追加。処理が全然違う」）；枚举 `Runtime/Script/Statements/BuiltInFunctionCode.cs:257`
- 实现（命令形态）：`Runtime/Script/Process.ScriptProc.cs:727`（`case FunctionCode.ENCODETOUNI`）

```text
EncodeToUniMethod（式中函数形态）:
构造：返回类型 = long；参数表 = [str, int]（第 2 参数可省略，省略时视为 0）；
     CanRestructure = true。
GetIntValue(exm, args):
    baseStr ← args[0].GetStrValue(exm)
    若 baseStr.Length == 0: 返回 -1
    position ← (args.Count > 1 且 args[1] != null) ? args[1].GetIntValue(exm) : 0
    若 position < 0:
        抛出 CodeEE("{0}関数: 第{1}引数に負の値({2})が指定されました")
    若 position >= baseStr.Length:
        抛出 CodeEE("{0}関数: 第2引数({1})が第1引数の文字列({2})の文字数を超えています")
    返回 char.ConvertToUtf32(baseStr, (int)position)

case FunctionCode.ENCODETOUNI（命令形态）:
    target ← 参数表达式求得的字符串
    若 target.Length > RESULT_ARRAY.Length - 1:
        抛出 CodeEE("ENCODETOUNIの引数が長すぎます（現在{0}文字。最大{1}文字まで）")
    ary ← 长度为 target.Length 的 int 数组
    对 i ∈ [0, target.Length): ary[i] ← char.ConvertToUtf32(target, i)
    vEvaluator.SetEncodingResult(ary)   ; RESULT:0 = 字符数，RESULT:1.. = 各码位
```

## 备注

- 两种形态共用 `FunctionCode.ENCODETOUNI` 但处理完全不同：独立一行调用时走命令形态（填 RESULT 数组），出现在表达式中时走式中函数形态（返回单个码位）。FunctionIdentifier.cs:404 的注释「式中関数版を追加。処理が全然違う」也说明两者是刻意分开的。
- ecd/Command.md 说命令形态把「其字节作为数值返回」（RESULT:1 起为「各字节的数值」），但源码实际写入的是**Unicode 码位**（`char.ConvertToUtf32`，32 位码元值），并非 UTF-8/UTF-32 字节序列；文档措辞不准确。
- ecd/Command.md 未收录式中函数形态（`int ENCODETOUNI(str, int = 0)`）；Expression.md 未收录命令形态。
- 空字符串在式中函数形态下返回 -1，不会报错；命令形态下空串则 RESULT:0 = 0。
