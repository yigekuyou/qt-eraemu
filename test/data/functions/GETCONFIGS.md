# GETCONFIGS

- **类别**：式中函数（EE 扩展）
- **签名**：str GETCONFIGS(str configName)
- **文档来源**：`ecd/Expression.md`、`ecd/Command.md`、zh 套件均未收录；语义完全依据源码（`Runtime/Script/Statements/Function/Creator.Method.cs` 的 `GetConfigMethod` 与 `Runtime/Config/ConfigData.cs` 的 `GetConfigValueInERB`）

## 语义

按配置项名称（字符串）返回该配置项当前的字符串值。用于读取字符串型配置项，如 `FontName`（字体名）、`MoneyLabel`（金钱单位）、`LoadLabel`（起動時簡略表示）、`DrawLineString`（DRAWLINE 文字）、`TitleMenuString0/1`（系统菜单文字）、`TimeupLabel`（时间切れ显示）、`BarChar1/BarChar2`（BAR 文字，单个字符的字符串）、`TextDrawingMode`（描画接口名）等。

`GETCONFIGS` 与 `GETCONFIG` 是同一个实现类 `GetConfigMethod` 的两个实例（`typeisInt` 分别为 false/true）：本函数返回字符串，`GETCONFIG` 返回整数。若用本函数读取整数/布尔/颜色型配置项（得到的 term 不是字符串型），会抛出 CodeEE（提示改用 `GETCONFIG`）。

错误行为：参数为空字符串、配置项名称不存在、该配置项不允许读取时，抛出 CodeEE。

## 用法

### str GETCONFIGS(str configName)
- `configName`：配置项名称（英文名）。匹配不区分大小写。
- 返回值：该配置项的字符串值。
```erb
PRINTL 当前字体：{GETCONFIGS("FontName")}
PRINTL 金钱单位：{GETCONFIGS("MoneyLabel")}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:140`（`["GETCONFIGS"] = new GetConfigMethod(false)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5015`（`GetConfigMethod`，与 `GETCONFIG` 共用）；辅助实现 `Runtime/Config/ConfigData.cs:485`（`GetConfigValueInERB`）

```text
构造（typeisInt = false）：funcname = "GETCONFIGS"；返回类型 = string；参数 = [string]；CanRestructure = true。

GetSingleTerm(exm, args):                       ; 与 GETCONFIG 完全共用
    str ← args[0].GetStrValue(exm)
    若 str 为 null 或空串:
        抛出 CodeEE（"第 1 参数是空字符串"）
    term, errMes ← ConfigData.GetConfigValueInERB(str)
        ; 查找配置项；不存在 → errMes = "无效的配置名"
        ; 字符串型/字符型/TextDrawingMode 型配置项 → SingleStrTerm
        ; 布尔/整型/颜色/Int64 型配置项 → SingleLongTerm（GETCONFIGS 读到它会报类型错）
    若 errMes != null:
        抛出 CodeEE(errMes)
    返回 term

GetStrValue(exm, args):
    若 ReturnType != string: 抛出 ExeEE（内部错误）
    term ← GetSingleTerm(exm, args)
    若 term 不是 SingleStrTerm:
        抛出 CodeEE（"类型不同，请使用 GETCONFIG 函数"）
    返回 term.Str
```

## 备注

- **全部文档套件（ecd 与 zh）均未收录本函数**，本篇语义完全由源码翻译得出。
- 与 `GETCONFIG` 共用同一个类，按返回类型（int/str）分为两个注册名；配置项的"允许读取清单"与类型到返回值的映射规则详见 GETCONFIG.md。
- 本仓库中该函数已正常注册（`Runtime/Script/Statements/Function/Creator.cs:140`），不存在"注册被注释"的情况。
