# ISACTIVE

- **类别**：式中函数
- **签名**：int ISACTIVE()
- **文档来源**：`ecd/Command.md`「### ISACTIVE」；`ecd/ERB_Commands.md` 分类表（AWAIT 相关）

## 语义

返回 Emuera 窗口的状态：窗口活动（拥有焦点）时返回 `1`，非活动时返回 `0`。

无参数、无副作用。主要用于与 `AWAIT`/`INPUT` 相关的处理中，根据窗口是否处于活动状态决定行为（例如窗口失焦时暂停或跳过某些处理）。判定标准是"Emuera 窗口存在且已创建，且当前系统中存在活动窗体（`Form.ActiveForm` 非空）"。

## 用法

### int ISACTIVE()
- 无参数。
- 返回值：Emuera 窗口活动时 `1`，非活动时 `0`。
```erb
IF ISACTIVE() == 0
  PRINTL 窗口当前处于非活动状态
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:168`（`["ISACTIVE"] = new IsActiveMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6862`（`IsActiveMethod`）；判定属性在 `UI/Game/EmueraConsole.cs:297`（`EmueraConsole.IsActive`）

```text
构造：返回类型 = long；参数 = []（无参）；CanRestructure = false。

GetIntValue(exm, args):
    返回 exm.Console.IsActive ? 1 : 0

Console.IsActive:
    返回 !(window == null || !window.Created || Form.ActiveForm == null)
    ; 即窗口存在、已创建、且当前有活动窗体（即 Emuera 自己获得焦点）时为真
```

## 备注

- ecd/Command.md 仅一句话描述，未说明判定细节；"窗口存在且 `Form.ActiveForm` 非空才算活动"是源码补充信息。
- zh 套件未收录本函数。
