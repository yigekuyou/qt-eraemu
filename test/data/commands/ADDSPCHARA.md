# ADDSPCHARA

- **类别**：命令
- **签名**：
  - `ADDSPCHARA <数值表达式>(, <数值表达式>, <数值表达式>, ...)`
- **文档来源**：`ecd/docs/translation/Command.md`「角色操作·引用」小节；`Era-Chinese-Documentation` 套件无本命令专节（`Difference.md` 中有 SP 角色兼容性说明，可佐证语义）。

## 语义

添加 SP 角色。SP 角色是 Eramaker 时代的特殊角色（由 `CFLAG:0` 非 0 标记；模板的该标志来自 chara\*.csv，源码 `CharacterTemplate.SetSpFlag` 即按 `CFLAG:0 != 0` 判定）。从 Emuera ver1.816 起，Emuera 不再默认支持 SP 角色：`CFLAG:0` 不再被特殊对待，所有角色都可以通过 `ADDCHARA` 添加。只有启用兼容性选项「使用 SP 角色」时 `ADDSPCHARA` 才可用，用来重现 Eramaker 的行为；未启用该选项时调用 `ADDSPCHARA` 直接报错。

## 用法

### `ADDSPCHARA <数值表达式>(, <数值表达式>, ...)`
- `<数值表达式>`：角色的 CSV 编号。可写多个，一次添加多个 SP 角色。
```erb
;仅在兼容性选项「使用 SP 角色」开启时可用
ADDSPCHARA 10
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:212`（`new ADDCHARA_Instruction(true, false)`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1400`（与 ADDCHARA 共用 `ADDCHARA_Instruction`，此处 `flagSp=true, flagDel=false`）；实际添加逻辑在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1052`（`AddCharacter_UseSp`）

```text
指令类 ADDCHARA_Instruction(flagSp=true, flagDel=false):
    若 !Config.CompatiSPChara（未开启「使用 SP 角色」选项）:
        抛出 CodeEE（SPCharaConfigIsOff，"SP角色功能未开启"）
    对参数表 TermList 中的每个整数表达式 term:
        no = term 求值
        因 Config.CompatiSPChara 已开启:
            VEvaluator.AddCharacter_UseSp(no, isSp=true)

AddCharacter_UseSp(charaTmplNo, isSp):
    tmpl = constant.GetCharacterTemplate_UseSp(charaTmplNo, isSp)
    若 tmpl == null:
        抛出 CodeEE（AddedUndefinedChara，"试图添加未定义的角色"）
    chara = 新建 CharacterData(constant, tmpl, varData)
    varData.CharacterList.Add(chara)
```

## 备注

- 与 `ADDCHARA` 共用同一指令类，仅 `flagSp` 不同；先检查兼容选项再取参数值。
- 源码细节：`AddCharacter_UseSp(no, isSp)` 把 `isSp` 交给 `ConstantData.GetCharacterTemplate_UseSp(index, sp)`，而该方法在本仓库里**忽略 `sp` 参数**（`Runtime/Script/Data/ConstantData.cs:1206-1214`，只按 `No` 做二分查找，且 `ExistCsv` 也照此调用），故开启兼容选项后 `ADDSPCHARA` 并不会像 Eramaker 那样「把 flag0 置 1 再创建」——两种命令取的是同一个模板、用同样的 `CharacterData` 初始化（`CFLAG:0` 直接来自模板 CSV），实际效果相同，差别只在 `ADDSPCHARA` 多一道配置检查。
- zh 套件无专节；`Difference.md` 佐证了 ecd 的说法：1.816 起 SP 角色不再是标准功能，兼容选项仅用于让旧脚本工作。
