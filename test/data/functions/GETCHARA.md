# GETCHARA

- **类别**：式中函数
- **签名**：int GETCHARA(int no, int flag = 0)
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「### GETCHARA `<角色编号>`, (`<0 或 0 以外>`，可省略)」

## 语义

在当前已加入的角色列表中查找角色编号（`NO`）等于 `no` 的角色：存在则返回其在列表中的位置（0 起始索引），不存在则返回 `-1`。

用于从整个角色列表中确认某个角色是否存在并取得其列表索引。

第 2 参数 `flag` 与互斥性选项「SP 角色を使用する」（`Config.CompatiSPChara`）有关：

- 未开启该兼容选项（标准行为）时忽略 `flag`，直接在整个角色列表（含 SP 角色）中按 `NO` 查找。
- 开启该兼容选项时沿用旧版行为：`flag` 非 0 表示"先找普通角色、找不到再找 SP 角色"；`flag` 为 0（或省略）表示只找普通角色。

## 用法

### int GETCHARA(int no, int flag = 0)
- `no`：要查找的角色编号（角色 CSV 中定义的 `NO`）。
- `flag`：可省略。兼容 SP 角色模式下为 0 或 0 以外的整数，控制是否回退查找 SP 角色；标准模式下无意义。
- 返回值：角色在列表中的位置；不存在返回 `-1`。
```erb
IF GETCHARA(3) >= 0
    PRINTL 编号 3 的角色在场
ELSE
    PRINTL 编号 3 的角色不在场
ENDIF
;常用形式：直接把返回值赋给 TARGET
TARGET = GETCHARA(1)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:16`（`["GETCHARA"] = new GetcharaMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:1992`（`GetcharaMethod`）；辅助实现 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1325`（`GetChara`）、`:1336`（`GetChara_UseSp`）

```text
构造：返回类型 = long；参数 = [int, int]，第 2 个可省略；CanRestructure = false。

GetIntValue(exm, args):
    no ← args[0].GetIntValue(exm)
    若 !Config.CompatiSPChara:                ; 标准模式：忽略 flag
        返回 VEvaluator.GetChara(no)
        ; GetChara: 遍历角色列表，找到 NO == no 的角色返回其下标 i，否则返回 -1
    ; 以下为兼容旧行为（CompatiSPChara = ON）
    CheckSp ← (args.Count > 1 且 args[1] != null 且 args[1].GetIntValue(exm) != 0)
    若 CheckSp:
        r ← GetChara_UseSp(no, false)         ; 先找普通角色
        若 r != -1: 返回 r
        返回 GetChara_UseSp(no, true)         ; 找不到再找 SP 角色
    否则:
        返回 GetChara_UseSp(no, false)        ; 只找普通角色
        ; GetChara_UseSp: 遍历列表，NO 相等且 CFlag[0]（SP 标志）== getSp 的角色返回下标
```

## 备注

- 文档只描述了"存在返回位置、不存在返回 -1"的核心语义；第 2 参数的具体含义（SP 角色兼容开关下的查找行为）仅见于源码，文档未展开。
- 源码中旧式的逐参数类型检查代码已被注释掉，改用 `argumentTypeArrayEx` 声明（1～2 个 int 参数，第 2 个可省略）。
- zh 套件（Era-Chinese-Documentation）未收录本函数。
