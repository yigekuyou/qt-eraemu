# 文件参考：CSV 与配置（CSV 部分）

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

> 来源：ecd/docs/CSV_File_Format.md（Eramaker 的 CSV 文件格式）、ecd/docs/Replace_CSV.md（关于 _replace.csv）、ecd/docs/CSV_File.md（CSV 文件参考）；交叉核对 zh 套件 CSV_File_Format.md、Replace_CSV.md、CSV_File.md

## 概述

CSV 文件是 Era 游戏的静态数据源，全部放在游戏根目录的 `CSV/` 文件夹中。Emuera 启动时按固定顺序读取它们：先 `GAMEBASE.CSV`（游戏基本信息），再 `VariableSize.CSV`（数组大小调整）、各名称注册类 CSV（给变量元素起名字）、`CHARA*.CSV`（角色初始数据），最后由 `_replace.csv` 调整系统显示文本。这些数据在 ERB 脚本运行前就固定下来，脚本通过「编号」或「CSV 中注册的名称」访问对应变量元素（如 `ABL:技巧`）。

本仓库源码为 emuera.em（EmueraEE 系分支），其 CSV 装载核心在 `Runtime/Script/Data/ConstantData.cs`，GameBase 解析在 `Runtime/Script/Data/GameBase.cs`，装载入口在 `Runtime/Script/Process.cs`。

## CSV 文件总览

| 文件 | 作用 |
|---|---|
| `GAMEBASE.CSV` | 游戏基本信息（代号、版本、标题、作者等） |
| `VariableSize.CSV` | 修改各数组变量的元素数 |
| `ABL.CSV` / `EXP.CSV` / `TALENT.CSV` / `PALAM.CSV` / `TRAIN.CSV` / `MARK.CSV` / `BASE.CSV` / `SOURCE.CSV` / `EX.CSV` / `EQUIP.CSV` / `TEQUIP.CSV` / `FLAG.CSV` / `TFLAG.CSV` / `CFLAG.CSV` / `TCVAR.CSV` / `CSTR.CSV` / `STAIN.CSV` / `CDFLAG1.CSV` / `CDFLAG2.CSV` | 名称注册类：第 1 栏编号、第 2 栏名称，用于给对应变量的元素起名（Eramaker 原生只有 Palam/Abl/Talent/Mark/Exp/Train/Item/Str，其余为 Emuera 及本分支扩展） |
| `ITEM.CSV` | 物品名称 + 价格（三栏） |
| `STR.CSV` | 全局字符串（编号可跳号） |
| `STRNAME.CSV` / `TSTR.CSV` / `SAVESTR.CSV` / `GLOBAL.CSV` / `GLOBALS.CSV` | 字符串变量的名称注册 |
| `DAY.CSV` / `TIME.CSV` / `MONEY.CSV` | 本分支（EE_CSV機能拡張）追加的名称注册 |
| `CHARA*.CSV` | 角色初始数据（可任意文件名，只要以 CHARA 开头） |
| `_replace.csv` | 替换系统显示文本（货币单位、系统菜单等） |
| `_Rename.csv` | EraMakerEx 的变量改名表（需配置开启） |
| `*.als` | 本分支扩展：与同名 CSV 配对的别名文件 |
| `VarExt*.csv` | 本分支扩展：存档扩展（XML/Map/DT）声明 |

全部 CSV 的装载入口见 `Runtime/Script/Data/ConstantData.cs:623`（`LoadData`）：

```text
// Runtime/Script/Data/ConstantData.cs:623-696
void LoadData(csvDir, console, disp):
    loadVariableSizeData(csvDir + "VariableSize.CSV")        // :626
    为每个名称类 CSV 分配 names[] / nameToIntDics[]
    loadDataTo("ABL.CSV", ablIndex) ... 逐个读取上表全部名称类 CSV   // :634-665
    为 names 构建逆序字典（名称 → 编号），再并入 .als 别名           // :667-694
    loadCharacterData(csvDir)                                 // :696 → :1239
    loadGlobalVarExSetting(csvDir)                            // VarExt*.csv，:1281
    用 Name/Callname/Nickname/Mastername 构建 relationDic（RELATION 的名字反查） // :702-713
```

