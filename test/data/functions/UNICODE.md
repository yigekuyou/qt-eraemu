# UNICODE

- **类别**：式中函数
- **签名**：
  - `str UNICODE(int value)`
- **文档来源**：`ecd/docs/translation/Command.md`「### UNICODE `<数值表达式>`」、`ecd/Expression.md` 函数目录（`str UNICODE(int value)`）；zh 套件未收录该函数小节

## 语义

把与参数值对应的 Unicode 字符作为字符串返回（例如 `UNICODE(0x2661)` 得到空心爱心「♡」）。参数必须落在 `0`～`0xFFFF` 范围内，越界抛错；该函数不能处理代理对（surrogate pair），字体不支持时也无法显示。

对控制字符有特殊处理：除换行（`0x000A`）与回车（`0x000D`）外，`0x0000`–`0x001F` 以及 C1 控制区 `0x007F`–`0x009F` 的值不报错而是给出警告（在控制台输出「注意:文件名·行号」形式或解析警告），并返回空字符串。

## 用法

### `str UNICODE(int value)`
```erb
UNICODE 0x2661 之类的值可用在表达式中：
PRINTFORMW %UNICODE(0x2661)%     ; 显示 ♡
A = UNICODE(65)                  ; A = "A"（用作字符串时）
PRINTFORML %UNICODE(0x41)%       ; 输出 A
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:128`（`["UNICODE"] = new UnicodeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4680`（`UnicodeMethod`）

```text
函数 UNICODE(i):
    若 i < 0 或 i > 0xFFFF:
        抛 CodeEE("UNICODE関数に範囲外の値(...)が渡されました")
        # （trerror.ArgIsOutOfRange：第1参数须在 0～0xFFFF 之间）
    若 (i < 0x001F 且 i != 0x000A 且 i != 0x000D) 或 (0x007F <= i <= 0x009F):
        # 除换行/回车外的控制字符 → 警告处理
        若当前正在执行脚本行（Process.getCurrentLine != null）:
            控制台输出系统行：
              "注意:<文件名>的<行号>行目でUNICODE関数に制御文字に対応する値(0x..)が渡されました"
        否则（解析期）:
            ParserMediator.Warn(同内容的警告, 级别1)
        返回 ""
    返回 由单个字符 (char)i 构成的字符串
```

## 备注

- 文档与源码一致；文档还提醒「Emuera 对 Unicode 的支持并不完整，使用代理对时无法保证准确动作」——由于参数上限为 0xFFFF，本函数本身无法返回增补平面字符。
- 源码注释表示：控制字符本可作错误处理，现在降级为警告，且返回空字符串（此行为文档未记载）。
- 文档示例以指令式书写（`UNICODE 0x2661` + `PRINTFORMW %RESULTS%`），那是同名指令形态（结果进 `RESULTS:0`）；式中函数形态直接返回值，二者均存在。
- `CanRestructure = true`：常量参数在解析期折叠。
