# CONVERT

- **类别**：式中函数
- **签名**：str CONVERT(int value, ※)（第 2 参数只能取 2 / 8 / 10 / 16）
- **文档来源**：`ecd/Expression.md`「内置表达式内函数一览」`str CONVERT(int value, ※)`；`ecd/Error_Index.md` 收录其错误消息；`ecd/Command.md` 无独立小节；zh 套件未收录

## 语义

把整数值按指定进制转换为字符串并返回，相当于 .NET 的 `Convert.ToString(long, int)`。第 1 参数是要转换的数值，第 2 参数是目标进制，只能是 2（二进制）、8（八进制）、10（十进制）、16（十六进制）之一，否则报错。

错误行为：
- 第 2 参数不是 2/8/10/16：抛出 CodeEE「CONVERT関数: 第2引数は2, 8, 10, 16のいずれかでなければなりません」。

返回值细节（来自 .NET 标准库行为）：
- 十六进制使用小写字母 `a`～`f`。
- 进制为 10 时负数带 `-` 号；进制为 2/8/16 时负数按 64 位补码的无符号形式输出，不带负号。

## 用法

### str CONVERT(int value, int toBase)
- `value`：要转换的整数。
- `toBase`：目标进制，2 / 8 / 10 / 16 之一。
- 返回值：按该进制表示的数字字符串。
```erb
PRINTFORML 2進 = {CONVERT(100, 2)}   ; 1100100
PRINTFORML 8進 = {CONVERT(100, 8)}   ; 144
PRINTL 10進 = %CONVERT(100, 10)%     ; 100
PRINTFORML 16進 = {CONVERT(255, 16)} ; ff
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:130`（`["CONVERT"] = new ConvertIntMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4736`（`ConvertIntMethod`）

```text
ConvertIntMethod:
构造：返回类型 = string；参数类型 = [long, long]；CanRestructure = true。
GetStrValue(exm, args):
    toBase ← args[1].GetIntValue(exm)
    若 toBase ∉ {2, 8, 10, 16}:
        抛出 CodeEE(trerror.ArgShouldBeSpecificValue, Name, 2, "2, 8, 10, 16")
        ; "CONVERT関数: 第2引数は2, 8, 10, 16のいずれかでなければなりません"
    返回 System.Convert.ToString(args[0].GetIntValue(exm), (int)toBase)
```

## 备注

- ecd 文档只给了签名 `str CONVERT(int value, ※)`，没有语义说明段落；`※` 表示第 2 参数取值受限，具体约束来自源码与 `ecd/Error_Index.md:429`（错误消息条目，出处标注为本文件所在源码）。
- 负数在 2/8/16 进制下输出 64 位补码形式（如 `CONVERT(-1, 16)` 得到 64 个 `f`），文档未提及，来自 .NET 行为。
