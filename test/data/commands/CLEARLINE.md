# CLEARLINE

- **类别**：命令
- **签名**：`CLEARLINE <行数>`
- **文档来源**：`ecd/docs/translation/Command.md`「CLEARLINE」小节；`Era-Chinese-Documentation` 的 `HTML_PRINT.md`/`Variable.md` 仅侧面提及行数计算，无独立条目

## 语义

删除（清除）屏幕上最近输出的指定行数的文本。行数的计算方法与 `LINECOUNT` 相同：凡引起换行的操作（如 `PRINTL`）计 1 行，文本过长被自动折行时也计 1 行。常与 `REUSELASTLINE` 配合处理用户无效输入：先 `CLEARLINE 1` 清掉输入行再重新提示。删除从最新的一行开始向上进行；若要求删除的行数超过现存行数，则把能删的都删掉（剩余部分按实现把逻辑行计数减到负值方向校正，不会崩溃）。

## 用法

### `CLEARLINE <行数>`
- 行数：整数表达式，指定要删除的显示行数。
```erb
PRINTL 第一行
PRINTL 第二行
PRINTL 第三行
CLEARLINE 2      ; 删除"第二行""第三行"两行
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:205` → `new CLEARLINE_Instruction()`（flag = METHOD_SAFE | EXTENDED | IS_PRINT）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:676`（`CLEARLINE_Instruction`）；实际删行在 `UI/Game/EmueraConsole.Print.cs:253`（`deleteLine`）

```text
DoInstruction(exm, func, state):
    delNum = (int)((ExpressionArgument)func.Argument).Term.GetIntValue(exm)
    exm.Console.deleteLine(delNum)
    exm.Console.RefreshStrings(false)      // 请求画面重绘

deleteLine(argNum):                        // 语义摘要
    若启用剪贴板复制(CBUseClipboard)，同步从剪贴板缓冲删行
    从显示行列表末尾向上逐行移除:
        每移除一行 lineNo--
        该行是"逻辑行"时 delNum++（自动折行产生的物理行不计数），
        且该行是逻辑行结尾时 logicalLineCount--
        列表被删空则提前结束
    若 delNum 仍 < argNum（行数不够删），把 lineNo 归 0、logicalLineCount 再减去差额
    非 WINAPI 绘制模式下把 lineNo 位置从局部重绘缓存中移除
```

## 备注

- ecd 文档强调"文本过长被自动换行，行数也会加 1"，与实现中 `IsLogicalLine` 才计数的语义一致。
- EE 扩展（EE_Anchor 的 CB 功能、EM 私家版描画扩展）在标准 Emuera 基础上给 `deleteLine` 增加了剪贴板同步与 dummyline 回填行为，不影响基本语义。
