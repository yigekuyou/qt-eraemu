# TOSTR

- **类别**：式中函数
- **签名**：
  - `str TOSTR(int value, str format = "")`
- **文档来源**：`ecd/docs/translation/Command.md`「### TOSTR `<数值表达式>`, `<格式指示符>`」、`ecd/Expression.md` 函数目录（`str TOSTR(int value, str format = "")`）；zh 套件未收录该函数小节（仅在 `zh/Function_and_Preprocessor.md` 提及可用 TOSTR 包裹数字参数）

## 语义

把数值转换为字符串并返回。第 1 参数是要转换的数值，第 2 参数是字符串形式的转换格式（即 C# `Int64.ToString(string)` 的格式指示符，如 `X`、`000`、`D5` 等）。第 2 参数可省略，省略（或为空）时与 `PRINTFORM` 的 `{}` 内一样转为普通十进制字符串。格式指示符不合适时抛错终止。与指令版 `TOSTR`（结果写入 `RESULTS:0`）不同，本形态在表达式中直接返回字符串。

## 用法

### `str TOSTR(int value, str format = "")`
```erb
STR:0 = TOSTR(255, "X")     ; STR:0 = "FF"
STR:0 = TOSTR(7, "000")     ; STR:0 = "007"
STR:0 = TOSTR(123)          ; STR:0 = "123"（省略格式）
PRINTFORML %TOSTR(5, "D3")% ; 输出 005
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:120`（`["TOSTR"] = new ToStrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4449`（`ToStrMethod`）

```text
函数 TOSTR(i, format?):
    i = 第1参数的整数值
    若没有第2参数或第2参数为 null:
        返回 i.ToString()                 # 普通十进制字符串
    format = 第2参数字符串
    try:
        返回 i.ToString(format)           # C# Int64.ToString 的标准数字格式
    catch FormatException:
        抛 CodeEE("TOSTR関数の書式指定が間違っています")
        # （实际为 trerror.InvalidFormat："{0}関数の{1}番目の引数に適切でない書式が指定されました"）
```

## 备注

- 文档明确「内部调用的是 C# 的 `Int64.ToString()`，第 2 参数不合适时会出错」，与源码一致；错误类型为编译期/执行期的 `CodeEE`。
- 参数表用 `argumentTypeArrayEx` 声明：`(Int, String)` 且从第 2 个起可省略（`OmitStart = 1`）。
- `CanRestructure = true`：常量参数在解析期折叠为常量字符串。
- ecd 文档小节在「字符串操作·引用」组下；Expression.md 将其归入函数目录，两者签名一致。
