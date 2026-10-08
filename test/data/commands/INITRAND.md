# INITRAND

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：
  - `INITRAND`
- **文档来源**：`ecd/docs/translation/Command.md`「随机数的控制」INITRAND 小节；zh 套件仅在与 `RANDDATA` 变量相关的说明中提及（`zh/Variable.md`：「它由 `DUMPRAND` 记录，由 `INITRAND` 读取」），无独立命令小节。

## 语义

读取保存到内置变量 `RANDDATA` 中的随机数状态，把随机数发生器恢复到该状态。必须在 `DUMPRAND` 之后执行才有意义；若 `RANDDATA` 内容不合适，`RAND` 之后将无法正常工作。

`RANDDATA` 是可保存变量，典型用法是：保存前 `DUMPRAND`，读档后立即 `INITRAND`，从而延续与保存时相同的随机数序列。无参数、无返回值（副作用是改变全局随机数状态）。

## 用法

### `INITRAND`
- 无参数。
```erb
;保存随机数状态到 RANDDATA（随存档一起保存）
DUMPRAND
;……读档后……
;恢复与保存时相同的随机数状态
INITRAND
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:296`（`new INITRAND_Instruction()`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1788`（`INITRAND_Instruction`）；核心在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:46`（`InitRanddata`）

```text
DoInstruction(exm, func, state):
    若 JSONConfig.Data.UseNewRandom（EE 新随机数配置开启）:
        ParserMediator.Warn(CanNotUseInitrand)   # 发出“无法使用 INITRAND”警告
        FlushWarningList()
        # 不修改随机数状态，指令相当于空操作
    否则:
        exm.VEvaluator.InitRanddata()

InitRanddata():
    rand.SetRand(RANDDATA)    # 用变量 RANDDATA 的内容恢复 Xorshift 随机数发生器状态
```

## 备注

- 文档说「`RANDDATA` 变量的内容不合适时，`RAND` 会无法正常工作」，源码 `SetRand` 不做有效性校验，语义一致。
- 与文档（成文时）不同，本仓库为 EE 增加了 `UseNewRandom`（JSON 配置）模式：该模式下 `INITRAND` 仅输出警告、不执行任何恢复操作——文档未记录此行为，属于文档与源码的差异。`RANDOMIZE`/`DUMPRAND` 在该模式下同样被忽略并告警。
- `INITRAND` 无参数，但与 `RANDOMIZE`（INT_EXPRESSION 参数）不同，注册时不经 switch-case，直接是独立指令类。
