# CSVCALLNAME

- **类别**：式中函数
- **签名**：str CSVCALLNAME(int no, int flag = 0)
- **文档来源**：`ecd/Command.md`「## 变量操作·变量引用·CSV引用」中 `### CSVCALLNAME <数值表达式>(, <数值表达式>)` 小节；`ecd/Expression.md`「内置表达式内函数一览」`str CSVCALLNAME(int no, int flag = 0)`；zh 套件未收录

## 语义

从角色模板 CSV（CharaXX.csv）中读取指定角色的 `CALLNAME`（称呼）并作为字符串返回。用于获取尚未加载的角色的信息，或已加载角色的初始值。第 1 参数为角色编号，第 2 参数指定是否读取 SP 角色：为 0（默认，可省略）时读取一般角色，为 1 时读取 SP 角色。

错误行为：
- 未设置兼容性选项「SPキャラを使用する」且第 2 参数非 0：抛出 CodeEE「SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)」。
- 指定编号的角色模板不存在：抛出 CodeEE「定義していないキャラクタを参照しようとしました」。
- CSV 中未定义 `CALLNAME` 时返回空字符串（不报错）。

## 用法

### str CSVCALLNAME(int no, int flag = 0)
- `no`：角色编号（角色模板的 `NO` 值）。
- `flag`：是否为 SP 角色。0（省略）= 一般角色，非 0 = SP 角色。
- 返回值：该角色 CSV 中登记的称呼字符串；未登记则返回空字符串。
```erb
PRINTFORML 3号角色的称呼是 %CSVCALLNAME(3)%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:19`（`["CSVCALLNAME"] = new CsvStrDataMethod(CharacterStrData.CALLNAME)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2064`（`CsvStrDataMethod`，`charaStr = CALLNAME`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1365`（`GetCharacterStrfromCSVData`）、`Runtime/Script/Data/ConstantData.cs:1206`（`GetCharacterTemplate_UseSp`）

```text
CsvStrDataMethod（charaStr = CALLNAME）:
构造：返回类型 = string；参数表 = [int, int]（第 2 参数可省略，省略时视为 0）；CanRestructure = true。
GetStrValue(exm, args):
    x ← args[0].GetIntValue(exm)
    y ← (args.Count > 1 且 args[1] != null) ? args[1].GetIntValue(exm) : 0
    若 !Config.CompatiSPChara 且 y != 0:
        抛出 CodeEE("SPキャラ関係の機能は標準では使用できません(...)")
    返回 exm.VEvaluator.GetCharacterStrfromCSVData(x, CALLNAME, (y != 0), 0)

GetCharacterStrfromCSVData(no, type, isSp, arg2):
    tmpl ← ConstantData.GetCharacterTemplate_UseSp(no, isSp)
    若 tmpl == null: 抛出 CodeEE("定義していないキャラクタを参照しようとしました")
    type == CALLNAME → 返回 tmpl.Callname；若为 null 则返回 ""
```

## 备注

- ecd/Command.md 将本函数与 CSVNAME/CSVNICKNAME/CSVMASTERNAME 合并在一段中描述，且以命令口吻书写；实际为式中函数，返回值直接进入表达式。Expression.md 的函数签名与之语义一致。
- 本仓库 `GetCharacterTemplate_UseSp` 的 `sp` 参数未实际参与查找（见 CSVNAME.md 备注），第 2 参数主要起触发 CompatiSPChara 检查的作用。