文件名不区分大小写匹配（Windows 环境），例如文档写 `GameBase.csv`，源码按 `GAMEBASE.CSV` 打开（`Runtime/Script/Process.cs:157`）。

## CSV 通用书写规则

- 每行用半角逗号 `,` 分栏；第 1 栏以半角分号 `;` 开头的行视为注释，空行同样忽略。
- 不要用 `""` 包住字符串——Emuera 不做 CSV 引号转义，引号会原样进入数据。
- 注释/空行过滤由 `EraStreamReader.ReadEnabledLine()` 完成（各装载函数的 `while ((st = eReader.ReadEnabledLine()) != null)` 循环，如 `Runtime/Script/Data/ConstantData.cs:1405`、`:1714`）。

```text
;体力和精神力的设置      ← 注释行，忽略
基礎,0,2000
基礎,1,1000
```

## GameBase.csv

第 1 栏为指令名，第 2 栏（及以后）为数据。解析实现：`Runtime/Script/Data/GameBase.cs:88-191`（`LoadGameBaseCsv`），文件不存在时静默跳过（`:90-93`）。

| 指令 | 含义 | 源码分支 |
|---|---|---|
| `コード` | 游戏代号，防止误读其它游戏的存档。为 0 时发警告（`:117`） | `:113` |
| `バージョン` | 游戏版本（内部整数，显示值 = 值/1000，见下） | `:120` |
| `バージョン違い認める` | 允许读取的最低存档版本（向下兼容） | `:123` |
| `最初からいるキャラ` | 游戏开始时默认在场的角色编号 | `:126` |
| `アイテムなし` | 为 1 时禁用内置物品系统 | `:129` |
| `タイトル` | 游戏标题 | `:132` |
| `作者` | 游戏作者 | `:135` |
| `製作年` | 制作年份 | `:138` |
| `追加情報` | 附加信息 | `:141` |
| `ウィンドウタイトル`（Emuera 扩展） | 窗口标题；未指定时为「标题 + 版本串」，标题也为空时为 `Emuera`（`:183-189`） | `:144` |
| `動作に必要なEmueraのバージョン`（Emuera 扩展） | 要求的 Emuera 版本，格式 `x.y.z.w`；当前版本更低时警告并中止装载（`:150-161`） | `:148` |
| `バージョン情報URL` / `バージョン名`（EE 扩展） | UPDATECHECK 更新检查用的 URL 与版本名 | `:164-169` |

版本号的显示逻辑（`Runtime/Script/Data/GameBase.cs:32-45`）：`ScriptVersionText = (V/1000) + "." + (V%1000 按两位或三位补零)`，即 `54321` 显示为 `54.321`。

存档版本检查（`Runtime/Script/Data/GameBase.cs:54-61` `CheckVersion`）：存档版本 ≥ `バージョン違い認める` 即可读取；否则必须与 `バージョン` 完全相等。未定义 `バージョン` 时视为 0，但存档版本为 1000（旧 eramaker 默认）时也视为相同（`:19-22` 的注释）。

## 名称注册类 CSV（Palam / Abl / Talent / Mark / Exp / Train / Item / Str 及扩展）

- 第 1 栏为编号，第 2 栏为名称；`ITEM.CSV` 额外有第 3 栏价格。
- Eramaker 时代各文件编号上限为 99（Str 为 19999）；Emuera 中上限由 `VariableSize.CSV` 或内部默认值决定，注册越界会报「数组越界」警告（`Runtime/Script/Data/ConstantData.cs:1734-1738`）。
- 建议从 0 开始连续编号，不留空号（空号不报错，只是无法用名称引用）。
- 编号重复时警告（`:1739-1740`）。

