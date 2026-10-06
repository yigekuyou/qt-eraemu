# CSVCSTR

- **类别**：式中函数
- **签名**：str CSVCSTR(int no, int index, int flag = 0)
- **文档来源**：`ecd/Command.md`「## 变量操作·变量引用·CSV引用」中 `### CSVCSTR <数值表达式>, <数值表达式>(, <数值表达式>)` 小节；`ecd/Expression.md`「内置表达式内函数一览」`str CSVCSTR(int no, int index, int flag = 0)`；zh 套件未收录

## 语义

从角色模板 CSV（CharaXX.csv）中读取指定角色的 `CSTR`（字符串型角色变量）第 `index` 个元素的初始值并作为字符串返回。第 1 参数为角色编号，第 2 参数为 CSTR 数组索引，第 3 参数指定是否读取 SP 角色：为 0（默认，可省略）时读取一般角色，为 1 时读取 SP 角色。

错误行为：
- 未设置兼容性选项「SPキャラを使用する」且第 3 参数非 0：抛出 CodeEE「SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)」。
- 指定编号的角色模板不存在：抛出 CodeEE「定義していないキャラクタを参照しようとしました」。
- `index` 为负或超出该角色 CSV 中 CSTR 的可引用范围：抛出 CodeEE「CSTRの参照可能範囲外を参照しました」。
- CSV 中定义了 CSTR 数组但该索引处未赋值时返回空字符串；CSV 中完全没有 CSTR 项时对任何索引都返回空字符串。

## 用法

### str CSVCSTR(int no, int index, int flag = 0)
- `no`：角色编号（角色模板的 `NO` 值）。
- `index`：CSTR 的数组索引。
- `flag`：是否为 SP 角色。0（省略）= 一般角色，非 0 = SP 角色。
- 返回值：该角色 CSV 中 CSTR:index 的初始值字符串。
```erb
; chara03.csv 中定义了 CSTR,0, hello 时
PRINTL %CSVCSTR(3, 0)%          ; 输出 hello
PRINTL %CSVCSTR(3, GETNUM(CSTR, "称呼"))%   ; 用文本索引换算数值索引
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:22`（`["CSVCSTR"] = new CsvcstrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2114`（`CsvcstrMethod`）
- 转调：`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1365`（`GetCharacterStrfromCSVData`，type = `CSTR`）、`Runtime/Script/Data/ConstantData.cs:1206`（`GetCharacterTemplate_UseSp`）

```text
CsvcstrMethod:
构造：返回类型 = string；参数表 = [int, int, int]（第 3 参数可省略，省略时视为 0）；CanRestructure = true。
GetStrValue(exm, args):
    x ← args[0].GetIntValue(exm)                     ; 角色编号
    y ← args[1].GetIntValue(exm)                     ; CSTR 索引
    z ← (args.Count == 3 且 args[2] != null) ? args[2].GetIntValue(exm) : 0  ; SP 标志
    若 !Config.CompatiSPChara 且 z != 0:
        抛出 CodeEE("SPキャラ関係の機能は標準では使用できません(...)")
    返回 exm.VEvaluator.GetCharacterStrfromCSVData(x, CSTR, (z != 0), y)

GetCharacterStrfromCSVData(no, type, isSp, arg2):
    tmpl ← ConstantData.GetCharacterTemplate_UseSp(no, isSp)
    若 tmpl == null: 抛出 CodeEE("定義していないキャラクタを参照しようとしました")
    type == CSTR →
        若 tmpl.CStr != null:
            若 arg2 < 0 或 arg2 >= tmpl.ArrayStrLength(CSTR):
                抛出 CodeEE("CSTRの参照可能範囲外を参照しました")
            若 tmpl.CStr 字典含键 arg2: 返回其值
            否则返回 ""
        否则返回 ""
```

## 备注

- ecd/Command.md 说「除了 CSVCSTR 的返回值为字符串返回到 RESULTS 中，其他指令返回值为数值返回到 RESULT 中」——这是命令式口吻；本函数实际为式中函数，字符串直接作为表达式的值返回。Expression.md 的函数签名与之语义一致。
- 与数值型 CSV 系列不同，本函数越界（index 超范围）报错，但「模板存在而该键未登记」时返回空串而非报错；模板完全没有 CSTR 时任意索引都返回空串（源码中先判 `tmpl.CStr != null`）。
- 本仓库 `GetCharacterTemplate_UseSp` 的 `sp` 参数未实际参与查找（见 CSVNAME.md 备注）。
