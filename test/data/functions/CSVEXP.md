# CSVEXP

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数
- **签名**：int CSVEXP(int no, int index, int flag = 0)
- **文档来源**：`ecd/Command.md`「## 变量操作·变量引用·CSV引用」中 `### CSVEXP <数值表达式>, <数值表达式>(, <数值表达式>)` 小节；`ecd/Expression.md`「内置表达式内函数一览」`int CSVEXP(int no, int index, int flag = 0)`；zh 套件未收录

## 语义

从角色模板 CSV（CharaXX.csv）中读取指定角色的 `EXP`（经验）第 `index` 个元素的初始值并作为数值返回。第 1 参数为角色编号，第 2 参数为 EXP 数组索引，第 3 参数指定是否读取 SP 角色：为 0（默认，可省略）时读取一般角色，为 1 时读取 SP 角色。

错误行为：
- 未设置兼容性选项「SPキャラを使用する」且第 3 参数非 0：抛出 CodeEE「SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)」。
- 指定编号的角色模板不存在：抛出 CodeEE「定義していないキャラクタを参照しようとしました」。
- `index` 为负或超出该角色的 EXP 数组长度：抛出 CodeEE「参照可能範囲外を参照しました」。
- 该索引在 CSV 中未赋值时返回 0。

## 用法

### int CSVEXP(int no, int index, int flag = 0)
- `no`：角色编号（角色模板的 `NO` 值）。
- `index`：EXP 的数组索引（可用 `GETNUM EXP, "<经验名>"` 换算文本索引）。
- `flag`：是否为 SP 角色。0（省略）= 一般角色，非 0 = SP 角色。
- 返回值：该角色 CSV 中 EXP:index 的初始值（整数）。
```erb
PRINTFORML 3号角色的V经验 = {CSVEXP(3, 0)}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:26`（`["CSVEXP"] = new CsvDataMethod(CharacterIntData.EXP)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2157`（`CsvDataMethod`，`charaInt = EXP`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1411`（`GetCharacterIntfromCSVData`）、`Runtime/Script/Data/ConstantData.cs:1206`（`GetCharacterTemplate_UseSp`）

```text
CsvDataMethod（charaInt = EXP）:
构造：返回类型 = long；参数表 = [int, int, int]（第 3 参数可省略，省略时视为 0）；CanRestructure = true。
GetIntValue(exm, args):
    x ← args[0].GetIntValue(exm)
    y ← args[1].GetIntValue(exm)
    z ← (args.Count == 3 且 args[2] != null) ? args[2].GetIntValue(exm) : 0
    若 !Config.CompatiSPChara 且 z != 0:
        抛出 CodeEE("SPキャラ関係の機能は標準では使用できません(...)")
    返回 exm.VEvaluator.GetCharacterIntfromCSVData(x, EXP, (z != 0), y)

GetCharacterIntfromCSVData(no, type, isSp, arg2):
    tmpl ← ConstantData.GetCharacterTemplate_UseSp(no, isSp)
    若 tmpl == null: 抛出 CodeEE("定義していないキャラクタを参照しようとしました")
    若 arg2 < 0 或 arg2 >= tmpl.ArrayLength(type):
        抛出 CodeEE("参照可能範囲外を参照しました")
    type == EXP → intDic ← tmpl.Exp
    若 intDic 字典含键 arg2: 返回其值
    否则返回 0
```

## 备注

- ecd/Command.md 的小节以命令口吻描述；实际为式中函数。Expression.md 的函数签名与之语义一致。
- 本仓库 `GetCharacterTemplate_UseSp` 的 `sp` 参数未实际参与查找（见 CSVNAME.md 备注）。
