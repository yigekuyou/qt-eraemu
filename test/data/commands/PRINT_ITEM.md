# PRINT_ITEM

- **类别**：命令
- **签名**：PRINT_ITEM
- **文档来源**：`ecd/docs/translation/Command.md`（ecd 命令详解无独立小节）；`Era-Chinese-Documentation/docs/ERB_File_Format.md`「显示训练专用的数据」小节有简要说明

## 语义

显示当前主角（PLAYER）的持有物品一览：遍历 `ITEM` 数组，对每个数量非 0 的物品输出「物品名(数量) 」，全部拼接到固定前缀「所持アイテム：」之后，一行输出并**换行**。若一件物品都没有，则只输出「所持アイテム：なし」（无持有物品）。无参数。受 `SKIPDISP` 影响。

注意：与 `PRINT_SHOPITEM`（显示商店在售物品）不同，本命令显示的是 `ITEM`（持有数量）。物品名称取自 `ITEMNAME` CSV 变量。

## 用法

### PRINT_ITEM

- 无参数（VOID）。
- 输出格式：`所持アイテム：<ITEMNAME[i]>(<ITEM:i>) …`，最后换行；全空时输出 `所持アイテム：なし`。

```erb
PRINT_ITEM
; 例：输出「所持アイテム：回复药(3) 魔力之书(1)」
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:185`（`addFunction(FunctionCode.PRINT_ITEM, argb[VOID], METHOD_SAFE)`）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:66`
- 实现：`Runtime/Script/Process.ScriptProc.cs:212-218`（switch-case）；字符串拼装在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:865`（`GetHavingItemsString`）

```text
执行（Process.ScriptProc.cs）：
    若 skipPrint → break
    str = vEvaluator.GetHavingItemsString()：
        array     = ITEM 数组
        itemnames = ITEMNAME 数组
        length = min(array.Length, itemnames.Length)
        builder = "所持アイテム："
        count = 0
        对 i = 0 .. length-1：
            若 array[i] == 0 → continue        ← 数量 0 的物品不显示
            count++
            若 itemnames[i] != null → builder += itemnames[i]
            builder += "(" + array[i] + ") "
        若 count == 0 → builder += "なし"
        返回 builder
    Console.Print(str)
    Console.NewLine()
```

## 备注

- ecd 命令详解（Command.md）未收录独立小节；zh 文档仅在 ERB_File_Format.md 中说明「显示持有的物品」。
- 文档未写明、由源码确认的细节：输出带固定日文前缀「所持アイテム：」，数量写在物品名后的括号里，全部为 0 时输出「なし」；名称为 null 时仅输出 `(数量)`。
- zh/Flow.md 提到 `@PRINT_ITEMSHOP` 显示范围以 `ITEMNAME`/`ITEMSALES` 元素数较小者为准——那是 PRINT_SHOPITEM 的姊妹逻辑；PRINT_ITEM 对应的截断是 `ITEM` 与 `ITEMNAME` 长度取小。
