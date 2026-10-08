# RAND

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（另有同名拟似变量形态 `RAND:X`）
- **签名**：int RAND(int min = 0, int max)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表及示例）；zh `Difference.md`「RAND的行为」、zh `Variable.md`（拟似变量 RAND）

## 语义

返回一个随机整数，取值范围为 `[min, max-1]`（即「min 以上 max 未满」）。第二参数 max 省略时（等价于 `RAND(max)`）min 取默认值 0，返回 `0 ～ max-1` 的随机值。两种省略写法 `RAND(100)` 与 `RAND(,100)` 等价。

错误行为：当 `max <= min` 时抛出 CodeEE 运行期错误——若 `min == 0` 报「RAND 的最大值指定了 0 以下的值」，否则报「RAND 的最大值指定了小于等于最小值的值」。

随机数状态可用 `RANDOMIZE` 初始化、用 `DUMPRAND` 保存到 `RANDDATA` 变量、用 `INITRAND` 恢复（见 ecd/Command.md 的 RANDOMIZE/DUMPRAND/INITRAND 小节）。

注意与 eramaker 时代的拟似变量形态 `RAND:X`（eramaker 中返回 0～32767 的随机数对 X 求余）不同：Emuera 的函数形态返回 64 位均匀随机数，且 X（max）为 0 或负数时直接报错而不是返回 0。zh/Difference.md 明确记载了这一行为差异。

## 用法

### int RAND(int max)
- `max`：最大值（不含）。
- 返回值：`0 ～ max-1` 的随机整数。
```erb
	A = RAND(100)	; A = 0 ～ 99 的随机值
```
### int RAND(int min, int max)
- `min`：最小值（含）。可省略为 `,`（`RAND(,100)` 与 `RAND(0,100)` 等价）。
- `max`：最大值（不含）。
- 返回值：`min ～ max-1` 的随机整数。
```erb
	A = RAND(0, 100)	; A = 0 ～ 99 的随机值
	A = RAND(5, 10)		; A = 5 ～ 9 的随机值
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:74`（`["RAND"] = new RandMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2951`（`RandMethod`）；底层随机数 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:55`（`GetNextRand`）

```text
构造：返回类型 = long；参数 = 1～2 个 int（第 1 参数可省略，OmitStart = 1）；CanRestructure = false（每次求值结果不同，禁止常量折叠）。

GetIntValue(exm, args):
    min ← 0
    若 args.Count == 1:
        max ← args[0].GetIntValue(exm)
    否则:
        若 args[0] != null: min ← args[0].GetIntValue(exm)
        max ← args[1].GetIntValue(exm)
    若 max <= min:
        若 min == 0: 抛出 CodeEE（"RAND 最大值指定了 0 以下的值 max"）
        否则:       抛出 CodeEE（"RAND 最大值指定了小于等于最小值的值 max"）
    返回 exm.VEvaluator.GetNextRand(max - min) + min
```

`GetNextRand(max)`：启用新随机数配置时由新随机数生成器返回 `[0, max)` 的 64 位随机数，否则由经典 `rand.NextInt64(max)` 返回；因此函数整体返回 `[min, max-1]`。

## 备注

- ecd/Command.md 没有独立的 `RAND` 函数小节（只有 RANDOMIZE/DUMPRAND/INITRAND 涉及随机数状态）；`ecd/Expression.md` 给出签名与三种省略写法示例。
- zh/Difference.md 记载了与 eramaker `RAND:X` 拟似变量的行为差异：eramaker 返回 0～32767 的随机数 % X（有偏且 X 为负也能工作），Emuera 返回 0～18446744073709551615 范围随机数 % X（此处指拟似变量实现 `Runtime/Script/Statements/Variable/VariableToken.cs:1283` 的经典路径），X 为 0 或负数时报错。本仓库源码中还支持 `UseNewRandom` 配置切换到新随机数生成器，文档均未提及。
- RAND 既有式中函数形态又有拟似变量形态（`RAND:X`），两者语法与返回机制不同，本文档以式中函数形态为主。
