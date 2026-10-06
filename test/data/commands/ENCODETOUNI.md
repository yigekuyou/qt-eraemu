# ENCODETOUNI

- **类别**：命令（另有同名式中函数，签名不同，见备注）
- **签名**：`ENCODETOUNI <对象字符串(FORM格式字符串)>`
- **文档来源**：`ecd/docs/translation/Command.md`（「字符串操作·引用」节 `### ENCODETOUNI`）；`Era-Chinese-Documentation` 未收录。

## 语义

把给定的字符串逐字符转换为 Unicode 码点数值，依次写入 `RESULT` 数组。`RESULT:0` 为字符数，`RESULT:1` 起依次为各字符的码点值。参数是 FORM 格式字符串，会先做文本展开。字符串长度超过 `RESULT` 数组可用长度（数组长度减 1）时抛错。索引处在 `METHOD_SAFE | EXTENDED` 上下文，注册处注明「式中函数版追加。处理完全不同」——命令版与函数版行为完全不同。

## 用法

### `ENCODETOUNI <对象字符串>`

- `<对象字符串>`：FORM 格式字符串表达式，要编码的文本。

```erb
ENCODETOUNI "あA"
;RESULT:0 = 2（字符数）
;RESULT:1 = 12354（'あ' 的码点 0x3042）
;RESULT:2 = 65（'A' 的码点）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:404` → `argb[FunctionArgType.FORM_STR_NULLABLE], METHOD_SAFE | EXTENDED`
- 实现：`Runtime/Script/Process.ScriptProc.cs:727`（`case FunctionCode.ENCODETOUNI`）；结果写入在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1738`（`SetEncodingResult`）

```text
命令执行:
    term = 本行参数（FORM 字符串表达式）
    target = term.GetStrValue(exm)          ;先展开 FORM 文本

    length = RESULT_ARRAY 的长度
    若 target.Length > length - 1:
        抛 CodeEE（tooLongEncodetouniArg：
                  字符串长度 X 超过可用长度 length-1）
        （RESULT:0 要占一个位置，所以可用空间是 length-1）

    ary = 长度为 target.Length 的 int 数组
    对 i = 0 .. target.Length-1:
        ary[i] = char.ConvertToUtf32(target, i)  ;第 i 个字符的码点
                                                  ;（正确处理代理对）
    vEvaluator.SetEncodingResult(ary):
        RESULT:0 = ary.Length        ;字符数
        RESULT:1..N = ary[0..N-1]    ;各字符码点
```

## 备注

- **文档与源码表述差异**：ecd 文档写「各**字节**的数值」，但实现存的是 `char.ConvertToUtf32` 得到的 Unicode **码点**（UTF-32 字符值，非 UTF-8/16 字节），且实现中被注释掉的代码正是早期按「字节」处理的 UTF32 编码版本。应以源码为准：存码点。
- 本仓库另有**式中函数**版 `ENCODETOUNI(字符串[, 位置])`：返回指定位置单个字符的码点（位置省略为 0；空串返回 -1；位置为负或超出字符串长度抛错），注册在 `Runtime/Script/Statements/Function/Creator.cs:133`，实现在 `Runtime/Script/Statements/Function/Creator.Method.cs:4809`（`EncodeToUniMethod`），二者同名不同义。
