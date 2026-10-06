# CSVTALENT

- **类别**：式中函数
- **签名**：int CSVTALENT(int no, int index, int flag = 0)
- **文档来源**：`ecd/Command.md`「## 变量操作·变量引用·CSV引用」中 `### CSVTALENT <数值表达式>, <数值表达式>(, <数值表达式>)` 小节（与 CSVBASE/CSVABL/CSVMARK 等共用一段说明）；`ecd/Expression.md`「内置表达式内函数一览」`int CSVTALENT(int no, int index, int flag = 0)`；zh 套件未收录

## 语义

从角色模板 CSV（CharaXX.csv）中读取指定角色的 `TALENT`（素质）第 `index` 个元素的初始值并以数值返回。索引通常配合 `talent.csv` 的文本索引（经 `GETNUM` 换算）使用。第 1 参数为角色编号，第 2 参数为 TALENT 数组索引，第 3 参数指定是否读取 SP 角色：为 0（默认，可省略）时读取一般角色，非 0 时读取 SP 角色。常用于判断未加载角色是否带有某素质，或读取已加载角色的初始素质。

错误行为：
- 未设置兼容性选项「SPキャラを使用する」且第 3 参数非 0：抛出 CodeEE「SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)」。
- 指定编号的角色模板不存在：抛出 CodeEE「定義していないキャラクタを参照しようとしました」。
- `index` 为负或超出该变量在 CSV 中的可引用范围：抛出 CodeEE「参照可能範囲外を参照しました」。
- 模板中该索引处未登记（未定义该素质）时返回 `0`。

## 用法

### int CSVTALENT(int no, int index, int flag = 0)
- `no`：角色编号（角色模板的 `NO` 值）。
- `index`：TALENT 的数组索引。
- `flag`：是否为 SP 角色。0（省略）= 一般角色，非 0 = SP 角色。
- 返回值：该角色 CSV 中 TALENT:index 的初始值（未定义为 0）。
```erb
; talent.csv 中定义了 10, 淑女 且 chara03.csv 写有 TALENT,10 时
PRINTV CSVTALENT(3, GETNUM(TALENT, "淑女"))   ; 输出 1
IF CSVTALENT(3, 10) != 0
    PRINTL 该角色拥有淑女素质
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:28`（`["CSVTALENT"] = new CsvDataMethod(CharacterIntData.TALENT)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2157`（`CsvDataMethod`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1411`（`GetCharacterIntfromCSVData`，type = `TALENT`）、`Runtime/Script/Data/ConstantData.cs:1206`（`GetCharacterTemplate_UseSp`）

```text
CsvDataMethod:
构造：返回类型 = long；参数表 = [int, int, int]（第 3 参数可省略，省略时视为 0）；
     charaInt = TALENT；CanRestructure = true。
GetIntValue(exm, args):
    x ← args[0].GetIntValue(exm)                     ; 角色编号
    y ← args[1].GetIntValue(exm)                     ; TALENT 索引
    z ← (args.Count == 3 且 args[2] != null) ? args[2].GetIntValue(exm) : 0  ; SP 标志
    若 !Config.CompatiSPChara 且 z != 0:
        抛出 CodeEE("SPキャラ関係の機能は標準では使用できません(...)")
    返回 exm.VEvaluator.GetCharacterIntfromCSVData(x, TALENT, (z != 0), y)

GetCharacterIntfromCSVData(no, type, isSp, arg2):
    tmpl ← ConstantData.GetCharacterTemplate_UseSp(no, isSp)
    若 tmpl == null: 抛出 CodeEE("定義していないキャラクタを参照しようとしました")
    若 arg2 < 0 或 arg2 >= tmpl.ArrayLength(type):
        抛出 CodeEE("参照可能範囲外を参照しました")
    type == TALENT → intDic ← tmpl.Talent
    若 intDic 含键 arg2: 返回其值
    否则返回 0L
```

## 备注

- ecd/Command.md 以命令口吻称「返回值为数值返回到 RESULT 中」，本函数实际为式中函数，数值直接作为表达式的值返回；Expression.md 的签名与之语义一致。
- 本仓库 `GetCharacterTemplate_UseSp` 的 `sp` 参数未实际参与查找（见 CSVNICKNAME.md 备注）。
