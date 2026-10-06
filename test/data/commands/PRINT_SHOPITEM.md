# PRINT_SHOPITEM

- **类别**：命令
- **签名**：`PRINT_SHOPITEM`（无参数）
- **文档来源**：`ecd/docs/translation/Command.md` 未收录独立小节；语义见 `Era-Chinese-Documentation/docs/Replace_CSV.md`（「在 Eramaker 通常的 `SHOP` 处理中，`PRINT_SHOPITEM` 会显示所有 `ITEMSALES` 非 0 的 `ITEMNAME`（0～999）」等）与 `ecd/EraBasic_Structure.md:160`（「调用 `PRINT_SHOPITEM` 显示出售中的物品」）；实现细节来自源码。

## 语义

在 `@SHOW_SHOP` 中显示当前出售中的物品列表：对 0～`ITEMSALES/ITEMNAME/ITEMPRICE` 三数组长度最小值-1 的每个物品编号 `i`，若 `ITEMSALES:i` 非 0（且 `ITEMNAME:i` 已定义，见 `ItemSales`），则以 `PRINTC` 格式输出 `[编号] 物品名(价格+货币单位)`。

- 货币单位取自 `Replace.csv` 的 `お金の単位`（`Config.MoneyLabel`）；
- 单位位置由 `単位の位置`（`Config.MoneyFirst`）决定：前置时输出 `[i] 名(单位价格)`，后置时输出 `[i] 名(价格单位)`；
- 物品名为 null 时按空串处理。

每打印 `PRINTC 并列数`（`PRINTCPERLINE` 设置）个换一次行，最后再整体换行并刷新显示。通常在 `BEGIN SHOP` 后由 `@SHOW_SHOP` 调用。

显示的物品范围由 `VariableSize.csv` 中 `ITEMNAME` 与 `ITEMSALES`（源码中还受 `ITEMPRICE` 限制）数组元素数量较小的一方决定。

## 用法

### `PRINT_SHOPITEM`
无参数。

```erb
@SHOW_SHOP
PRINT_SHOPITEM        ; 列出所有 ITEMSALES 非 0 的商品：[编号] 名字(价格￥)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:186`（`argb[FunctionArgType.VOID], METHOD_SAFE`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:218-247`（switch-case `FunctionCode.PRINT_SHOPITEM`）；`ItemSales` 在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:1711-1722`

```text
若处于 skipPrint 状态：什么都不做。

length = min(ITEMSALES.Length, ITEMNAME.Length)
若 length > ITEMPRICE.Length：length = ITEMPRICE.Length
count = 0
对 i 从 0 到 length-1：
    若 vEvaluator.ItemSales(i)（即 ITEMSALES[i] != 0 且 CSV 的 ITEMNAME[i] 非 null）：
        s = ITEMNAME[i]；若为 null 则 s = ""
        price = ITEMPRICE[i]
        若 Config.MoneyFirst（单位前置）：
            控制台.PrintC(string.Format("[{2}] {0}({3}{1})", s, price, i, MoneyLabel), false)
        否则（单位后置）：
            控制台.PrintC(string.Format("[{2}] {0}({1}{3})", s, price, i, MoneyLabel), false)
        count++
        若 Config.PrintCPerLine > 0 且 count % PrintCPerLine == 0：PrintFlush（换行）
控制台.PrintFlush(false)；控制台.RefreshStrings(false)
```

## 备注

- `ecd/Command.md` 无独立小节。「显示所有 `ITEMSALES` 非 0 的 `ITEMNAME`（0～999）」中 999 的上限来自原版 eramaker 的数组大小；本仓库源码用三数组长度最小值动态决定范围，并额外用 `ITEMPRICE.Length` 截断——这是文档与源码的细微差异。
- 文档（Replace_CSV.md）只描述了价格单位附加（`お金の単位`）与前置/后置（`単位の位置`），输出格式 `[编号] 名字(价格)` 仅见于源码。
- `zh/Command.md` 未收录；`zh/Replace_CSV.md`、`zh/EraBasic_Structure.md` 有与 ecd 相同的描述。
