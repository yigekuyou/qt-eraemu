# GETDISPLAYLINE

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `GETDISPLAYLINE <行号>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・GETDISPLAYLINE」。ecd 套件与 zh 套件均未收录本命令。

## 语义

获取已由 `PRINT` 或 `HTML_PRINT` 显示出来的某一行的内容（字符串）。行号从 0 开始（显示行的内容保存在数组中），因此 `GETDISPLAYLINE(LINECOUNT)` 恒为空字符串；用 `LINECOUNT` 循环恰好能取到全部行。`HTML_PRINT` 显示的行返回带标签的原文，若只想要显示文本需与 `HTML_TOPLAINTEXT` 等函数组合。

行号越界（小于 0 或不小于已保存行数）时返回空字符串，不报错。

## 用法

### `GETDISPLAYLINE(<行号>)`
```erb
PRINTL 第一行
PRINTL 第二行
PRINTL 第三行
FOR LCNT, 0, LINECOUNT
    PRINTVL LCNT
    PRINTSL GETDISPLAYLINE(LCNT)
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:332`（`["GETDISPLAYLINE"] = new GetDisplayLineMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7381`（`GetDisplayLineMethod`，`#region EE_GETDISPLAYLINE`；`GetStrValue` 在 7393 行）

```text
构造: ReturnType = string
      argumentTypeArrayEx = [ [int] ]
      CanRestructure = false

GetStrValue(exm, arguments):
    num = arguments[0].GetIntValue(exm)
    # 注：源码中把「num 减去 Console.DeletedLines（已被 CLEARLINE 等删除的行数）」
    #     的修正尝试撤销了（注释「修正に失敗したので差し戻す」），num 即原始下标
    if num < 0 或 num >= Console.DisplayLineList.Count:
        return ""
    else:
        return Console.DisplayLineList[(int)num].ToString()   # 整行的字符串表示
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致。
- readme「=GETDISPLAYLINE(LINECOUNT)は常に空文字」对应源码的越界保护：`LINECOUNT` 正好等于行数上限，命中 `num >= Count` 分支返回 `""`。
- 源码注释表明曾经实现过「把参数当作从当前画面顶部起的行号（自动扣除已删除行数）」的语义，后因修正失败回退为直接对 `DisplayLineList` 下标取值；引用本函数的脚本应按「显示缓冲数组的绝对下标」理解。
- 返回的字符串是 `ConsoleDisplayLine.ToString()`：普通行为纯文本，`HTML_PRINT` 行含 HTML 标签。
