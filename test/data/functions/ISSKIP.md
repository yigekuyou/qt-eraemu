# ISSKIP

- **类别**：式中函数
- **签名**：int ISSKIP()
- **文档来源**：`ecd/Command.md`「### ISSKIP」（及 SKIPDISP 小节的关联说明）；`ecd/Expression.md` 签名列表

## 语义

当 `SKIPDISP` 的开关为 0 以外（即正在忽略 `PRINT` 等画面输出）时返回 `1`，否则返回 `0`。

无参数、无副作用，只读取当前显示忽略开关的状态。典型用法：在 `SKIPDISP 1` 状态下被调用的口上等代码中，通过本函数获知当前是否处于不显示状态，从而使"显示/不显示"两种情况下行为一致。与 `NOSKIP`～`ENDNOSKIP` 区间配合时，区间内的输出不受开关影响，但本函数返回值仍反映 `SKIPDISP` 开关本身。

## 用法

### int ISSKIP()
- 无参数。
- 返回值：显示忽略开关打开时 `1`，否则 `0`。
```erb
SKIPDISP 1
IF ISSKIP()
  PRINTL 这行不会显示
ENDIF
SKIPDISP 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:40`（`["ISSKIP"] = new IsSkipMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2530`（`IsSkipMethod`）

```text
构造：返回类型 = long；参数 = []（无参）；CanRestructure = false（依赖运行期状态，不可折叠）。

GetIntValue(exm, args):
    返回 exm.Process.SkipPrint ? 1 : 0
    ; SkipPrint 即 SKIPDISP 指令设置的"忽略画面输出"开关
```

## 备注

- ecd/Command.md 还提示（在 SKIPDISP 小节）：开关打开期间执行 `INPUT`/`INPUTS` 会因用户无从得知该做什么而产生警告与错误；宏处理时的跳过与 `MOUSESKIP` 有关，与本函数无关。
- 相关函数 `MOUSESKIP()`（右键 WAIT 跳过状态）与同名指令群 `SKIPDISP`/`NOSKIP`/`ENDNOSKIP` 见 ecd/Command.md 相应小节。
- zh 套件未收录本函数。
