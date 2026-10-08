# EXISTCSV

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：int EXISTCSV(int no, int flag = 0)
- **文档来源**：`ecd/Command.md`「### EXISTCSV `<数值表达式>`, `<数值表达式>`」小节；`ecd/Expression.md`「内置表达式内函数一览」`int EXISTCSV(int no, int flag = 0)`；zh 套件未收录

## 语义

检查指定角色编号的角色是否已在角色 CSV（CharaXX.csv）中定义：已定义返回 `1`，未定义返回 `0`。常用来判断 `ADDCHARA no` 能否在不报错的情况下执行。第 2 参数指定是否检查 SP 角色：为 0（默认，可省略）时检查一般角色，非 0 时检查 SP 角色（未设兼容性选项时非 0 会直接报错）。无副作用，可常量折叠（`CanRestructure = true`）。

错误行为：
- 未设置兼容性选项「SPキャラを使用する」且第 2 参数非 0：抛出 CodeEE「SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)」。

## 用法

### int EXISTCSV(int no, int flag = 0)
- `no`：角色编号（角色模板的 `NO` 值）。
- `flag`：是否检查 SP 角色。0（省略）= 一般角色，非 0 = SP 角色。
- 返回值：已定义 `1`，未定义 `0`。
```erb
IF EXISTCSV(10)
    ADDCHARA 10
ELSE
    PRINTL 编号 10 的角色未定义
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:34`（`["EXISTCSV"] = new ExistCsvMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2292`（`ExistCsvMethod`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1355`（`ExistCsv`）、`Runtime/Script/Data/ConstantData.cs:1206`（`GetCharacterTemplate_UseSp`）

```text
ExistCsvMethod:
构造：返回类型 = long；参数表 = [int, int]（第 2 参数可省略）；CanRestructure = true。
GetIntValue(exm, args):
    no ← args[0].GetIntValue(exm)
    isSp ← (args.Count == 2 且 args[1] != null) ? (args[1].GetIntValue(exm) != 0) : false
    若 !Config.CompatiSPChara 且 isSp:
        抛出 CodeEE("SPキャラ関係の機能は標準では使用できません(...)")
    返回 exm.VEvaluator.ExistCsv(no, isSp)

ExistCsv(charaNo, getSp):
    tmpl ← ConstantData.GetCharacterTemplate_UseSp(charaNo, getSp)
    若 tmpl == null: 返回 0
    否则: 返回 1
```

## 备注

- ecd/Command.md 的签名写作两个必填参数（`<数值表达式>, <数值表达式>`），源码与 Expression.md 均允许省略第 2 参数。
- ecd/Command.md 以命令口吻称结果「赋值给 RESULT:0」，本函数实际为式中函数，数值直接作为表达式的值返回。
- 本仓库 `GetCharacterTemplate_UseSp` 的 `sp` 参数未实际参与查找（二分查找直接按 `No` 命中，见 CSVNICKNAME.md 备注），因此即使开启兼容选项，SP 标志也不会改变查找结果。
