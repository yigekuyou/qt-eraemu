# GGETPENWIDTH

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（式中函数）
- **签名**：
  - `GGETPENWIDTH <gID>`
- **文档来源**：EM+EE 在线文档「GGETPENWIDTH」；`EmueraEE_changelog.txt` v12+「GGETPEN,GGETPENWIDTH,GGETBRUSH追加」；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

返回指定的 Graphics ID（gID）代表的图像中由 `GSETPEN` 指定的画笔宽度（整型）。若该 gID 尚未生成图像，返回 0。仅在文本绘制方式为 GDI+（非 WINAPI）时可用，否则报错。

## 用法

### `GGETPENWIDTH <gID>`
- `<gID>`：数值表达式，目标 Graphics ID。
- 返回：该 gID 画笔宽度。
```erb
@SYSTEM_TITLE
GCREATE 0, 100, 100
GSETPEN 0, 0xFF00FF00, 5
PRINTFORMW Color:{GGETPEN(0)}(%CONVERT(GGETPEN(0), 16)%) Width:{GGETPENWIDTH(0)}
;输出：Color:4278255360(ff00ff00) Width:5
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:324`（`["GGETPENWIDTH"] = new GraphicsStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5305`（`GraphicsStateMethod`，`case "GGETPENWIDTH"` 位于 `#region EE_GDRAWTEXTに付随する要素`）

```text
class GraphicsStateMethod : FunctionMethod
    构造: ReturnType = long; argumentTypeArray = [long]; CanRestructure = false
    GetIntValue(exm, arguments):
        若 Config.TextDrawingMode == WINAPI: 抛出 CodeEE（仅 GDI+ 可用）
        g = ReadGraphics(参数0)
        若 !g.IsCreated: 返回 0
        switch Name:
            ...
            case "GGETPENWIDTH": 返回 (long)g.Pen.Width
            ...
        其他: 抛 ExeEE
```

## 备注

- 文档标注「命令/行内函数两种写法均有效」；本仓库仅以式中函数形式实现。
- 无文档与源码冲突。
