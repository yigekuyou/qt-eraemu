# CBGCLEARBUTTON

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGCLEARBUTTON()（无参数）
- **文档来源**：`ecd/Command.md`「### CBGCLEARBUTTON」（图像处理相关章节）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

解除由 `CBGSETBUTTONSPRITE` 指令设置的全部可选择按钮（内部列表中 `isButton == true` 的条目），同时解除由 `CBGSETBMAPG` 设置的按钮映射。

作为式中函数调用时返回 `1`（成功）。

## 用法

### int CBGCLEARBUTTON()
- 无参数。
- 返回值：恒为 `1`。
```erb
; 删除全部 CBG 按钮及其按钮映射
CBGCLEARBUTTON()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:199`（`["CBGCLEARBUTTON"] = new CBGClearButtonMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6596`（`CBGClearButtonMethod`）
- 控制台侧：`UI/Game/EmueraConsole.cs:177`（`CBG_ClearButton()`）

```text
CBGClearButtonMethod:
构造：返回类型 = long；参数 = []；CanRestructure = false。
GetIntValue(exm, args):
    exm.Console.CBG_ClearButton()
    返回 1

EmueraConsole.CBG_ClearButton():
    对 i = 0 .. cbgList.Count-1:
        cimg ← cbgList[i]
        若 !cimg.isButton: continue        ; 非按钮条目跳过
        若 cimg.Img 非空且 Img.Name 为空串: Img.Dispose()
        cbgList.RemoveAt(i); i--           ; 删除并回退下标
    CBG_ClearBMap()                        ; 按钮映射置 null，选择状态复位
```

## 备注

- 与 `CBGCLEAR` 不同：本函数保留 `CBGSETG`/`CBGSETSPRITE` 设置的非按钮背景图像，只删按钮条目。
- 源码中被注释掉的一行表明原版曾限制 WINAPI 绘制方式时报错，现版本不限制。