解析实现 `loadDataTo`（`Runtime/Script/Data/ConstantData.cs:1686-1774`）：

```text
// Runtime/Script/Data/ConstantData.cs:1714-1752
while line = ReadEnabledLine():
    parts = line.Split(',')                 // 最多取前 3 栏（stackalloc Range[5]）
    if parts.Length < 2          → 警告“缺少逗号”，跳过
    if 第1栏不能转 int           → 警告，跳过
    if 编号 < 0 或 ≥ 数组长度    → 警告越界，跳过
    if 编号已定义               → 警告重复
    names[目标][编号] = 第2栏
    if 目标是 ITEM 且有第3栏:    ItemPrice[编号] = 第3栏（解析失败警告）  // :1742-1752
读完后尝试读取同名 .als 别名文件                                  // :1769-1773
```

注册后，脚本里既可写编号也可写名称：`ABL:3` 等价于 `ABL:技巧`（名称→编号的逆查字典在 `Runtime/Script/Data/ConstantData.cs:667-694` 构建，`TryKeywordToInteger` 在 `:830` 提供查询）。

`ITEM.CSV` 的价格对应变量 `ITEMPRICE`；商店显示范围由 `ITEMNAME`/`ITEMSALES`/`ITEMPRICE` 三者数组长度取最小值决定（`Runtime/Script/Process.ScriptProc.cs:222-224`）。

### Str.csv 中的占位符

`STR.CSV` 注册的字符串中，以下三连符号在输出时被替换（Eramaker 规格继承）：

| 符号 | 含义 |
|---|---|
| `+++` | 主角姓名（`NAME:MASTER`） |
| `***` | 被训练者姓名（`NAME:TARGET`） |
| `$$$` | 被训练者称呼（`CALLNAME:TARGET`） |
| `///` | 助手姓名（`NAME:ASSI`） |
| `===` | 训练者姓名（主角或助手） |

## CHARA*.CSV（角色定义）

角色文件按 `CHARA*.CSV` 模式搜索（受「搜索子目录」「按文件名排序」两个配置影响），见 `Runtime/Script/Data/ConstantData.cs:1243`。一个角色可分散在多个文件中定义；同一编号重复定义时后读的忽略并警告（`:1267-1276`）。角色装载后按 `No` 排序（`:1459`）。

### 全部列（字段）

解析实现在 `toCharacterTemplate`（`Runtime/Script/Data/ConstantData.cs:1521-1684`），支持日文别名：

| 字段 | 日文别名 | 对应角色变量 | 说明 |
|---|---|---|---|
| `NAME` | `名前` | `NAME` | 角色名（`:1535-1538`） |
| `CALLNAME` | `呼び名` | `CALLNAME` | 称呼（`:1539-1542`） |
| `NICKNAME` | `あだ名` | `NICKNAME` | 绰号（Emuera 扩展，`:1543-1546`） |
| `MASTERNAME` | `主人の呼び方` | `MASTERNAME` | 主人的称呼（Emuera 扩展，`:1547-1550`） |
| `BASE` | `基礎` | `BASE`/`MAXBASE` | 基础属性初始值与上限（`:1572-1578`） |
| `ABL` | `能力` | `ABL` | 能力（`:1565-1571`） |
| `TALENT` | `素質` | `TALENT` | 素质（`:1579-1585`） |
| `EXP` | `経験` | `EXP` | 经验（`:1558-1564`） |
| `MARK` | `刻印` | `MARK` | 刻印（`:1551-1557`） |
| `RELATION` | `相性` | `RELATION` | 对其它角色的相性；第 2 栏是对方角色编号（`:1586-1591`，无名称字典） |
| `CFLAG` | `フラグ` | `CFLAG` | 角色标志（`:1592-1598`） |
| `EQUIP` | `装着物` | `EQUIP` | 初始装备（Emuera 扩展，`:1599-1605`） |
| `JUEL` | `珠` | `JUEL` | 宝珠初始值（Emuera 扩展，名称用 PALAM 的字典，`:1606-1612`） |
| `CSTR` | （无日文别名） | `CSTR` | 角色字符串变量（Emuera 扩展，`:1613-1618`） |
| `ISASSI` | `助手` | — | 本分支中**被忽略**（解析到后直接 return，`:1619-1621`）；兼容性配置「使用 SP 角色」关闭时，`CFLAG:0` 不再作为 SP 标记（见下） |

