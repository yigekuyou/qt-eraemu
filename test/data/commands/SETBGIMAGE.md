# SETBGIMAGE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:175`；两套中文文档均未收录，语义据源码）
- **签名**（从 `FORM_STR_ANY_ArgumentBuilder` 推定的完整形式）：`SETBGIMAGE <图片名>{, <深度>, <不透明度>}`
  - `<图片名>`：FORM 格式文本（必须求值为已在 `resources` 中注册的**静止**图片名）
  - `<深度>`：整数表达式，省略为 0
  - `<不透明度>`：0～255 的整数表达式，省略为 255（= 完全不透明）
- **文档来源**：无中文文档收录。ecd 的图像处理章节只有 `G`（Graphics）/`SPRITE`/`CBG`（ClientBackground）三族（`ecd/docs/translation/Command.md:2245~2250` 的章节导言 + `CBG*` 各小节），没有 `SETBGIMAGE`；`grep -rn "BGIMAGE" test/data/_extracted/` 只命中 CBG 相关的两句说明。依据源码：`Runtime/Script/Statements/FunctionIdentifier.cs:275`、`Runtime/Script/Statements/Instraction.Child.cs:1562`、`UI/Game/EmueraConsole.cs:679`

## 语义

给控制台窗口添加一张**背景图片**（贴在文字后面的底图）：图片按名字从已注册的精灵表（`resources/` 目录 CSV 里声明的资源）中取出，按 `<深度>` 决定叠放顺序，按 `<不透明度>` 做整体透明（alpha）混合，然后立刻重新烘焙（bake）整张背景。

- 同一张图片可以多次添加（每次都会往背景列表里追加一项）。
- `<深度>` 越大越靠**后**：`AddBackgroundImage` 用 `(v1.Key >= v2.Key) ? -1 : 1` 排序，列表按深度**降序**排列；`BakeBackground` 从列表头部往后依次绘制，因此后绘制（深度小）的图片盖在深度大的图片之上。
- 不透明度以 `值 / 255.0f` 换算成 `ColorMatrix.Matrix33`，写入 `ConsoleBackground`。
- 图片绘制按「长宽比保持」缩放：取宽、高缩放比中较小者，使图片完整放入窗口，水平居中（纵向贴顶），`BakeBackground` 第 726~738 行。
- **错误行为（静默）**：找不到图片名、图片是动画精灵（`SpriteAnime`）或 Graphics 图（`SpriteG`）时，`as SpriteF` 得到 null，指令什么都不做、**不报错**。
- **错误行为（异常）**：`<深度>`、`<不透明度>` 是按字符串解析后 `long.Parse` 的，写成非数字文本会抛 .NET `FormatException`（不是 CodeEE），属于未受控异常。
- 与 `CLEARBGIMAGE`（清空全部背景）和 `REMOVEBGIMAGE`（按图片名移除）配套；`RESETBGCOLOR` 只改背景色、不动背景图。

## 用法

### SETBGIMAGE <图片名>{, <深度>, <不透明度>}

- `<图片名>`：FORM 文本，通常与 `PRINT_IMG` 使用的资源名相同（`resources/` 下 CSV 声明的精灵名，大小写不敏感——`AppContents.GetSprite` 内部统一转大写）。
- `<深度>` / `<不透明度>`：数值**以 FORM 文本的一部分**参与解析，随后 `long.Parse`；因此必须写成裸数字（写 `abc` 会抛异常）。不透明度 0 = 完全透明，255 = 完全不透明。

```erb
; 名字为纯文本时可以直接写；若名字会与变量名混淆，请用 %...% 或 {…} 明确求值
SETBGIMAGE bg_room.png              ; 深度 0、不透明度 255
SETBGIMAGE bg_room.png, 10          ; 深度 10
SETBGIMAGE bg_room.png, 10, 128     ; 深度 10、约 50% 透明
SETBGIMAGE %"bg_" + TOSTR(DAY) + ".png"%
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:275`（`addFunction(FunctionCode.SETBGIMAGE, new SETBGIMAGE_Instruction())`，同组 `SETBGCOLOR`/`RESETBGCOLOR`/`CLEARBGIMAGE`/`REMOVEBGIMAGE` 见第 273~279 行）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:175`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1562`（`SETBGIMAGE_Instruction`；构造第 1564~1568 行，执行第 1570~1586 行）；最终落点 `UI/Game/EmueraConsole.cs:679`（`EmueraConsole.AddBackgroundImage`）
- 参数构造：`Runtime/Script/Statements/ArgumentBuilder.cs:559`（`FORM_STR_ANY_ArgumentBuilder`，1 个以上的 FORM 字符串，逗号分隔，第 559~600 行）；参数类型注释见 `Runtime/Script/Statements/FunctionArgType.cs:66`

