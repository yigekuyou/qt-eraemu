# CBGCLEAR

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGCLEAR()（无参数）
- **文档来源**：`ecd/Command.md`「### CBGCLEAR」（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 CBG 系）；zh 套件未收录

## 语义

解除由 `CBG` 系各指令（`CBGSETG`、`CBGSETSPRITE`、`CBGSETBUTTONSPRITE`）设置的全部客户端区域背景图像，同时解除按钮映射（等同执行 `CBGREMOVEBMAP`），并重建内部列表的哑元条目（zdepth = 0）。

作为式中函数调用时返回 `1`（成功）。CBG 系指令作为函数调用时结果不赋给 `RESULT`，而是作为返回值。

注：ecd 文档中 CBG 系各条目以「指令」形式记载；源码将其注册为式中函数（FunctionMethod），因此命令形态与函数形态（`CBGCLEAR()`）均可使用。

## 用法

### int CBGCLEAR()
- 无参数。
- 返回值：恒为 `1`。
```erb
; 清空全部客户端背景图像与按钮映射
CBGCLEAR()
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:197`（`["CBGCLEAR"] = new CBGClearMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6553`（`CBGClearMethod`）
- 控制台侧：`UI/Game/EmueraConsole.cs:146`（`CBG_Clear()`）

```text
CBGClearMethod:
构造：返回类型 = long；参数 = []；CanRestructure = false。
GetIntValue(exm, args):
    exm.Console.CBG_Clear()
    返回 1

EmueraConsole.CBG_Clear():
    对 cbgList 中每个 ClientBackGroundImage:
        若 Img 非空且 Img.Name 为空串（一次性无名图像）: Img.Dispose()
    cbgList.Clear()
    CBG_ClearBMap()                 ; 按钮映射置 null，选择状态复位为 -1
    cbgList.Add(new ClientBackGroundImage(0))   ; 重建 zdepth=0 的哑元
```

## 备注

- 源码中被注释掉的一行表明原版曾限制绘制方式为 WINAPI 时报错，现版本 CBGCLEAR 在任何绘制方式下都可执行（CBGSETG/CBGSETBMAPG/CBGSETBUTTONSPRITE 仍保留该限制）。
- 哑元条目（zdepth=0）不会被 `CBGREMOVERANGE` 删除，也不会绘制。