### 角色编号（`番号` / `NO`）

每个文件以 `番号,<编号>` 开始；它之前的其它数据行会触发「在角色编号前出现数据」警告（`Runtime/Script/Data/ConstantData.cs:1451-1454`）。同一文件里写两次 `番号` 会警告（`:1422-1426`）。

文件名中 `CHARA` 之后的连续数字被提取为 `csvNo`（`CSV` 文件序号，供 `CSVNAME` 类指令按文件取名用），无数字则为 0（`:1433-1445`）——因此 `番号` 与文件名序号 `CharaXX` 是两个独立的值。

### 数值行格式与名称引用

```text
// Runtime/Script/Data/ConstantData.cs:1636-1683
p1 = tryToInt64(第2栏)                 // 第2栏可以是编号，也可以是名称类 CSV 注册的名字
if p1 越界 → 警告
if 第2栏不是数字 且 该变量有名称字典 → 按名称查编号（查不到警告“未定义的键”）
if 是字符串变量（CSTR）:
    必须有第3栏（否则警告），strArray[编号] = 第3栏        // :1668-1675
else:
    第3栏省略或非数字时默认为 1（素质行「TALENT,4」即置 1）  // :1678-1679
    intArray[编号] = 第3栏
```

示例：

```csv
番号,1
名前,小明
呼び名,魔王
基礎,0,2000
能力,0,10
素質,2
; 也可以用名称引用（需在 ABL.CSV 等中注册过）
能力,技巧,5
```

### SP 角色

`CFLAG:0` 非 0 的角色被标记为 SP 角色（`Runtime/Script/Data/ConstantData.cs:1905-1910` `SetSpFlag`）。仅在兼容性配置 `SPキャラを使用する` 开启时 SP 角色进入独立列表，否则与普通角色同等对待（`:1260-1277`）。

## VariableSize.csv

格式：`变量名,元素数`（二维/三维变量可写 `变量名,长度1,长度2[,长度3]`）。实现在 `Runtime/Script/Data/ConstantData.cs:219-256`（`loadVariableSizeData`）与 `:259-402`（`changeVariableSizeData`）。

限制与联动：

- 每维长度必须 ≥ 1，单维 ≤ 1,000,000；三维乘积 ≤ 10,000,000（`:386-401`）。
- `ITEMNAME`/`ITEMPRICE` 改动会同步：两者与 `ITEMNAME` 的名称数组同长（`:408-412`）。
- `STR` 改动同步字符串数组长度（`:413-415`）。
- `PALAMNAME` 与 `PALAM`/`JUEL`、`CDFLAGNAME1/2` 与 `CDFLAG` 之间有同步与警告逻辑（`decideActualArraySize`，`:520-620`）：例如 `PALAMNAME` 数量与 `PALAM`/`JUEL` 大小不一致时按较大者对齐并发 Lv1 警告（`:560-588`）。
- 修改 `ITEMNAME` 的大小会改变 `PRINT_SHOPITEM` 的显示范围（见 _replace.csv 一节）。

## _Replace.csv

放在 `CSV/` 文件夹中，用于定制系统显示文本。仅当配置 `_Replace.csvを利用する` 为 YES 时读取（默认 YES）。装载入口：`Runtime/Script/Process.cs:109-133`，解析实现：`Runtime/Config/ConfigData.cs:803-841`（`LoadReplaceFile`）。