```text
构造期 SETBGIMAGE_Instruction():
    ArgBuilder = GetArgumentBuilder(FunctionArgType.FORM_STR_ANY)
    flag = METHOD_SAFE | EXTENDED

执行期 DoInstruction（Instraction.Child.cs:1570~1586）:
    arg = (ExpressionArrayArgument)func.Argument
    bgName = arg.TermList[0].GetStrValue(exm)                     # 第 1 参数必填
    bgDepth = 0; opacity = 1.0f                                   # 默认深度 0、不透明
    若 TermList.Count >= 2: bgDepth = long.Parse(arg.TermList[1].GetStrValue(exm))
    若 TermList.Count >= 3: opacity = long.Parse(arg.TermList[2].GetStrValue(exm)) / 255.0f
    exm.Console.AddBackgroundImage(bgName, bgDepth, opacity)

EmueraConsole.AddBackgroundImage(name, depth, opacity)（EmueraConsole.cs:679~691）:
    spr = AppContents.GetSprite(name) as SpriteF
    若 spr == null: return                    # 名字不存在 / 动画精灵 / Graphics → 静默忽略
    bg = new ConsoleBackground(spr, opacity)  # 构造时 SetOpacity → colorMatrix.Matrix33 = opacity
    backgroundList.Add(KeyValuePair(depth, bg))
    backgroundList.Sort((v1, v2) => (v1.Key >= v2.Key) ? -1 : 1)  # 深度降序
    BakeBackground()

EmueraConsole.BakeBackground()（EmueraConsole.cs:718~739）:
    若 bakedBackground == null: return        # 尚未 ValidateBackground 时只排队，不重绘
    graph.Clear(透明)
    对 backgroundList 中每一项（深度大的在前）:
        scaleW = 画布宽 / 图片宽; scaleH = 画布高 / 图片高
        cropHorizontally = 图片高 * scaleW < 画布高
        newWidth/newHeight = 图片尺寸 × (cropHorizontally ? scaleH : scaleW)   # 取较小缩放比
        paddingX = (画布宽 - newWidth) / 2                                    # 水平居中
        attributes.SetColorMatrix(bg.GetColorMatrix())
        bg.GraphicsDraw(graph, Rectangle(paddingX, 0, newWidth, newHeight), attributes)
```

## 备注

- **无任何中文文档**（ecd/zh 都未收录），可确认为私有扩展：`SETBGIMAGE`/`CLEARBGIMAGE`/`REMOVEBGIMAGE` 三个名字在改造版（`emuera.em/Emuera/`）里只出现于 `Runtime/Script/Statements/BuiltInFunctionCode.cs`、`Runtime/Script/Statements/FunctionIdentifier.cs`、`Runtime/Script/Statements/Instraction.Child.cs` 三处，而且在**未改造的原版 Emuera 1.824 源码（仓库根 `Emuera/`，`Emuera/Properties/AssemblyInfo.cs:34` 为 `1.824.*`）中完全不存在**（`grep -rl "SETBGIMAGE" Emuera/` 零命中）→ 这族命令是 emuera.em（EM+EE / Emuera.NET 系）新增的私有扩展，不是原版 Emuera 命令。EmueraEE readme、changelog、私家改造版 readme 中也没有记载（Shift-JIS 转码后 grep 无结果）。
- 与 ecd 记载的 `CBG*`（ClientBackground）**不是同一套机制**：`CBG*` 是 Emuera.NET 客户端/描画层的背景指令（ecd 有完整小节），在本仓库 C# 侧**只有式中函数形态**——`CBGSETG`/`CBGSETSPRITE`/`CBGCLEAR` 等注册于 `Runtime/Script/Statements/Function/Creator.cs:195~203`，`Runtime/Script/Statements/BuiltInFunctionCode.cs` 与 `Runtime/Script/Process.ScriptProc.cs` 里没有任何 `CBG*` 命令枚举（`grep -n CBGSETSPRITE BuiltInFunctionCode.cs` 零命中）；而 `SETBGIMAGE` 操作的是 `EmueraConsole.backgroundList`，两者互不影响。
- 文档侧无法对照，但源码里有两条「文档若收录也未必会写」的行为：① 深度用 `>=` 比较的排序器不是稳定排序，同深度图片的相对顺序不保证；② `long.Parse` 失败抛 `FormatException`（未包成 CodeEE），运行期该异常不会走 CodeEE 分支，而是由 `Process.handleException` 的 `else` 分支按「予期せぬエラー」连堆栈一起打印（`Runtime/Script/Process.cs:494~502`）。
- `bgDepth` 声明为 `long`，`AddBackgroundImage` 的形参也是 `long`，但列表用 `KeyValuePair<long, ConsoleBackground>` 保存后再烘焙——深度只影响顺序，不影响透明度/大小。
