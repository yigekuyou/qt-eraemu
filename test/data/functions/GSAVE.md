# GSAVE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GSAVE(`<ID>`, `<文件编号>`)
- **文档来源**：`ecd/Command.md`「### GSAVE `<ID>`, `<文件编号>`」（图像处理相关章节，按指令形式记载）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

把指定 ID 的 Graphics 的图像以附加 `fileNo` 编号的文件名、按 png 格式输出保存到存档目录。处理成功返回 1；Graphics 未创建/已废弃、文件编号越界、保存目录创建失败或保存失败时返回 0。

文件名由源码固定为 `imgXXXX.png`（`fileNo` 以 4 位零填充），保存在 `Config.SavDir`（存档目录，通常为 `save/`）下。保存的图像可用 `GLOAD` 读回。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报运行时错误。

## 用法

### int GSAVE(ID, 文件编号)
- ID：Graphics 的 ID，0 以上整数。
- 文件编号：0 以上、int.MaxValue 以下的整数；负数或越界时返回 0。
- 返回值：成功 1；失败 0。
```erb
GCREATE 0, 64, 64
GCLEAR 0, 0xFF00FF00
IF GSAVE(0, 5) != 0
	PRINTL 已保存到 save/img0005.png
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:205`（`["GSAVE"] = new GraphicsSaveMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7105`（`GraphicsSaveMethod`）

```text
辅助函数:
    GetSaveDataPathGraphics(index) = "<Config.SavDir>/img{index:0000}.png"
    ; 例：fileNo = 5 → save/img0005.png

GraphicsSaveMethod:
构造：返回类型 = long；参数 = [long, long]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    i64 = args[1] 的整数值
    若 i64 < 0 || i64 > int.MaxValue: 返回 0
    filepath = GetSaveDataPathGraphics((int)i64)
    try:
        Config.CreateSavDir()      ; 存档目录不存在则创建
        g.Bitmap.Save(filepath)    ; 以位图原始格式（png）保存
    catch:
        返回 0
    返回 1
```

## 备注

- ecd/Command.md 按「指令」形式记载 G 系，但本仓库中 GSAVE 是注册在 `Runtime/Script/Statements/Function/Creator.cs` 的式中函数（在表达式内调用、有返回值），无同名命令。
- 文档只说「以附加 fileNo 编号的文件名、按 png 格式输出保存」，未写明目录与文件名格式；源码明确为存档目录下 `img0000` 式 4 位零填充文件名（`GetSaveDataPathGraphics`）。
- 文档只说「成功时返回非 0」，源码明确成功返回 1、失败返回 0，语义一致。
- 与 `GLOAD` 成对使用：GLOAD 从同一位置（`save/imgXXXX.png`）读回。
