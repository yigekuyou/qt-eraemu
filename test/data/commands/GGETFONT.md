# GGETFONT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（式中函数）
- **签名**：
  - `GGETFONT <gID>`
- **文档来源**：EM+EE 在线文档「GGETFONT」；`EmueraEE_readme.txt`「・GGETFONT gID」条目；`ecd/Command.md` 未收录；zh 套件未收录。

## 语义

返回指定的 Graphics ID（gID）通过 `GSETFONT` 设置的字体名称（字符串型）。若该 gID 尚未生成图像（`GCREATED` 为假），返回空字符串。仅在文本绘制方式为 GDI+（非 WINAPI）时可用，否则报错。

EE v4 加入（changelog：`関数追加：GGETFONT, GGETFONTSIZE`）。相关命令：`GSETFONT`、`GGETFONTSIZE`、`GGETFONTSTYLE`。

## 用法

### `GGETFONT <gID>`
- `<gID>`：数值表达式，目标 Graphics ID。
- 返回：该 gID 用 GSETFONT 指定的字体名。
```erb
@SYSTEM_TITLE
GCREATE 0, 100, 100
GSETFONT 0, "Arial", 100
GCREATE 1, 100, 100
GSETFONT 1, "ＭＳ ゴシック", 100
PRINTSL GGETFONT(0)
PRINTSL GGETFONT(1)
WAIT
;输出：Arial ／ ＭＳ ゴシック
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:318`（`["GGETFONT"] = new GraphicsStateStrMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5347`（`GraphicsStateStrMethod`，`#region EE_GGETFONT`）；gID 读取辅助 `ReadGraphics` 在同文件 `:5159`

```text
class GraphicsStateStrMethod : FunctionMethod
    构造: ReturnType = string; argumentTypeArray = [long]; CanRestructure = false
    GetStrValue(exm, arguments):
        若 Config.TextDrawingMode == WINAPI:
            抛出 CodeEE（"仅 GDI+ 绘制方式可用"）
        g = ReadGraphics(参数0)      # gID<0 或超过 int.MaxValue 抛 CodeEE
        若 !g.IsCreated: 返回 ""     # 未生成图像返回空串
        switch Name:
            case "GGETFONT": 返回 g.Fontname
        其他: 抛 ExeEE（异常分支）
```

## 备注

- 注意 EE readme 中还有一条同名 `・GGETFONT gID` 条目（第 89 行附近），描述为「返回 GSETFONT 指定的字体样式数值」，那实际是 `GGETFONTSTYLE` 的说明（changelog 中 GSETFONT 第 4 参数字体样式功能追加时的补充说明）；readme 排版将两条 GGETFONT 混排，文档与命令名存在此出入，语义以在线文档为准。
- 文档标注「命令/行内函数两种写法均有效」；本仓库仅以式中函数形式实现。
