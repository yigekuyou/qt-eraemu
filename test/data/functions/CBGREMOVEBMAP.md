# CBGREMOVEBMAP

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGREMOVEBMAP()（无参数）
- **文档来源**：`ecd/Command.md`「### CBGREMOVEBMAP」（图像处理相关章节）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

解除由 `CBGSETBMAPG` 指令设置的按钮映射。按钮映射图像本身不显示，但会被 `CBGSETBUTTONSPRITE` 与 `INPUTMOUSEKEY` 用于把鼠标位置换算成按钮值；解除后这些按钮不再响应。

作为式中函数调用时返回 `1`（成功）。

## 用法

### int CBGREMOVEBMAP()
- 无参数。
- 返回值：恒为 `1`。
```erb
; 解除按钮映射（背景图像保留）
CBGREMOVEBMAP()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:201`（`["CBGREMOVEBMAP"] = new CBGRemoveBMapMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6615`（`CBGRemoveBMapMethod`）
- 控制台侧：`UI/Game/EmueraConsole.cs:194`（`CBG_ClearBMap()`）

```text
CBGRemoveBMapMethod:
构造：返回类型 = long；参数 = []；CanRestructure = false。
GetIntValue(exm, args):
    exm.Console.CBG_ClearBMap()
    返回 1

EmueraConsole.CBG_ClearBMap():
    cbgButtonMap ← null
    selectingCBGButtonInt ← -1       ; 当前选中按钮复位
    lastSelectingCBGButtonInt ← -1   ; 上次选中按钮复位
```

## 备注

- 与 `CBGCLEARBUTTON` 不同：本函数不删除任何按钮精灵条目，仅解除按钮映射。
- 源码中被注释掉的一行表明原版曾限制 WINAPI 绘制方式时报错，现版本不限制。
