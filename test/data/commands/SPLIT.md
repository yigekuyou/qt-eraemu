# SPLIT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：SPLIT `<字符串表达式>`, `<字符串表达式>`, `<字符串变量>` {, `<数值变量>`}
- **文档来源**：`ecd/docs/translation/Command.md`（`### SPLIT <字符串表达式>, <字符串表达式>, <字符串变量>` 小节）；Era-Chinese-Documentation 未收录本命令

## 语义

以第 2 参数指定的字符串为分隔符，分割第 1 参数指定的字符串，并从数组起始处开始赋值给第 3 参数指定的字符串数组变量；同时把实际分割出的数量写入 `RESULT:0`（或省略的第 4 参数指定的数值变量）。

第 3 参数必须是数组变量（1～3 维均可）。分割后元素数超过目标数组可容纳数量时，超出部分不会被赋值，但 `RESULT` 中仍存入实际的分割数，请据此判断是否截断。

## 用法

### SPLIT `<字符串表达式>`, `<字符串表达式>`, `<字符串变量>`

- 第 1 参数：被分割的原始字符串。
- 第 2 参数：分隔符字符串（整个作为单个分隔符使用，不是字符集合，也不是正则）。
- 第 3 参数：接收结果的字符串数组变量（必须是数组变量）。
- 返回（副作用）：`RESULT:0` = 实际分割数。

```erb
SPLIT "あい,うえ,,お", ",", LOCALS
;LOCALS:0 = "あい"、LOCALS:1 = "うえ"、LOCALS:2 = ""（空字符串）、LOCALS:3 = "お"、RESULT:0 = 4
```

### SPLIT `<字符串表达式>`, `<字符串表达式>`, `<字符串变量>`, `<数值变量>`

本仓库扩展用法：第 4 参数为可省略的数值变量，用于替代 `RESULT:0` 接收分割数。

```erb
SPLIT "a,b,c", ",", LOCALS, COUNT
;COUNT = 3
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:269`（`argb[FunctionArgType.SP_SPLIT]`，METHOD_SAFE | EXTENDED）
- 实现：`Runtime/Script/Process.ScriptProc.cs:537`（`case FunctionCode.SPLIT`）；参数解析在 `Runtime/Script/Statements/ArgumentBuilder.cs:1978`（`SP_SPLIT_ArgumentBuilder`）

```text
参数解析（SP_SPLIT_ArgumentBuilder）:
    参数类型表 = [string, string, string, long]，最少 3 个
    第 3 参数必须是可赋值变量，且必须是 1～3 维数组变量，否则告警「不是数组变量」
    若给了第 4 参数：必须为可赋值数值变量，作为计数接收变量
    否则默认计数接收变量 = 系统变量 RESULT:0

case SPLIT:
    target = spSplitArg.TargetStr.GetStrValue(exm)          // 第 1 参数
    sep    = [ spSplitArg.Split.GetStrValue(exm) ]          // 第 2 参数，单元素分隔符数组
    retStr = target.Split(sep, StringSplitOptions.None)     // 保留空字段
    spSplitArg.Num.SetValue(retStr.Length, exm)             // 先写入实际分割数
    若 retStr.Length > 目标数组第 0 维长度:
        截断 retStr 到数组长度（原 throw 的错误检查已被注释掉，不再报错）
    spSplitArg.Var.SetValue(retStr, [0,0,0])                // 从数组原点开始整体赋值
```

## 备注

- **文档与源码差异**：ecd 文档只给出 3 参数签名；本仓库源码（`SP_SPLIT_ArgumentBuilder`，`minArg = 3`、类型表第 4 项 `long`）额外支持可省略的第 4 参数（数值变量）接收分割数。已并列记录。
- 源码中「分割数超过数组元素数」时原版会抛错的代码已被注释掉，行为变为静默截断，与文档「超出部分不会被赋值」一致。
- 分隔符按普通字符串整体匹配（`String.Split`），与 `REPLACE` 不同，不涉及正则。
- Era-Chinese-Documentation 套件未收录本命令。
