# BITMAP_CACHE_ENABLE

- **类别**：式中函数（本树 `emuera.em` 独有：Bitmap Cache 扩展）
- **签名**：int BITMAP_CACHE_ENABLE(int 启用标志)
- **文档来源**：两套中文文档（`ecd/`、`_extracted/zh/`）与 EM/EE readme 均未收录，语义据源码

## 语义

设置「**下一行**打印时的位图缓存开关」，恒返回 `0`。

- 参数非 0 → 打开缓存标记；参数为 0 → 关闭（`Runtime/Script/Statements/Function/Creator.Method.cs:7582`）。
- 影响范围只在**紧接着绘制的那一行**：控制台在生成显示行对象时读取该标记并写到行对象上（`UI/Game/EmueraConsole.Print.cs:216`：`line.bitmapCacheEnabled = GlobalStatic.Console.bitmapCacheEnabledForNextLine;`）。标记本身在字段里不会自动复位（`UI/Game/EmueraConsole.cs:84` 的 `bitmapCacheEnabledForNextLine`），因此**后续所有行都会沿用同一设置**，直到再次调用本函数切换。
- 位图缓存作用于文字绘制（把整行文字渲染结果缓存为位图以加速重绘），属于显示性能优化；不改变任何游戏逻辑状态，也不影响 `RESULTS`/`RESULT`。
- 返回值恒为 `0`（不是设置结果），所以不能用于读取当前状态。

## 用法

### int BITMAP_CACHE_ENABLE(int 启用标志)
- 启用标志：非 0 → 启用；0 → 停用。
- 返回值：恒 `0`。
```erb
BITMAP_CACHE_ENABLE 1
PRINTL 这一行启用位图缓存
BITMAP_CACHE_ENABLE 0
PRINTL 这一行不启用
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:347`（`["BITMAP_CACHE_ENABLE"] = new BitmapCacheEnableMethod()`，位于 `//Bitmap Cache` 注释下）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7568`（`BitmapCacheEnableMethod`）
- 消费点：`UI/Game/EmueraConsole.Print.cs:216`；字段定义 `UI/Game/EmueraConsole.cs:84`

```text
构造（Creator.Method.cs:7570-7578）:
    返回类型 = long
    argumentTypeArrayEx = [{ Int }, OmitStart = 1]
    ; 注意：ArrayTypes 只有 1 个元素且 OmitStart = 1，
    ; 故实参个数下界等于 OmitStart = 1 —— 第 1 参数实际仍必须给出
    即参数表为「1 个整数」，写法上第 1 参必填；CanRestructure = false

GetIntValue(exm, args)（Creator.Method.cs:7579-7584）:
    flag = args[0].GetIntValue(exm)
    GlobalStatic.Console.bitmapCacheEnabledForNextLine = (flag != 0)
    return 0
```

## 备注

- 无任何文档可对照；本函数只在本树（`emuera.em`）的 `Runtime/Script/Statements/Function/Creator.cs` 中注册，另一棵树 `Emuera/GameData/Function/Creator.cs` 没有它（可 grep 回读验证）。
- 名字里的 "NEXT" 语义并未在字段名体现（字段名 `...ForNextLine`），但由于代码从不复位该字段，「下一行」在实践中等同于「此后所有行」——这是读源码才能发现的点（推定：如果没有其它代码改它，设置即持久）。
- 返回值无意义（恒 0），不要写 `IF BITMAP_CACHE_ENABLE(1) == 1`。
- 作为**语句**调用（如示例）也合法：所有式中函数都被登记为 `METHOD_SAFE | EXTENDED` 的指令，语句形态下返回值写入 `RESULT`（字符串返回型写 `RESULTS`），见 `Runtime/Script/Statements/Instraction.Child.cs:579-598`（`METHOD_Instruction`）、`Runtime/Script/Statements/FunctionIdentifier.cs:458`（所有式中函数注册为 `methodInstruction`，其 flag 于 `Runtime/Script/Statements/Instraction.Child.cs:584` 为 `METHOD_SAFE | EXTENDED`）。
- 与本函数同区的还有 `HOTKEY_STATE`、`HOTKEY_STATE_INIT`（`Runtime/Script/Statements/Function/Creator.cs:350-351`）。
- 本仓库移植版（`src/eraengine/`）未实现本函数。
