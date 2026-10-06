# CSVRELATION

- **类别**：式中函数
- **签名**：int CSVRELATION(int no, int index, int flag = 0)
- **文档来源**：`ecd/Command.md`「## 变量操作·变量引用·CSV引用」中 `### CSVRELATION <数值表达式>, <数值表达式>, <数值表达式>` 小节（与 CSVBASE/CSVABL/CSVTALENT 等共用一段说明）；`ecd/Expression.md`「内置表达式内函数一览」`int CSVRELATION(int no, int index, int flag = 0)`；zh 套件未收录

## 语义

从角色模板 CSV（CharaXX.csv）中读取指定角色的 `RELATION`（好感度/关系）第 `index` 个元素的初始值并以数值返回。RELATION 的索引对应其他角色的编号（`relation.csv`／CSV 文本索引定义的角色名→编号映射）。第 1 参数为角色编号，第 2 参数为 RELATION 数组索引，第 3 参数指定是否读取 SP 角色：为 0（默认，可省略）时读取一般角色，非 0 时读取 SP 角色。

错误行为：
- 未设置兼容性选项「SPキャラを使用する」且第 3 参数非 0：抛出 CodeEE「SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)」。
- 指定编号的角色模板不存在：抛出 CodeEE「定義していないキャラクタを参照しようとしました」。
- `index` 为负或超出该变量在 CSV 中的可引用范围：抛出 CodeEE「参照可能範囲外を参照しました」。
- 模板中该索引处未登记时返回 `0`。

## 用法

### int CSVRELATION(int no, int index, int flag = 0)
- `no`：角色编号（角色模板的 `NO` 值）。
- `index`：RELATION 的数组索引（通常是对象角色的编号）。
- `flag`：是否为 SP 角色。0（省略）= 一般角色，非 0 = SP 角色。
- 返回值：该角色 CSV 中 RELATION:index 的初始值。
```erb
; chara01.csv 中定义了 RELATION, 3, 500 时
PRINTV CSVRELATION(1, 3)         ; 输出 500（角色1对角色3的关系值）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:27`（`["CSVRELATION"] = new CsvDataMethod(CharacterIntData.RELATION)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2157`（`CsvDataMethod`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1411`（`GetCharacterIntfromCSVData`，type = `RELATION`）、`Runtime/Script/Data/ConstantData.cs:1206`（`GetCharacterTemplate_UseSp`）

```text
CsvDataMethod:
构造：返回类型 = long；参数表 = [int, int, int]（第 3 参数可省略，省略时视为 0）；
     charaInt = RELATION；CanRestructure = true。
GetIntValue(exm, args):
    x ← args[0].GetIntValue(exm)                     ; 角色编号
    y ← args[1].GetIntValue(exm)                     ; RELATION 索引
    z ← (args.Count == 3 且 args[2] != null) ? args[2].GetIntValue(exm) : 0  ; SP 标志
    若 !Config.CompatiSPChara 且 z != 0:
        抛出 CodeEE("SPキャラ関係の機能は標準では使用できません(...)")
    返回 exm.VEvaluator.GetCharacterIntfromCSVData(x, RELATION, (z != 0), y)

GetCharacterIntfromCSVData(no, type, isSp, arg2):
    tmpl ← ConstantData.GetCharacterTemplate_UseSp(no, isSp)
    若 tmpl == null: 抛出 CodeEE("定義していないキャラクタを参照しようとしました")
    若 arg2 < 0 或 arg2 >= tmpl.ArrayLength(type):
        抛出 CodeEE("参照可能範囲外を参照しました")
    type == RELATION → intDic ← tmpl.Relation
    若 intDic 含键 arg2: 返回其值
    否则返回 0L
```

## 备注

- ecd/Command.md 中 CSVRELATION 的签名写作三参数不带括号省略记法（`<数值表达式>, <数值表达式>, <数值表达式>`），与同组其他函数的 `(, <数值表达式>)` 写法不一致；源码参数表 `OmitStart = 2` 表明第 3 参数可省略，Expression.md 的 `int CSVRELATION(int no, int index, int flag = 0)` 与源码一致。
- ecd/Command.md 以命令口吻称「返回值为数值返回到 RESULT 中」，本函数实际为式中函数，数值直接作为表达式的值返回。
- 本仓库 `GetCharacterTemplate_UseSp` 的 `sp` 参数未实际参与查找（见 CSVNICKNAME.md 备注）。