```text
// Runtime/Runtime/Config/ConfigData.cs:812-830
while line = ReadLine():
    if 空行或以 ';' 开头 → 跳过
    tokens = line.Split(',', ':')            // 分隔符可用逗号或冒号
    itemName = tokens[0].Trim()
    value    = 第一个分隔符之后的全部内容     // 值中可再含逗号
    item = GetReplaceItem(itemName)
    if item != null → item.TryParse(value)
```

全部条目与默认值（定义于 `Runtime/Config/ConfigData.cs:186-201`）：

| 条目 | 默认值 | 作用 |
|---|---|---|
| `お金の単位`（金钱单位） | `$` | `PRINT_SHOPITEM` 价格与 `MONEY` 系输出附加的单位，可用全角/多字符 |
| `単位の位置`（单位位置） | `後`（单位在后） | `前`/`後`，决定单位在数字前还是后 |
| `起動時簡略表示`（启动时简略显示） | `Now Loading...` | 关闭「加载时显示报告」时替代显示的字符串（`Runtime/Script/Process.cs:150-154`） |
| `販売アイテム数`（出售的物品数量） | `100` | 购买处理的物品编号上限：`INPUT` 值 < 此数走购买/`@EVENTBUY`，否则调用 `@USERSHOP`（`Runtime/Script/Process.SystemProc.cs:737`） |
| `DRAWLINE文字`（DRAWLINE 字符） | `-` | `DRAWLINE` 用的分隔线字符；读到的值为空串时重置为 `-`（`Runtime/Config/Config.cs:570-571`） |
| `BAR文字1` / `BAR文字2`（BAR 字符） | `*` / `.` | `BAR`/`BARL` 指令的满格/空格字符 |
| `システムメニュー0` / `システムメニュー1`（系统菜单） | `最初からはじめる` / `ロードしてはじめる` | 标题画面（未自定义 `@SYSTEM_TITLE` 时）的两个选项文字 |
| `COM_ABLE初期値`（COM_ABLE 初始值） | `1` | `TRAIN` 中找不到 `@COM_ABLE{X}` 时该指令是否可用（`Runtime/Script/Process.SystemProc.cs:386-388`） |
| `汚れの初期値`（污渍初始值） | `0, 0, 2, 1, 8` | `STAIN` 初始化值，用 `/` 分隔 `STAIN:1` 之后的值（`Runtime/Script/Statements/Variable/VariableEvaluator.cs:1665-1674`） |
| `時間切れ表示`（超时显示） | `時間切れ` | `TINPUT` 等限时输入超时显示的字符串 |
| `EXPLVの初期値`（EXPLV 初始值） | `0, 1, 4, 20, 50, 200` | 经验升级阈值，`/` 分隔 `EXPLV:1` 之后（`Runtime/Script/Statements/Variable/VariableData.cs:650`） |
| `PALAMLVの初期値`（PALAMLV 初始值） | `0, 100, 500, 3000, 10000, 30000, 60000, 100000, 150000, 250000` | 参数升级阈值（`Runtime/Script/Statements/Variable/VariableData.cs:636`） |
| `PBANDの初期値`（PBAND 初始值） | `4` | `PBAND:0` 初始值（`Runtime/Script/Statements/Variable/VariableData.cs:667`） |
| `RELATIONの初期値`（RELATION 初始值） | `0` | `Chara*.csv` 未指定 `RELATION` 时的初始值（`Runtime/Script/Statements/Variable/CharacterData.cs:126`） |

注意（继承自原版 wiki）：输入 0～（販売アイテム数-1）之间的值时，无论购买是否成功都不会调用 `@USERSHOP`；要增减 `PRINT_SHOPITEM` 显示数量应修改 `VariableSize.CSV` 中 `ITEMNAME`/`ITEMSALES` 的元素数。

