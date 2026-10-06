# CBGREMOVERANGE

- **类别**：式中函数（CBG 系图像处理指令，可作命令或函数使用）
- **签名**：int CBGREMOVERANGE(int zmin, int zmax)
- **文档来源**：`ecd/Command.md`「### CBGREMOVERANGE `<z最小值>`, `<z最大值>`」（图像处理相关章节）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

在由 `CBGSETG`、`CBGSETSPRITE`、`CBGSETBUTTONSPRITE` 指令设置的图像中，解除 Z 深度在 `zmin` 以上 `zmax` 以下的图像（含两端）。

- Z 深度为 0 的条目是内部哑元，不会被删除。
- `zmin > zmax` 时什么都不做（源码行为，直接返回）。

作为式中函数调用时返回 `1`（即使什么都没删也返回 1）。

## 用法

### int CBGREMOVERANGE(int zmin, int zmax)
- `zmin`：要解除的 Z 深度范围下界。
- `zmax`：要解除的 Z 深度范围上界。
- 返回值：恒为 `1`。
```erb
; 解除 Z 深度 10～20 的背景图像
CBGREMOVERANGE(10, 20)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:200`（`["CBGREMOVERANGE"] = new CBGRemoveRangeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6573`（`CBGRemoveRangeMethod`）
- 控制台侧：`UI/Game/EmueraConsole.cs:159`（`CBG_ClearRange(int zmin, int zmax)`）

```text
CBGRemoveRangeMethod:
构造：返回类型 = long；参数 = [long, long]；CanRestructure = false。
GetIntValue(exm, args):
    x64 ← args[0].GetIntValue(exm)
    y64 ← args[1].GetIntValue(exm)
    unchecked: exm.Console.CBG_ClearRange((int)x64, (int)y64)  ; long 截断为 int
    返回 1

EmueraConsole.CBG_ClearRange(zmin, zmax):
    若 zmin > zmax: 返回（不做任何事）
    对 i = 0 .. cbgList.Count-1:
        cimg ← cbgList[i]
        若 cimg.zdepth < zmin 或 cimg.zdepth > zmax 或 cimg.zdepth == 0:
            continue                      ; 范围外及哑元（zdepth=0）跳过
        若 cimg.Img 非空且 Img.Name 为空串: Img.Dispose()
        cbgList.RemoveAt(i); i--
```

## 备注

- 文档未提及 `zmin > zmax` 时静默忽略、参数按 long→int 截断（unchecked）等源码行为。
- 源码中被注释掉的一行表明原版曾限制 WINAPI 绘制方式时报错，现版本不限制。
