# GGETFONTSIZE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（式中函数）
- **签名**：
  - `GGETFONTSIZE <gID>`
- **文档来源**：EM+EE 在线文档「GGETFONTSIZE」；`EmueraEE_readme.txt`「・GGETFONTSIZE gID」条目；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

返回指定的 Graphics ID（gID）通过 `GSETFONT` 设置的字体大小（整型）。若该 gID 尚未生成图像，返回 0。仅在文本绘制方式为 GDI+（非 WINAPI）时可用，否则报错。

EE v4 加入（changelog：`関数追加：GGETFONT, GGETFONTSIZE`）。相关命令：`GSETFONT`、`GGETFONT`、`GGETFONTSTYLE`。

## 用法

### `GGETFONTSIZE <gID>`
- `<gID>`：数值表达式，目标 Graphics ID。
- 返回：该 gID 用 GSETFONT 指定的字号。
```erb
@SYSTEM_TITLE
GCREATE 0, 100, 100
GSETFONT 0, "Arial", 100
GCREATE 1, 100, 100
GSETFONT 1, "ＭＳ ゴシック", 200
PRINTVL GGETFONTSIZE(0)
PRINTVL GGETFONTSIZE(1)
WAIT
;输出：100 ／ 200
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:319`（`["GGETFONTSIZE"] = new GraphicsStateMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5305`（`GraphicsStateMethod`，`case "GGETFONTSIZE"` 位于 `#region EE_GDRAWTEXTに付随する要素`）；gID 读取辅助 `ReadGraphics` 在同文件 `:5159`

```text
class GraphicsStateMethod : FunctionMethod     # GCREATED/GWIDTH/GHEIGHT/GGETFONTSIZE/GGETFONTSTYLE/GGETPEN/GGETPENWIDTH/GGETBRUSH 共用
    构造: ReturnType = long; argumentTypeArray = [long]; CanRestructure = false
    GetIntValue(exm, arguments):
        若 Config.TextDrawingMode == WINAPI: 抛出 CodeEE（仅 GDI+ 可用）
        g = ReadGraphics(参数0)      # gID<0 或超过 int.MaxValue 抛 CodeEE
        若 !g.IsCreated: 返回 0
        switch Name:
            ...
            case "GGETFONTSIZE": 返回 g.Fontsize
            ...
        其他: 抛 ExeEE（异常分支）
```

## 备注

- 文档标注「命令/行内函数两种写法均有效」；本仓库仅以式中函数形式实现。
- 无文档与源码冲突。
