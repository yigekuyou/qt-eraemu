# UNICODEBYTE

- **类别**：式中函数
- **签名**：
  - `int UNICODEBYTE(str value)`
- **文档来源**：**ecd 与 zh 套件均未收录**（`ecd/Expression.md` 函数目录、`ecd/Command.md` 中都没有该函数小节）；以下语义完全依据源码实现（与 Emuera 官方 wiki 的说明一致：返回字符串第一个字符的 Unicode 码位数值）

## 语义

返回参数字符串第一个字符的 Unicode 码位（code point）数值。实现上先把整个字符串按 UTF-32 编码，再取开头 4 个字节解释为整数——因此对于 BMP 字符返回其码位本身；若首字符是代理对（增补平面字符），UTF-32 编码会把它合成一个码位一并返回。传空字符串时因无字节可取而抛参数异常。

它可看作 `UNICODE`（码位→字符）的逆操作之一。

## 用法

### `int UNICODEBYTE(str value)`
```erb
A = UNICODEBYTE("A")     ; A = 65
B = UNICODEBYTE("あ")    ; B = 0x3042 (12354)
C = UNICODEBYTE(UNICODE(0x2661))  ; C = 0x2661（UNICODE 的逆操作）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:129`（`["UNICODEBYTE"] = new UnicodeByteMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4716`（`UnicodeByteMethod`）

```text
函数 UNICODEBYTE(s):
    target = 第1参数字符串
    length = Encoding.UTF32.GetEncoder().GetByteCount(target 的字符, 0, target.Length, flush=false)
    bytes = 新建 byte[length]
    Encoding.UTF32.GetEncoder().GetBytes(target 的字符, 0, target.Length, bytes, 0, flush=false)
    返回 BitConverter.ToInt32(bytes, 0)
        # 取 UTF-32 字节流的开头 4 字节 = 第 1 个字符的码位（小端）
        # 若 s 为空串，bytes 长度为 0，BitConverter.ToInt32 抛 ArgumentException（数组长度不足 4）
```

## 备注

- **文档未收录**：`ecd/Expression.md` 只列了 `UNICODE` 与 `ENCODETOUNI`，没有 `UNICODEBYTE`；zh 套件同样没有。本条目的语义、行为与错误情况均由源码推导。
- 源码把**整个字符串**都编码成 UTF-32，但只取首 4 字节——第 2 个及之后的字符实际被丢弃（只是浪费了一次编码）。
- 空字符串会抛 `ArgumentException`（`BitConverter.ToInt32` 要求至少 4 字节），而非返回 0 或抛 `CodeEE`——这是实现上的粗疏之处。
- `CanRestructure = true`：常量参数在解析期折叠。
