# CSVNICKNAME

- **类别**：式中函数
- **签名**：str CSVNICKNAME(int no, int flag = 0)
- **文档来源**：`ecd/Command.md`「## 变量操作·变量引用·CSV引用」中 `### CSVNICKNAME <数值表达式>(, <数值表达式>)` 小节（与 CSVNAME/CSVCALLNAME/CSVMASTERNAME 共用一段说明）；`ecd/Expression.md`「内置表达式内函数一览」`str CSVNICKNAME(int no, int flag = 0)`；zh 套件未收录

## 语义

从角色模板 CSV（CharaXX.csv）中读取指定角色的 `NICKNAME`（绰号）初始值并作为字符串返回。第 1 参数为角色编号，第 2 参数指定是否读取 SP 角色：为 0（默认，可省略）时读取一般角色，非 0 时读取 SP 角色。用于获取未加载角色的信息或已加载角色的初始信息。

错误行为：
- 未设置兼容性选项「SPキャラを使用する」且第 2 参数非 0：抛出 CodeEE「SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)」。
- 指定编号的角色模板不存在：抛出 CodeEE「定義していないキャラクタを参照しようとしました」。
- CSV 中未定义 NICKNAME（或该字段为 null）时返回空字符串。

## 用法

### str CSVNICKNAME(int no, int flag = 0)
- `no`：角色编号（角色模板的 `NO` 值）。
- `flag`：是否为 SP 角色。0（省略）= 一般角色，非 0 = SP 角色。
- 返回值：该角色 CSV 中 NICKNAME 的初始值字符串。
```erb
; chara03.csv 中定义了 NICKNAME, 小恶魔 时
PRINTL %CSVNICKNAME(3)%          ; 输出 小恶魔
PRINTL %CSVNICKNAME(3, 0)%       ; 同上
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:20`（`["CSVNICKNAME"] = new CsvStrDataMethod(CharacterStrData.NICKNAME)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2064`（`CsvStrDataMethod`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1365`（`GetCharacterStrfromCSVData`，type = `NICKNAME`）、`Runtime/Script/Data/ConstantData.cs:1206`（`GetCharacterTemplate_UseSp`）

```text
CsvStrDataMethod:
构造：返回类型 = string；参数表 = [int, int]（第 2 参数可省略，省略时视为 0）；
     charaStr = NICKNAME；CanRestructure = true。
GetStrValue(exm, args):
    x ← args[0].GetIntValue(exm)                     ; 角色编号
    y ← (args.Count > 1 且 args[1] != null) ? args[1].GetIntValue(exm) : 0  ; SP 标志
    若 !Config.CompatiSPChara 且 y != 0:
        抛出 CodeEE("SPキャラ関係の機能は標準では使用できません(...)")
    返回 exm.VEvaluator.GetCharacterStrfromCSVData(x, NICKNAME, (y != 0), 0)

GetCharacterStrfromCSVData(no, type, isSp, arg2):
    tmpl ← ConstantData.GetCharacterTemplate_UseSp(no, isSp)
    若 tmpl == null: 抛出 CodeEE("定義していないキャラクタを参照しようとしました")
    type == NICKNAME →
        若 tmpl.Nickname != null: 返回 tmpl.Nickname
        否则返回 ""
```

## 备注

- ecd/Command.md 以命令口吻描述「读取…读取一般角色的信息」（称结果返回到 RESULTS），本函数实际为式中函数，字符串直接作为表达式的值返回；Expression.md 的签名与之语义一致。
- 本仓库 `GetCharacterTemplate_UseSp` 的 `sp` 参数未实际参与查找（二分查找直接按 `No` 命中，见 `Runtime/Script/Data/ConstantData.cs:1206`），即 SP 角色查找事实上未实现；但兼容性开关检查仍会先行拦下 `flag != 0` 的调用。
