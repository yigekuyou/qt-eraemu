# GCREATEFROMFILE

- **类别**：式中函数（G 系图像处理指令，可作命令或函数使用）
- **签名**：int GCREATEFROMFILE(`<ID>`, `<文件路径>`)；EE 扩展另支持 int GCREATEFROMFILE(`<ID>`, `<文件路径>`, `<相对路径标志>`)
- **文档来源**：`ecd/Command.md`「### GCREATEFROMFILE `<ID>`, `<文件路径>`」（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

读取图像文件并以它的尺寸创建指定 ID 的 Graphics。与在 `resources` 文件夹的 CSV 中声明资源不同，图像文件不会被锁定，读入后即可删除/改名。

返回值：创建成功返回 `1`（非 0）。指定 ID 的 Graphics 已创建时什么都不做并返回 `0`；文件不存在、无法识别为图像、文件尺寸超过 8192×8192 等失败时也返回 `0`（不报错）。

文件路径按相对路径解释时基于 `resources` 文件夹（`Program.ContentDir`）；EE 私家版扩展的第 3 参数非 0 时改为相对当前目录的原样路径。G 系指令要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报错。

## 用法

### int GCREATEFROMFILE(ID, filename)
- ID：Graphics 的 ID，0 以上整数（负数报错）。
- filename：图像文件路径。非绝对路径时默认接在 `resources/`（ContentDir）之后。
- 返回值：成功 `1`；失败（已存在/无文件/非图像/过大）`0`。
```erb
IF GCREATEFROMFILE(0, "title.png") == 0
	PRINTL 图像读取失败
ENDIF
```
### int GCREATEFROMFILE(ID, filename, isRelative)（EE 扩展，文档未收录）
- isRelative：非 0 时非绝对路径的 filename 不拼接 ContentDir，按脚本当前工作目录的原样相对路径处理。
```erb
ret = GCREATEFROMFILE(1, "img/logo.png", 1)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:179`（`["GCREATEFROMFILE"] = new GraphicsCreateFromFileMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:5975`（`GraphicsCreateFromFileMethod`）；读入核心 `UI/Game/Image/GraphicsImage.cs:96`（`GCreateFromF`，internal）

```text
GraphicsCreateFromFileMethod:
构造：返回类型 = long；
    参数表 [int, str, int]，OmitStart = 2（第 3 参可省略）；
    CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI: 抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)     ; ID 检查同 GCREATE
    若 g.IsCreated: 返回 0                    ; 已创建，什么都不做

    filename = args[1].GetStrValue(exm)
    isRelative = (args.Count > 2) ? (args[2] != 0) : false

    try:
        filepath = filename
        若 filepath 不是绝对路径:
            filepath = isRelative ? filename : Program.ContentDir + filename
        若 !File.Exists(filepath): 返回 0          ; 文件不存在
        bmp = Utils.LoadImage(filepath)            ; EM_私家版_webp：经工具类读图，支持 webp
        若 bmp == null: 返回 0                     ; 无法识别为图像
        若 bmp.Width > 8192 或 bmp.Height > 8192: 返回 0   ; 文件过大
        g.GCreateFromF(bmp, false)
            ; GDispose() → 新建与 bmp 同尺寸的 32bppArgb 位图
            ; g.DrawImage(bmp, 0, 0, w, h) 把图像整体画入
    catch (e):
        若 e 是 CodeEE: 重新抛出（其余异常吞掉）
    finally:
        bmp.Dispose()
    若 !g.IsCreated: 返回 0                        ; 因其他原因创建失败
    返回 1
```

## 备注

- 文档「文件过大等导致失败时也返回 0」与源码一致（上限 8192，同 `MAX_IMAGESIZE`）。
- 源码第 3 参数（isRelative）与 webp 支持（`Utils.LoadImage`）是本仓库 EE 私家版扩展，文档未收录。
- 文档说「以相对路径指定 resources 文件夹内的图像文件」：源码默认拼接 `Program.ContentDir`（resources 目录）；但传入绝对路径时按原样使用，文档未明说。
- 与资源 CSV 声明的区别（不锁定文件句柄）文档有说明，实现上体现为 `bmp.Dispose()` 在 finally 中释放。
