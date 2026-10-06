# ADDCHARA

- **类别**：命令
- **签名**：
  - `ADDCHARA <数值表达式>(, <数值表达式>, <数值表达式>, ...)`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作·引用」小节；`Era-Chinese-Documentation` 套件无本命令专节（仅在 `Difference.md`、`General.md` 等处附带提及：Eramaker 中编号越界也能工作、SP 角色历史等）。

## 语义

添加角色。第 1 参数是角色的 CSV 编号（即 `chara*.csv` 中 `番号` 对应的模板编号）。给出多个参数时会一次性添加多个角色。若指定的 CSV 编号未定义任何角色模板，则抛出 CodeEE 错误。添加成功后新角色追加到角色列表末尾，`CHARANUM` 增加。

在 Emuera ver1.816 之后，SP 角色不再是默认功能，`CFLAG:0` 不再被特殊对待，所有角色都可以通过 `ADDCHARA` 添加；只有启用兼容性选项「使用 SP 角色」时，SP 语义才重新生效（此时 `ADDCHARA` 与 `ADDSPCHARA` 都走 `AddCharacter_UseSp` 分支、按编号查同一张模板表，二者实际效果相同，见 `ADDSPCHARA.md` 备注）。

## 用法

### `ADDCHARA <数值表达式>(, <数值表达式>, ...)`
- `<数值表达式>`：角色的 CSV 编号。可写多个，用逗号分隔，一次添加多个角色。
```erb
;添加 CSV 编号为 10 和 11 的两个角色
ADDCHARA 10, 11
PRINTFORML CHARANUM = {CHARANUM}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:211`（`new ADDCHARA_Instruction(false, false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1400`（`ADDCHARA_Instruction`，构造参数 `flagSp=false, flagDel=false`）；实际添加逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1043`（`AddCharacter`）

```text
指令类 ADDCHARA_Instruction(flagSp=false, flagDel=false):
    对参数表 TermList 中的每个整数表达式 term:
        no = term 求值
        若 Config.CompatiSPChara（兼容 SP 角色选项开启）:
            VEvaluator.AddCharacter_UseSp(no, isSp=false)
        否则:
            VEvaluator.AddCharacter(no)

AddCharacter(charaTmplNo):
    tmpl = constant.GetCharacterTemplate(charaTmplNo)
    若 tmpl == null:
        抛出 CodeEE（"试图添加未定义的角色"）
    chara = 新建 CharacterData(constant, tmpl, varData)
    varData.CharacterList.Add(chara)   # 追加到角色列表末尾
```

## 备注

- 同一个指令类通过构造参数复用：`ADDSPCHARA = ADDCHARA_Instruction(true, false)`、`DELCHARA = ADDCHARA_Instruction(false, true)`（见 source_index 中 DELCHARA 条目）。`ADDVOIDCHARA`（`ADDVOIDCHARA_Instruction`）、`SWAPCHARA`／`COPYCHARA`（`SWAPCHARA_Instruction`／`COPYCHARA_Instruction`）、`ADDCOPYCHARA`（`ADDCOPYCHARA_Instruction`）等**不共用**本类，各自是独立指令类。
- 文档说「若 CSV 不存在会报错」与源码一致（`AddCharacter` 抛出 `AddedUndefinedChara`）；注意与 `ADDDEFCHARA` 不同，后者对未定义编号回退为空角色。
- zh 套件无本命令专节，仅 `Difference.md` 提及 Eramaker 时代编号越界也能注册等兼容性历史。