金钱单位的实际应用点：`PRINT_SHOPITEM`（`Runtime/Script/Process.ScriptProc.cs:235-238`）、表达式函数中的金额格式化（`Runtime/Script/Statements/Function/Creator.Method.cs:2825,2837`）。

## _Rename.csv 与 VarExt*.csv

- `_Rename.csv`：EraMakerEx 的变量改名表，需在配置中开启 `_Rename.csvを利用する`（默认 NO）；装载在 `Runtime/Script/Process.cs:137-147`，由 `ParserMediator.LoadEraExRenameFile` 处理。
- `VarExt*.csv`（本分支扩展）：声明哪些 `GLOBAL_MAPS`/`SAVE_MAPS`/`GLOBAL_XMLS`/`SAVE_XMLS`/`GLOBAL_DTS`/`SAVE_DTS`/`STATIC_XMLS`/`STATIC_MAPS`/`STATIC_DTS` 随存档保存（`Runtime/Script/Data/ConstantData.cs:1281-1388`）。

## 与源码的差异/备注

1. **文件名大小写**：文档写 `GameBase.csv`、`CharaXX.csv`，源码按 `GAMEBASE.CSV`（`Runtime/Script/Process.cs:157`）与 `CHARA*.CSV` 模式（`Runtime/Script/Data/ConstantData.cs:1243`）匹配，Windows 下不区分大小写；角色文件名只需以 `CHARA` 开头，`XX` 不限于两位数字（`csvNo` 提取连续数字，`:1433-1445`）。
2. **`助手`/`ISASSI` 列被忽略**：ecd 文档（继承 eramaker）说「若其值为 1，该角色初始化后默认为助手」；emuera.em 源码中该行解析后直接丢弃（`Runtime/Script/Data/ConstantData.cs:1619-1621`），助手指定须由脚本完成。
3. **名称注册类 CSV 大幅扩展**：ecd/CSV_File_Format.md 只覆盖 eramaker 的 8 类文件；源码还装载 BASE/SOURCE/EX/EQUIP/TEQUIP/FLAG/TFLAG/CFLAG/TCVAR/CSTR/STAIN/CDFLAG1/CDFLAG2/STRNAME/TSTR/SAVESTR/GLOBAL/GLOBALS（`Runtime/Script/Data/ConstantData.cs:634-660`）以及 DAY/TIME/MONEY（EE 扩展，`:663-665`）。ecd 新写的 `CSV_File.md` 页补充了其中一部分（VariableSize.csv、_replace.csv、_rename.csv）。
4. **GameBase 扩展字段**：文档未记载 `ウィンドウタイトル`、`動作に必要なEmueraのバージョン`、`バージョン情報URL`、`バージョン名`（`Runtime/Script/Data/GameBase.cs:144-169`）。
5. **`.als` 别名文件与 VarExt*.csv** 为本分支特有，两套文档均未记载。
6. **编号上限**：文档称各编号上限 99（Str 19999）；Emuera 实际上限取决于数组大小（默认值在 `setDefaultArrayLength`，如 ABL 100、TALENT 1000，`Runtime/Script/Data/ConstantData.cs:137-140` 起），越界按警告处理而非硬限制。
7. **zh 套件笔误**：zh/CSV_File_Format.md 把被训练者称呼的占位符写作 `$$`（两个 `$`），ecd 版与 ecd/CSV_File.md 均为 `$$$`，以 ecd 为准。
8. **销售物品数默认值**：ecd/Replace_CSV.md 未给默认值，源码为 `100`（`Runtime/Config/ConfigData.cs:189`）。
9. **`CSTR` 行**：必须写满三栏（编号, 空串也算缺第三栏时仅警告），值按字符串原样保留（`Runtime/Script/Data/ConstantData.cs:1668-1675`）；重复赋值同编号会警告。
