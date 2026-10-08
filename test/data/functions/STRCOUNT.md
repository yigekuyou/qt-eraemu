# STRCOUNT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：
  - int STRCOUNT(str input, str match)
- **文档来源**：`ecd/docs/translation/Command.md`「STRCOUNT `<检索对象字符串>`, `<检索字符串>`」小节、`ecd/docs/translation/Expression.md` 内置函数目录「int STRCOUNT(str input, str match)」；zh 套件未收录

## 语义

返回字符串 input 中 match（正则表达式）匹配出现的次数。查找格式遵循 C# 的正则表达式规范。

第 2 参数不是合法正则时抛出运行时错误（CodeEE），错误信息含正则引擎的具体报错。

ecd 的 Command.md 小节以旧式口吻描述为「获取字符串中指定子串出现的次数，并把命中次数赋值给 RESULT:0」；在本仓库中它注册为式中函数，返回值直接参与表达式，不经过 RESULT。

## 用法

### STRCOUNT(input, match)
- input：被检索的字符串表达式。
- match：C# 正则表达式字符串。
```erb
;统计 "a" 出现次数（正则匹配，区分大小写）
N = STRCOUNT("banana", "a")        ;N = 3
;正则示例：统计连续数字串的个数
M = STRCOUNT("ab12cd345ef", "[0-9]+")   ;M = 2
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:119`（`["STRCOUNT"] = new StrCountMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4425`（`private sealed class StrCountMethod : FunctionMethod`，`GetIntValue`）；辅助：`Runtime/Utils/RegexFactory.cs`（GetRegex，正则缓存）

```text
函数 STRCOUNT(参数表):
    尝试:
        reg <- RegexFactory.GetRegex(参数[1] 的字符串值)
            # 正则按模式字符串缓存（ConcurrentDictionary），缓存未命中时
            # new Regex(模式, RegexOptions.Compiled) 编译并登记
    捕获 ArgumentException e:
        抛出 CodeEE（"{0}関数:第{1}引数が正規表現として不正です:{2}"）
    返回 reg.Matches(参数[0] 的字符串值).Count
        # 对 input 求所有匹配并返回匹配个数（可重叠计数为不重叠匹配次数）
```

## 备注

- 与同名命令的关系：ERB 语句层面没有独立的 STRCOUNT 命令，它是纯式中函数；但 ecd Command.md 的小节沿用了「赋值给 RESULT:0」的旧命令式描述，与源码形态（函数返回值）存在表述差异，以源码为准。
- 匹配基于 C# `Regex`（区分大小写、`RegexOptions.Compiled`，无 culture 设置），与旧文档「指定子串出现的次数」的字面描述不同：第 2 参数始终按正则解释，含正则元字符时需转义。
- zh 套件未收录 STRCOUNT 条目。
