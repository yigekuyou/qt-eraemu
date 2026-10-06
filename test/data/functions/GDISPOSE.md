# GDISPOSE

- **类别**：式中函数（G 系图像处理指令，可作命令或函数使用）
- **签名**：int GDISPOSE(`<ID>`)
- **文档来源**：`ecd/Command.md`「### GDISPOSE `<ID>`」（图像处理相关章节）；`ecd/Expression.md` 未收录（签名列表不含 G 系）；zh 套件未收录

## 语义

废弃（释放）指定 ID 的 Graphics：销毁其位图、Graphics、画刷、画笔、字体等全部非托管资源，尺寸归零，之后该 ID 回到「未创建」状态，可被 `GCREATE` / `GCREATEFROMFILE` 重新使用。

废弃成功返回 `1`；指定 ID 的 Graphics 未创建（包括已废弃）时返回 `0`，不做任何事。G 系指令要求绘制方式为 `GRAPHICS` 或 `TEXTRENDERER`；`WINAPI` 下报错。

## 用法

### int GDISPOSE(ID)
- ID：要废弃的 Graphics 的 ID，0 以上整数（负数或超 int 上限报错）。
- 返回值：成功废弃 `1`；本来就不存在 `0`。
```erb
IF GCREATED(0) != 0
	GDISPOSE 0
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:180`（`["GDISPOSE"] = new GraphicsDisposeMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:6038`（`GraphicsDisposeMethod`）；释放核心 `UI/Game/Image/GraphicsImage.cs:562`（`GDispose`）

```text
GraphicsDisposeMethod:
构造：返回类型 = long；参数 = [int]；CanRestructure = false。

GetIntValue(exm, args):
    若 Config.TextDrawingMode == WINAPI: 抛 CodeEE（仅 GDI+ 绘制方式可用）
    g = ReadGraphics(Name, exm, args, 0)
        ; args[0] → ID；负数或 > int.MaxValue → CodeEE
    若 !g.IsCreated: 返回 0
    g.GDispose()
        ; size = (0, 0)；drawImgList = null
        ; 依次 Dispose：g、RealBitmap、brush、pen、font（各判空）
        ; 全部引用置 null → 之后 IsCreated == false
    返回 1
```

## 备注

- 文档「废弃成功时返回非 0；未创建（包括已废弃）时返回 0」与源码完全一致。
- `GCreate` 内部第一步也调用 `GDispose`（防重复创建）；但 `GCREATE` 在已创建时直接返回 0，不会走到那里，二者不冲突。
- `GClear`（矩形扩展）等操作会把 `drawImgList` 置空，废弃资源须显式调用本函数，脚本不调用不会自动回收（仅 tempLoadedGraphicsImages 列表持有引用）。
