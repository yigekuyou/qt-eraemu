# GLOAD

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：式中函数（G 系图像处理函数）
- **签名**：int GLOAD(`<ID>`, `<文件编号>`)
- **文档来源**：`ecd/Command.md`「### GLOAD `<ID>`, `<文件编号>`」（图像处理相关章节，按指令形式记载）；`ecd/Expression.md` 未收录；zh 套件未收录

## 语义

打开附加 `fileNo` 编号的文件名的图像，创建（初始化）指定 ID 的 Graphics。动作与 `GCREATEFROMFILE` 基本相同，区别在于不是从 `resources` 文件夹中的图像创建，而是从 `GSAVE` 保存的图像（存档目录下 `imgXXXX.png`）创建。

指定 ID 的 Graphics 已创建时会创建失败，该函数什么都不做并返回 0。其他失败情况（文件不存在、图像尺寸超过上限、加载失败）也返回 0。处理成功返回 1。

G 系函数要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报运行时错误。

## 用法

### int GLOAD(ID, 文件编号)
- ID：Graphics 的 ID，0 以上整数。
- 文件编号：0 以上、int.MaxValue 以下的整数；对应 `GSAVE` 保存的 `imgXXXX.png`。
- 返回值：成功 1；失败（含目标 Graphics 已创建）0。
```erb
; 读回 GSAVE(0, 5) 保存的图像
IF GLOAD(1, 5) != 0
	PRINTL 已加载到 Graphics 1
ELSE
	PRINTL 加载失败
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:206`（`["GLOAD"] = new GraphicsLoadMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:7142`（`GraphicsLoadMethod`）

```text
辅助函数（与 GSAVE 共用）:
    GetSaveDataPathGraphics(index) = "<Config.SavDir>/img{index:0000}.png"

GraphicsLoadMethod:
构造：返回类型 = long；参数 = [long, long]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI:
        抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 g.IsCreated: 返回 0          ; 与 GSAVE 相反：已创建则直接失败
    i64 = args[1] 的整数值
    若 i64 < 0 || i64 > int.MaxValue: 返回 0
    filepath = GetSaveDataPathGraphics((int)i64)
    bmp = null
    try:
        若 !File.Exists(filepath): 返回 0
        bmp = Utils.LoadImage(filepath)   ; EM 私家版：支持 webp 的加载
        若 bmp == null: 返回 0
        若 bmp.Width > AbstractImage.MAX_IMAGESIZE
           || bmp.Height > AbstractImage.MAX_IMAGESIZE: 返回 0
        g.GCreateFromF(bmp, Config.TextDrawingMode == WINAPI)
            ; 用该位图初始化 Graphics（从文件创建）
    catch e:
        若 e 是 CodeEE 则原样重抛；其他异常吞掉（返回值按后续判断）
    finally:
        bmp != null 时 Dispose(bmp)
    若 !g.IsCreated: 返回 0
    返回 1
```

## 备注

- ecd/Command.md 按「指令」形式记载 G 系，但本仓库中 GLOAD 是注册在 `Runtime/Script/Statements/Function/Creator.cs` 的式中函数（在表达式内调用、有返回值），无同名命令。
- 文档说「从 GSAVE 指令保存的图像创建」；源码明确路径为存档目录下 `imgXXXX.png`（与 GSAVE 共用 `GetSaveDataPathGraphics`）。
- 本仓库在加载处使用 `Utils.LoadImage`（EM 私家版 webp 支持的痕迹），与上游 `new Bitmap(filepath)` 略有差异，已由 try/finally 确保位图释放。
- 文档「已创建时什么都不做并返回 0」与源码 `g.IsCreated → 0` 一致。
