# GDRAWGWITHROTATE

- **类别**：EE 扩展命令（本仓库中实现为式中函数）
- **签名**：
  - `GDRAWGWITHROTATE <复制目标ID>, <复制源ID>, <旋转角度>`
  - `GDRAWGWITHROTATE <复制目标ID>, <复制源ID>, <旋转角度>, <中心X>, <中心Y>`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`「・GDRAWGWITHROTATE コピー先gID, コピー元gID, 回転させる角度(時計回り), X座標, Y座標」；`EmueraEE_changelog.txt`（EEv11 追加）。ecd 套件与 zh 套件均未收录本命令。

## 语义

把复制源 Graphics 的图像旋转指定角度后贴到复制目标 Graphics 上。角度为顺时针度数，负值为逆时针，超过 360 也没问题。第 4、5 参数指定旋转中心点（在目标坐标系中的位置），省略时默认为源图像尺寸的一半（`src宽/2, src高/2`）。

中心点按复制源尺寸计算，若源与目标尺寸不同会产生错位，且错位程度随角度不同（90°、270° 时最大）；此时应手动指定合适的中心点（readme 附有 200x100 图旋转 90° 移入 100x200 图时须以重叠区域中心为轴的图例说明）。绘制方式为 `WINAPI` 时不能使用；源或目标未创建时返回 0。

## 用法

### `GDRAWGWITHROTATE <目标ID>, <源ID>, <角度>{, <中心X>, <中心Y>}`
```erb
GCREATE 0, 200, 100      ;源
GCREATE 1, 100, 200      ;目标
GFILLRECTANGLE 0, 0, 0, 200, 100
;以重叠部分中心 (50,50) 为轴旋转 90° 贴入
GDRAWGWITHROTATE 1, 0, 90, 50, 50
;270° 时指定整体中心 (100,100) 即可对齐
;GDRAWGWITHROTATE 1, 0, 270, 100, 100
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:316`（`["GDRAWGWITHROTATE"] = new GraphicsDrawGWithRotateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5738`（`GraphicsDrawGWithRotateMethod`；`GetIntValue` 在 5771 行）；底层 `UI/Game/Image/GraphicsImage.cs:436`（`GDrawGWithRotate`）

```text
构造: ReturnType = 整数
      argumentTypeArrayEx = [ [int, int, int, int, int], OmitStart = 3 ]   # 3 或 5 个参数
      CanRestructure = false

GetIntValue(exm, arguments):
    if Config.TextDrawingMode == WINAPI:
        throw CodeEE(「该命令只能在 GDI+ 绘制方式下使用」)
    dest = ReadGraphics(Name, exm, arguments, 0)
    if !dest.IsCreated: return 0
    src = ReadGraphics(Name, exm, arguments, 1)
    if !src.IsCreated: return 0
    angle = arguments[2].GetIntValue(exm)
    if arguments.Count == 3:
        # 坐标省略时以源尺寸的一半为中心
        dest.GDrawGWithRotate(src, angle, src.Width / 2, src.Height / 2)
    else:
        p = ReadPoint(Name, exm, arguments, 3)     # 解析第4、5参数为 (x, y)
        dest.GDrawGWithRotate(src, angle, p.X, p.Y)
    return 1

# GraphicsImage.GDrawGWithRotate（GDI+ 变换顺序自后向前叠加）
GDrawGWithRotate(srcGra, a, x, y):
    if g == null 或 srcGra == null: throw NullReferenceException
    g.TranslateTransform(-x, -y, Append)     # 把旋转中心平移到原点
    g.RotateTransform((float)a, Append)      # 绕原点旋转 angle 度（正=顺时针）
    g.TranslateTransform(x, y, Append)       # 平移回 (x, y)
    src = srcGra.GetBitmap()
    g.DrawImage(src, 0, 0)                   # 在原点绘制源位图（即绕 (x,y) 旋转后的位置）
```

## 备注

- ecd 与 zh 两套文档均未收录；语义以 EmueraEE_readme.txt 为准，实现以本仓库 C# 源码为准，两者一致。
- readme 对中心点错位的详细图示（200x100 转 90°/270° 时需改中心点）对应实现细节：旋转中心参数是「目标坐标系中的点」，而默认值取自源尺寸一半，源/目标尺寸不一致时贴图位置会偏移。
- 源码中同类函数 `GraphicsRotateMethod`（`GROTATE`，Creator.cs:315）被注释停用，`GDRAWGWITHROTATE` 是其替代方案。
