# GETSPCHARA

- **类别**：式中函数（ecd/Command.md 以命令口径收录；本仓库仅有函数形态）
- **签名**：int GETSPCHARA(int no)
- **文档来源**：`ecd/Command.md`「### GETSPCHARA `<角色编号>`」；`ecd/Expression.md`（表达式内函数签名列表）；zh 套件未收录

## 语义

判断当前拥有的 SP 角色中是否存在该角色：存在则返回其在角色列表中的位置（索引），不存在则返回 `-1`。

与 `GETCHARA` 的区别：GETCHARA 在整个角色列表中查找，而 GETSPCHARA 只匹配"SP 角色"（以 `CFLAG:0` 非 0 标记）。从 ver1.816 起 Emuera 默认不再支持 SP 角色，调用本函数前必须启用兼容性选项「SPキャラを使用する / 使用 SP 角色」，否则抛出 CodeEE 运行期错误。

## 用法

### int GETSPCHARA(int no)
- `no`：整数表达式，角色编号（`NO`，即 csv 中定义的编号，不是列表索引）。
- 返回值：该编号的 SP 角色在角色列表中的索引；不存在时 `-1`。
- 前提：兼容性选项「使用 SP 角色」为 ON，否则运行期错误。
```erb
I = GETSPCHARA(10)
IF I >= 0
    PRINTFORML SP 角色（编号 10）位于列表第 {I} 位
ELSE
    PRINTL 不存在该编号的 SP 角色
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:17`（`["GETSPCHARA"] = new GetspcharaMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2046`（`GetspcharaMethod`）；转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1336`（`VariableEvaluator.GetChara_UseSp`）

```text
构造：返回类型 = long；参数 = [long]；CanRestructure = false。

GetIntValue(exm, args):
    若 !Config.CompatiSPChara:
        抛出 CodeEE（"SP 角色相关功能在标准设置下不可用（请把兼容性选项「使用 SP 角色」设为 ON）"）
    integer ← args[0].GetIntValue(exm)
    返回 exm.VEvaluator.GetChara_UseSp(integer, true)

GetChara_UseSp(charaNo, getSp):               # VariableEvaluator.cs:1336
    # 考虑后天变更 NO 的情况，不检查 chara*.csv 中是否有定义
    对 i = 0 .. CharacterList.Count-1:
        若 CharacterList[i].NO == charaNo:
            isSp ← (CharacterList[i].CFlag[0] != 0)
            若 isSp == getSp:
                返回 i                         # GETSPCHARA 时 getSp 恒为 true
    返回 -1
```

## 备注

- ecd/Command.md 的小节按命令口径描述（原版 Emuera 中 SP 角色相关指令在 1.816 后受兼容开关限制）；本仓库 `Runtime/Script/Statements/FunctionIdentifier.cs` 中没有 GETSPCHARA 命令注册，只有式中函数形态。
- 源码实现与文档一致：只匹配 `CFlag[0] != 0`（SP 标记）的角色；同一编号的普通角色即使存在也会被跳过（继续向后查找），文档未写明这一点。
- 兼容性选项名见 `ecd/Compatibility.md`：「SPキャラを使用する」，及表格「使用了 SP 角色」（Compatibility.md:214）、「存档|SP 角色」（:191）。
- 对比同文件上方 `GetcharaMethod`（`Runtime/Script/Statements/Function/Creator.Method.cs` 约 :2033 起）：GETCHARA 调 `GetChara_UseSp(integer, false)`（或关闭兼容时按普通查找），GETSPCHARA 调 `GetChara_UseSp(integer, true)`。
- zh 套件未收录本函数。
