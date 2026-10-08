# DUMPRAND

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：`DUMPRAND`
- **文档来源**：`ecd/docs/translation/Command.md`（「随机数的控制」节）；`Era-Chinese-Documentation` 仅在 `zh/Variable.md:614` 提到「它由 `DUMPRAND` 记录，由 `INITRAND` 读取」，无独立小节。

## 语义

把当前随机数发生器的内部状态保存到 `RANDDATA` 变量中。之后可以用 `INITRAND` 命令把保存的状态读回来，使 `RAND` 从保存点继续产生相同的随机序列。常与 `RANDOMIZE`、`INITRAND` 配合，用于可复现的随机流程。无参数、无返回值。

## 用法

### `DUMPRAND`

无参数。

```erb
;固定随机流程示例
RANDOMIZE 12345
DUMPRAND          ;把初始随机状态存进 RANDDATA
...               ;（RAND 被大量消耗）
INITRAND          ;恢复到 DUMPRAND 时的状态
;此后的 RAND 与上次运行完全一致
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:295` → `new DUMPRAND_Instruction()`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1810`（`DUMPRAND_Instruction`）；随机状态存取在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:51`（`DumpRanddata`）

```text
指令执行时:
    若 全局配置 JSONConfig.Data.UseNewRandom 为真:
        通过 ParserMediator 发出警告
            （trerror.CanNotUseDumprand：新随机数机制下不能使用 DUMPRAND）
        刷新警告列表
        结束（不做任何其他事）
    否则:
        调用 exm.VEvaluator.DumpRanddata()
            即 rand.GetRand(RANDDATA):
                把私有随机数发生器 rand 的当前内部状态
                序列化写入变量 RANDDATA

（配套的 INITRAND 执行 rand.SetRand(RANDDATA)，从变量恢复状态）
```

## 备注

- 本仓库扩展点：若 EE 配置启用了 `UseNewRandom`（新随机数实现），`DUMPRAND` 不再保存状态，只发警告。ecd 文档未提及这一分支。
- 文档强调「不要在 `DUMPRAND` 之前执行 `INITRAND`」：`RANDDATA` 内容不合适时 `RAND` 无法正常工作。实现上恢复状态时并无校验，文档描述与源码行为一致（无检查、后果自负）。
- `RANDDATA` 是可保存变量，可在存档前 `DUMPRAND`、读档后 `INITRAND` 以延续随机序列。
