# GETCONFIG

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（EE 扩展）
- **签名**：int GETCONFIG(str configName)
- **文档来源**：`ecd/Expression.md`、`ecd/Command.md`、zh 套件均未收录；语义完全依据源码（`Runtime/Script/Statements/Function/Creator.Method.cs` 的 `GetConfigMethod` 与 `Runtime/Config/ConfigData.cs` 的 `GetConfigValueInERB`）

## 语义

按配置项名称（字符串）返回该配置项当前的整数值。可读取的配置项与 emuera.config / 设置界面中的项目对应（如 `WindowX`、`FontSize`、`PrintCPerLine`、`ForeColor` 等）。

配置项按其类型返回：

- 布尔型配置项：真返回 `1`，假返回 `0`。
- 整型/Int64 型配置项：直接返回其数值。
- 颜色型配置项（`ForeColor`/`BackColor`/`FocusColor`/`LogColor`）：返回 `(R*256+G)*256+B`，即 `0xRRGGBB`。
- 其余类型（字符串型、字符型、TextDrawingMode 等）返回的是字符串值——若用本函数读取会因类型不符而抛错，应改用 `GETCONFIGS`。

错误行为：参数为空字符串、配置项名称不存在、该配置项不允许用本函数读取时，抛出 CodeEE 运行期错误。

`GETCONFIG` 与 `GETCONFIGS` 是同一个实现类 `GetConfigMethod` 的两个实例（`typeisInt` 分别为 true/false），前者返回整数、后者返回字符串。

## 用法

### int GETCONFIG(str configName)
- `configName`：配置项名称（英文名，如 `"WindowX"`、`"ForeColor"`）。名称匹配不区分大小写（注册表使用 `Config.StrComper`，`GetItem` 内部还会 `ToUpper`）。
- 返回值：该配置项的整数值（布尔 1/0、整数、或颜色的 0xRRGGBB）。
```erb
W = GETCONFIG("WindowX")
PRINTL 窗口宽度为 {W}
C = GETCONFIG("ForeColor")
PRINTL 默认文字色为 0x{C,6:X}
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:139`（`["GETCONFIG"] = new GetConfigMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5015`（`GetConfigMethod`）；辅助实现 `Runtime/Config/ConfigData.cs:485`（`GetConfigValueInERB`）

```text
构造（typeisInt = true）：funcname = "GETCONFIG"；返回类型 = long；参数 = [string]；CanRestructure = true。

GetSingleTerm(exm, args):                       ; 两个函数共用
    str ← args[0].GetStrValue(exm)
    若 str 为 null 或空串:
        抛出 CodeEE（"第 1 参数是空字符串"）
    term, errMes ← ConfigData.GetConfigValueInERB(str)
        ; GetItem(str) 查找配置项；找不到则 errMes = "无效的配置名"
        ; 按配置项类型生成 SingleLongTerm / SingleStrTerm（规则见"语义"一节）
        ; 未被显式列举的配置项：若 ValueToString() 为 "YES"/"NO" 返回 1/0，
        ;   能解析为 long 则返回该 long，否则返回字符串值
        ;   都不行则 errMes = "该配置项不能被 GETCONFIG 读取"
    若 errMes != null:
        抛出 CodeEE(errMes)
    返回 term

GetIntValue(exm, args):
    若 ReturnType != long: 抛出 ExeEE（内部错误）
    term ← GetSingleTerm(exm, args)
    若 term 不是 SingleLongTerm:
        抛出 CodeEE（"类型不同，请使用 GETCONFIGS 函数"）
    返回 term.Int
```

## 备注

- **全部文档套件（ecd 与 zh）均未收录本函数**，本篇语义完全由源码翻译得出。
- 与 `GETCONFIGS` 共用同一个类，仅返回类型不同：读字符串型配置项时用 `GETCONFIG` 会抛出 CodeEE（提示改用 `GETCONFIGS`），反之亦然。
- 名称上与命令 `SETCONFIG` 系无关；本函数只读不写，且读取的是程序运行中的配置快照。
- 本仓库中该函数已正常注册（`Runtime/Script/Statements/Function/Creator.cs:139`），不存在"注册被注释"的情况。
