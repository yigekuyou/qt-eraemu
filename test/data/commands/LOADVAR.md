# LOADVAR

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：命令
- **签名**：
  - `LOADVAR <字符串表达式>`
- **文档来源**：`ecd/docs/translation/Command.md` 未收录本命令；`Era-Chinese-Documentation` 套件亦未收录。

## 语义

从指定文件名（字符串表达式）读取此前用 `SAVEVAR` 保存的变量数据，恢复到当前变量中。属于 `SAVEVAR`～`LOADVAR` 变量存取对。文档中没有任何关于本命令的记述；本仓库中该命令虽被注册，但执行时直接抛出「未实现」异常，实际不可用。

## 用法

### `LOADVAR <字符串表达式>`
- `<字符串表达式>`：要读取的数据文件名（原意是配合 `SAVEVAR` 把指定变量组保存到该文件、再从该文件读回）。
```erb
;本仓库中执行会报「未实现」错误，以下仅为原设计意图
LOADVAR "vars.dat"
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:381`（`new LOADVAR_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1974`（`LOADVAR_Instruction`，参数构建器 `STR_EXPRESSION`，标志 `METHOD_SAFE | EXTENDED`）

```text
case LOADVAR:
    抛出 NotImplCodeEE 异常（未实现，执行即报错）
    # 以下为被注释掉的原设计意图：
    # datFilename = 参数字符串表达式求值
    # exm.VEvaluator.LoadVariable(datFilename)   # 从文件读回变量
```

## 备注

- 本仓库未实现：`DoInstruction` 第一行就是 `throw new NotImplCodeEE()`，后续逻辑全部被注释。同批的 `SAVEVAR`（`Runtime/Script/Statements/Instraction.Child.cs:1956`）也是同样状态，二者配对存在但均不可执行。
- 两套文档（ecd 与 zh 套件）均未收录本命令，语义只能依据被注释的源码推断，无法与文档交叉核对。
