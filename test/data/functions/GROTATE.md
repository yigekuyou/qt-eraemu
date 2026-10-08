# GROTATE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（EE 扩展 / G 系图像处理；**本仓库中已停用**）
- **签名**（源码注释块所载，当前版本不可用）：
  - `int GROTATE(<图像ID>, <旋转角度>)`
  - `int GROTATE(<图像ID>, <旋转角度>, <中心X>, <中心Y>)`
- **文档来源**：两套中文文档（`ecd/`、`zh/`）与 EE readme 均未收录本函数；EE readme 收录的是其后继替代品 `GDRAWGWITHROTATE`。语义据源码注释实现（`Runtime/Script/Statements/Function/Creator.Method.cs` 中被注释的类）与 `GraphicsImage.GRotate` 的现存代码。

## 语义

把指定 Graphics（图像 ID）**自身**的图像绕指定中心原地旋转，图像尺寸不变。角度单位为度、顺时针为正，负值为逆时针。第 3、4 参数为旋转中心（在图像自身坐标系中的位置），省略时取图像尺寸的一半（`宽/2, 高/2`）。旋转后的结果通过 `DrawImageUnscaled` 画回自身位图，因此是「旋转后重新绘制到自己身上」，多次调用会累积（每次都在上一次的结果上再转）。

**本函数在当前源码中不可用**：注册表项被注释停用（`Runtime/Script/Statements/Function/Creator.cs:315`），实现类 `GraphicsRotateMethod` 整块被注释（`Runtime/Script/Statements/Function/Creator.Method.cs:5698-5737`），连参数检查的覆写也在注释内。因此在 erb 中写 `GROTATE(...)` 会被当作未定义函数而报错。其被保留的替代方案是 `GDRAWGWITHROTATE`（见 `test/data/commands/GDRAWGWITHROTATE.md`），后者把源图像旋转后画到另一个目标图像上。

## 用法

### int GROTATE(ID, 角度{, 中心X, 中心Y})

以下示例按注释实现的语义书写，**在本仓库版本的 Emuera 中会因函数未注册而报错**，仅供理解语义与对照 `GDRAWGWITHROTATE`：

```erb
; 以下代码在本仓库版本中无法运行（GROTATE 未注册）
GCREATEFROMFILE 0, "pic.png"
GROTATE 0, 90                 ; 绕图像中心顺时针旋转 90 度
GROTATE 0, -45, 0, 0          ; 绕图像左上角逆时针旋转 45 度
```

可用的等价写法（把旋转结果画到另一个图像）：

```erb
GCREATEFROMFILE 0, "pic.png"
GCREATE 1, 100, 200
GDRAWGWITHROTATE 1, 0, 90     ; 把 0 号图像顺时针转 90 度后画到 1 号
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:315`（`//["GROTATE"] = new GraphicsRotateMethod(),` — 整行被注释，紧邻的 `:316` 是 `["GDRAWGWITHROTATE"]`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5698-5737`（`GraphicsRotateMethod`，整类被注释，源码作者在 `:5697` 注明「使われてない」＝未被使用）
- 旋转核心：`UI/Game/Image/GraphicsImage.cs:421`（`GRotate`，此方法**未被注释、仍然存在**，但已无调用者）

```text
GraphicsRotateMethod（Creator.Method.cs:5698 起，整段在注释中）:
构造：返回类型 = Int64；参数 = [Int64, Int64]{, Int64, Int64}；CanRestructure = false。
CheckArgumentType(name, arguments):
    参数个数 < 2 → 错误「引数が少なすぎます」
    参数个数 > 4 → 错误「引数が多すぎます」
    参数个数 既不是 2 也不是 4 → 错误「引数の数が正しくありません」
    返回 null（通过检查）
GetIntValue(exm, arguments):
    若 Config.TextDrawingMode == WINAPI: 抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(exm, arguments, 0)       ; 取第 1 参数指定的图像，负值/过大 → CodeEE
    若 !g.IsCreated: 返回 0                    ; 图像未创建
    angle = arguments[1] 的整数值
    若 arguments.Count == 2:                   ; 未给中心点
        g.GRotate(angle, g.Width / 2, g.Height / 2)     ; 默认以图像中心为轴
    否则:
        p = ReadPoint(Name, exm, arguments, 2)  ; 第 3、4 参数，越出 int 范围 → CodeEE
        g.GRotate(angle, p.X, p.Y)
    返回 1

GraphicsImage.GRotate(a, x, y)（GraphicsImage.cs:421）:
    若 g == null: 抛 NullReferenceException
    angle = (float)a
    g.TranslateTransform(-x, -y, MatrixOrder.Append)   ; 把轴心平移到原点
    g.RotateTransform(angle, MatrixOrder.Append)       ; 旋转（顺时针为正）
    g.TranslateTransform(x, y, MatrixOrder.Append)     ; 平移回去
    g.DrawImageUnscaled(Bitmap, 0, 0)                  ; 把原图按新变换重绘到自身
```

## 备注

- **本仓库未启用**：注册行与实现类都在注释中，唯一活着的部分是 `GraphicsImage.GRotate`（`UI/Game/Image/GraphicsImage.cs:421`），它已无任何调用者。同名条目在 `test/data/emuera_standard_funcs.txt` 中出现，只是因为该清单取自注册表的字面文本。
- 注释实现里的参数个数约束与 `GDRAWGWITHROTATE` 一致（2 或 4 个），但 `GDRAWGWITHROTATE` 用 `ArgTypeList`（`OmitStart = 3`）表达为「3~5 个参数」，两者形式不同。
- 注释版没有「源与目标尺寸不同」的错位问题（旋转的是自身、尺寸不变），这正是它被 `GDRAWGWITHROTATE` 取代的原因之一：`GDRAWGWITHROTATE` 在源/目标尺寸不同、中心点按源尺寸计算时会产生随角度变化的错位（EE readme 有专门说明）。
- 旋转角度、中心点的取值合法性由 GDI+ 处理，源码不额外校验角度范围（超过 360 度可正常使用，与 `GDRAWGWITHROTATE` 的文档说明一致）。
