# GSETFONT

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GSETFONT(`<ID>`, `<字体名>`, `<字号>`)
- **签名**：int GSETFONT(`<ID>`, `<字体名>`, `<字号>`, `<字体样式>`)（EE 扩展第 4 参数，源码支持、文档未记）
- **文档来源**：`ecd/Command.md`「### GSETFONT `<ID>`, `<字体名>`, `<字号>`」（图像处理相关章节，按指令形式记载）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

为指定 ID 的 Graphics 设置指定名称和字号的字体。设置的字体被记忆在该 Graphics 中，直到用 `GDISPOSE` 废弃该 Graphics 或再次设置为止。处理成功返回 1；Graphics 未创建/已废弃、或字体创建失败时返回 0。

文档（基于 1.824 版）称「截至 1.824 版，尚不存在使用所设置字体的指令或函数」；本仓库已跟进 EE 的 `GDRAWTEXT` 等扩展，所设置的字体可被 `GGETFONT`/`GGETFONTSIZE`/`GGETFONTSTYLE` 读取并供 `GDRAWTEXT` 绘制文字使用。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报运行时错误。

## 用法

### int GSETFONT(ID, 字体名, 字号)
- ID：Graphics 的 ID，0 以上整数。
- 字体名：字符串，字体族名称。若该名字与已加载的字体文件（PrivateFontCollection）中的字体族匹配，则优先使用之；否则按系统字体名创建。
- 字号：像素单位字号（GraphicsUnit.Pixel）。
- 返回值：成功 1；失败（Graphics 未创建/字体无效等）0。

### int GSETFONT(ID, 字体名, 字号, 字体样式)（EE 扩展）
- 字体样式：按位组合的整数——1 = 粗体（Bold）、2 = 斜体（Italic）、4 = 删除线（Strikeout）、8 = 下划线（Underline）；0 或省略为 Regular。
```erb
GCREATE 0, 200, 100
GSETFONT 0, "MS Gothic", 16
GSETCOLOR 0, 0xFF000000, 0, 0
GDRAWTEXT 0, "Hello", 0, 0   ; 用所设字体绘制文字（EE 扩展）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:189`（`["GSETFONT"] = new GraphicsSetFontMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5447`（`GraphicsSetFontMethod`）

```text
GraphicsSetFontMethod:
构造：返回类型 = long；参数 = [Int, String, Int, Int]，从第 3 个参数起可省略
     （OmitStart = 2，即第 4 参数可省）；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    fontname = args[1] 的字符串值
    fontsize = args[2] 的整数值
    fs = FontStyle.Regular
    若 args.Count > 3:                      ; 指定了第 4 参数
        style = args[3] 的整数值
        style & 1 → fs |= Bold
        style & 2 → fs |= Italic
        style & 4 → fs |= Strikeout
        style & 8 → fs |= Underline
    尝试创建字体:
        遍历 GlobalStatic.Pfc.Families（已加载字体文件的字体族）:
            若某字体族名 == fontname:
                styledFont = new Font(该字体族, fontsize, fs, Pixel)
                跳出（foundfont）
        否则 styledFont = new Font(fontname, fontsize, fs, Pixel)
        （Font 构造抛异常时返回 0）
    g.GSetFont(styledFont, fs)   ; 旧 Font 被 Dispose，新字体与样式保存
    返回 1
```

## 备注

- ecd/Command.md 按「指令」形式记载 G 系，但本仓库中 GSETFONT 是注册在 `Runtime/Script/Statements/Function/Creator.cs` 的式中函数（在表达式内调用、有返回值），无同名命令。
- 文档与源码差异 1：文档签名只有 3 参数，源码支持可选第 4 参数「字体样式」（EE_GDRAWTEXT 扩展的一部分），文档未记。
- 文档与源码差异 2：文档称「尚不存在使用所设置字体的指令或函数」（1.824 版状态），本仓库已实现 `GDRAWTEXT` 等使用该字体的扩展函数，该描述在本仓库已过时。
- 文档称设置失败/未创建时返回非 0 之外的结果，源码明确返回 0，语义一致。
