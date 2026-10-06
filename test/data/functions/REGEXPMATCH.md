# REGEXPMATCH

- **类别**：式中函数（EM 扩展 / 正则表达式）
- **签名**：
  - `int REGEXPMATCH(str 目标, str 模式{, int 输出到 RESULTS})`
  - `int REGEXPMATCH(str 目标, str 模式, int 分组数变量, str 结果数组变量)`
- **文档来源**：`ecd/`、`zh/` 两套中文文档与 EE readme 未收录；本分支 readme `emuera.em/Readme/Emuera.EM_readme.txt:134-138`（「◆ <1> int REGEXPMATCH str, pattern(, output) ／ <2> int REGEXPMATCH str, pattern, groupCount, matches」）有记载；changelog `emuera.em/Readme/Emuera.EM_changelog.txt:3`「REGEXPMATCH機能拡張」（v7）。语义以源码为准。

## 语义

用 .NET 正则表达式在目标字符串中做匹配，返回**匹配成功的个数**（`MatchCollection.Count`），没有匹配时返回 `0`。模式字符串非法（`ArgumentException`）时抛 `CodeEE`（`"REGEXPMATCH関数: 第2引数が正規表現として不正です: …"`）。

结果的取出方式有两种：

- **形式 1**：第 3 参数为非 `0` 的整数时，把**分组个数**写入 `RESULT:1`，把各匹配的各分组内容写入 `RESULTS`。写入顺序是「对每个匹配、按 `Regex.GetGroupNames()` 的顺序」依次追加，总量为 `分组数 × 匹配数`（`RESULTS` 数组放不下时截断）。第 3 参数为 `0` 或省略时只返回个数，不写任何结果。
- **形式 2**：第 3 参数必须是**整数变量**，接收分组个数；第 4 参数必须是**字符串数组变量**，接收各匹配的各分组内容（同样是 `分组数 × 匹配数` 个，按数组长度截断）。

未参与匹配的分组取空字符串（`match.Groups[name].Value` 对未参与分组返回 `""`）。

## 用法

### int REGEXPMATCH(目标, 模式{, 输出到 RESULTS})
- 目标：被搜索的字符串。
- 模式：.NET 正则表达式（`RegexFactory.GetRegex` 缓存编译结果）。
- 输出到 RESULTS：`0`/省略 → 只返回个数；非 `0` → 同时写 `RESULT:1`（分组数）与 `RESULTS`（分组内容）。
- 返回值：匹配个数。
```erb
; 形式 1：只数个数
PRINTL REGEXPMATCH("a1 b2 c3", "[a-z][0-9]")        ; 3

; 形式 1：取出分组
REGEXPMATCH("2026-10-07", "([0-9]{4})-([0-9]{2})-([0-9]{2})", 1
PRINTL RESULT:1        ; 分组个数
PRINTL RESULTS:0        ; 整个匹配 "2026-10-07"
PRINTL RESULTS:1        ; 第 1 个分组
```

```erb
; 形式 2：写入自己的变量
#DIM GRP
#DIMS M = 32
REGEXPMATCH("a=1, b=22, c=333", "(\w+)=(\d+)", GRP, M)
PRINTFORML 匹配数={RESULT} 分组数={GRP}             ; RESULT 不写，提示：形式 2 不写 RESULT:1
LOOP LOCAL, 0, GRP * 2
    PRINTFORML M:{LOCAL} = {M:LOCAL}
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:244`（`["REGEXPMATCH"] = new RegexpMatchMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:689`（`RegexpMatchMethod`，辅助 `Output` 在 `:701`）；正则缓存见 `RegexFactory.GetRegex`

```text
RegexpMatchMethod:
    构造:
        ReturnType = long
        argumentTypeArrayEx = [
            { ArgTypes = { String, String, Int }, OmitStart = 2 },              # 2~3 个参数（形式 1）
            { ArgTypes = { String, String, RefInt, RefString1D } }              # 恰好 4 个参数（形式 2）
        ]
        CanRestructure = false

    Output(matches, reg, values):                        # :701
        idx = 0
        遍历 match ∈ matches:
            遍历 name ∈ reg.GetGroupNames():
                若 idx >= values.Length: 返回            # 数组放满即停
                values[idx] = match.Groups[name].Value
                idx++

    GetIntValue(exm, arguments):
        baseString = arguments[0]
        尝试 reg = RegexFactory.GetRegex(arguments[1])
        失败(ArgumentException) → 抛 CodeEE(InvalidRegexArg, Name, 2, e.Message)
        matches = reg.Matches(baseString)
        ret = matches.Count
        若 参数个数 == 3 且 arguments[2] 的值 != 0:
            exm.VEvaluator.RESULT_ARRAY[1] = reg.GetGroupNumbers().Length     # RESULT:1 = 分组个数
            若 ret > 0: Output(matches, reg, exm.VEvaluator.RESULTS_ARRAY)     # RESULTS:0 起
        若 参数个数 == 4:
            (arguments[2] as VariableTerm).SetValue(reg.GetGroupNumbers().Length, exm)   # 整数变量 ← 分组个数
            若 ret > 0: Output(matches, reg, (arguments[3] as VariableTerm).Identifier.GetArray() as string[])
        返回 ret
```

## 备注

- 语义据源码；readme 的形式 1、形式 2 描述与源码一致（形式 1 写 `RESULT:1` 与 `RESULTS`；形式 2 写整数变量的「分组数」与字符串数组变量的结果）。
- **分组顺序**：写入顺序由 .NET 的 `Regex.GetGroupNames()` 决定。对含无名分组的模式，该顺序与模式中书写的次序并不一致（.NET 的已知实现特性），因此 `RESULTS`/目标数组里各分组的排列不能简单按 `1, 2, 3` 假设——需要用形式 2 先取分组个数并结合自己的模式做核对。此点标注为**推定**（依 .NET 语义推得，未在本仓库运行时验证），源码未做任何重排。
- 形式 2 的第 3 参数用 `VariableTerm.SetValue`（可带下标），第 4 参数用 `Identifier.GetArray()`（**忽略**变量名里写的下标，整个数组从头写）；第 4 参数写上标也会被忽略。
- 形式 2 也要求第 3 参数是整数变量、第 4 参数是一维字符串数组变量（`RefInt`、`RefString1D`），传字面量或非数组会在解析期报错。
- 两种形式都不写 `RESULT:0`（`RESULT` 本身）；形式 1 写 `RESULT:1`（注意是 `RESULT` 的**第 2 个元素**，不是 `RESULTS` 的第一个元素）。`RESULT:1` 若被 `VariableSize.CSV` 缩到只剩 1 个元素会越界——源码无检查。
- `RegexFactory.GetRegex` 会缓存已编译的正则，反复用同一模式不会重复编译。
- 形式 1 的输出上限是 `RESULTS` 数组长度（可在 `VariableSize.CSV` 调整）；超出部分被静默丢弃，返回值 `ret` 仍是完整匹配数。
