# ADDCOPYCHARA

- **类别**：命令
- **签名**：
  - `ADDCOPYCHARA <数值表达式>`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作·引用」小节；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

新添加一个与参数指定登录编号的角色数据完全相同的角色。也就是说，它是 `ADDCHARA` 的变种：先创建一个空角色追加到列表末尾，再把源角色的全部变量数据复制过去。指定的登录编号越界时出错。

## 用法

### `ADDCOPYCHARA <数值表达式>`
- `<数值表达式>`：被复制角色的登录编号（角色列表中的位置，0 起）。
```erb
;复制 0 号角色，新角色成为列表最后一个
ADDCOPYCHARA 0
PRINTFORML 新角色登录编号 = {CHARANUM - 1}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:268`（`new ADDCOPYCHARA_Instruction()`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1493`（`ADDCOPYCHARA_Instruction`）；实际逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1188`（`AddCopyChara`）

```text
指令类 ADDCOPYCHARA_Instruction:
    对参数表 TermList 中的每个整数表达式 term:
        VEvaluator.AddCopyChara(term 求值)

AddCopyChara(x):
    若 x < 0 或 x >= 角色列表长度:
        抛出 CodeEE（NotExistFromCopyChara，"复制源角色不存在"）
    AddPseudoCharacter()                 # 先追加一个空角色到列表末尾
    角色列表[x].CopyTo(角色列表[末尾], varData)   # 把源角色全部数据复制到新角色
```

## 备注

- 虽然指令参数类型是 `INT_ANY`（可变数量的整数表达式），文档签名只给出单参数用法；实现上对参数列表逐个执行复制，即写成 `ADDCOPYCHARA 0, 1` 也会依次复制，但文档未提及此用法。
- 相关命令：`COPYCHARA` 是把数据复制到已存在的角色上，不新增角色。
- zh 套件未收录本命令。
