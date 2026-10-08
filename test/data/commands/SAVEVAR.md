# SAVEVAR

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：`SAVEVAR <变量名列表>, <文件名表达式>, <注释>`（按参数类型 SP_SAVEVAR 推定；两套文档均未给出正式签名）
- **文档来源**：`ecd/docs/translation/Command.md` 未收录；`Era-Chinese-Documentation/docs/` 未收录

## 语义

按设计意图是把指定的变量集保存到指定存档文件（对应读取指令 `LOADVAR`）。**本仓库中该指令未实现**：执行时无条件抛出「未实现」错误并终止脚本。两套参考文档也没有为它撰写语义说明。

由于实现体被整体注释掉，其确切签名与行为无法从源码还原，只能从参数构造器 `SP_SAVEVAR` 推定需要「变量名列表 + 文件名 + 注释」三类参数。

## 用法

### SAVEVAR <变量名列表>, <文件名表达式>, <注释>

不可用。任何调用都会报「未实现」错误。

```erb
; 下面的写法（按推定签名）在本仓库中会直接出错
SAVEVAR DAY, MONEY, "savevars.dat", "变量存档"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:380`（`new SAVEVAR_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1956`（`SAVEVAR_Instruction`，`DoInstruction` 在 1964 行）

```text
SAVEVAR_Instruction:
    构造: ArgBuilder = SP_SAVEVAR 参数构造器, 标记 METHOD_SAFE | EXTENDED

    DoInstruction(exm, func, state):
        throw new NotImplCodeEE()      // 无条件抛出未实现错误

        // 以下原实现已被注释：
        // arg = (SpSaveVarArgument)func.Argument
        // vars = arg.VarTokens                      // 变量名列表
        // datFilename = arg.Term.GetStrValue(exm)   // 文件名
        // savMes = arg.SavMes.GetStrValue(exm)      // 存档注释
        // exm.VEvaluator.SaveVariable(datFilename, savMes, vars)
```

## 备注

- 本仓库未实现：`DoInstruction` 直接抛 `NotImplCodeEE`，注释代码保留了原设计（`VariableEvaluator.SaveVariable`，该成员在本仓库 Evaluator 中也已不存在）。
- 读取侧 `LOADVAR`（`Runtime/Script/Statements/Instraction.Child.cs:1974`）同样是「注册了但一执行就抛未实现」。
- 两套文档（ecd 与 zh）均未收录，语义与签名部分只能依据源码残迹撰写。
