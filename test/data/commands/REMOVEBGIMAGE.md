# REMOVEBGIMAGE

- **类别**：EE 扩展命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:179`；两套中文文档均未收录，语义据源码）
- **签名**：`REMOVEBGIMAGE <图片名>`
- **文档来源**：无中文文档收录。ecd 图像章节只有 `CBGREMOVERANGE`（`ecd/docs/translation/Command.md:2590`「在 CBGSETG/CBGSETSPRITE/CBGSETBUTTONSPRITE 设置的图像中，解除 Z 深度在范围内的图像」）等 ClientBackground 指令，与本节无关；`grep -rn "REMOVEBGIMAGE\|BGIMAGE" test/data/_extracted/` 无本命令记载。依据源码：`Runtime/Script/Statements/FunctionIdentifier.cs:279`、`Runtime/Script/Statements/Instraction.Child.cs:1588`、`UI/Game/EmueraConsole.cs:699`

## 语义

按图片名从背景图列表里移除**一张**背景图（`FindIndex` 找到的第一项），然后重新烘焙背景。要一次清空全部背景图请用 `CLEARBGIMAGE`。

关键行为与坑：

- 名字比较是**序号比较（大小写敏感）**：`backgroundList.FindIndex(v => v.Value.bgImage.Name == key)`，其中 `bgImage.Name` 是精灵注册时统一转大写的名字（`UI/Game/Image/AppContents.cs:224` 对 CSV 资源名 `ToUpper()`），而 `key` 是 ERB 里写的原样字符串。因此 `REMOVEBGIMAGE` 必须写成**大写**的注册名，写小写虽然能 `SETBGIMAGE` 成功（`GetSprite` 内部大写化，大小写不敏感），却在 `REMOVEBGIMAGE` 时找不到。
- 名字不存在（或大小写不符）时，`FindIndex` 返回 -1，源码直接把 -1 交给 `List.RemoveAt`，会抛 .NET `ArgumentOutOfRangeException`（不是 CodeEE）——即「移除不存在的背景图」不是静默无操作，而是非受控异常。
- 同名图被添加多次时，只移除其中一项，其余保留。
- 只影响 `SETBGIMAGE` 添加的背景图，不影响背景色与其他绘制内容。

## 用法

### REMOVEBGIMAGE <图片名>

- `<图片名>`：与 `SETBGIMAGE` 相同的资源名，但本命令要求**大写**、且必须确实存在。

```erb
SETBGIMAGE BG_ROOM.PNG, 0, 255
SETBGIMAGE BG_RAIN.PNG, 5, 128
REMOVEBGIMAGE BG_RAIN.PNG      ; 只撤掉雨的背景，房间背景保留
REMOVEBGIMAGE BG_ROOM.PNG      ; 现在背景图清空
CLEARBGIMAGE                   ; 等价的批量做法
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:279`（`addFunction(FunctionCode.REMOVEBGIMAGE, new REMOVEBGIMAGE_Instruction())`，紧邻 `CLEARBGIMAGE`，第 278 行）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:179`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1588`（`REMOVEBGIMAGE_Instruction`；构造第 1590~1594 行，执行第 1596~1602 行）；最终落点 `UI/Game/EmueraConsole.cs:699`（`EmueraConsole.RemoveBackground`）
- 参数构造：与 `SETBGIMAGE` 同为 `FORM_STR_ANY`（`Runtime/Script/Statements/ArgumentBuilder.cs:559`，1 个以上的 FORM 字符串）

```text
构造期 REMOVEBGIMAGE_Instruction():
    ArgBuilder = GetArgumentBuilder(FunctionArgType.FORM_STR_ANY)
    flag = METHOD_SAFE | EXTENDED

执行期 DoInstruction（Instraction.Child.cs:1596~1602）:
    arg = (ExpressionArrayArgument)func.Argument
    bgName = arg.TermList[0].GetStrValue(exm)      # 只使用第 1 项
    exm.Console.RemoveBackground(bgName)

EmueraConsole.RemoveBackground(key)（EmueraConsole.cs:699~703）:
    index = backgroundList.FindIndex(v => v.Value.bgImage.Name == key)   # 序号比较，大小写敏感
    backgroundList.RemoveAt(index)      # index == -1（未找到）时抛 ArgumentOutOfRangeException
    BakeBackground()                    # 注意：异常在上一行抛出，烘焙不会执行
```

## 备注

- 与 `SETBGIMAGE`、`CLEARBGIMAGE` 同族、同样无中文文档：改造版里 `grep -rln REMOVEBGIMAGE` 只命中 `Runtime/Script/Statements/BuiltInFunctionCode.cs`、`Runtime/Script/Statements/FunctionIdentifier.cs`、`Runtime/Script/Statements/Instraction.Child.cs`，且**未改造的原版 Emuera 1.824（仓库根 `Emuera/`）里没有这个名字**（`grep -rl "REMOVEBGIMAGE" Emuera/` 零命中）→ 属 emuera.em 新增的私有扩展。
- 大小写陷阱是文档（若有）最容易写错的地方：同一名字在 `SETBGIMAGE` 里大小写均可，在 `REMOVEBGIMAGE` 里必须与注册名（大写）完全一致，否则报的是 .NET 异常而不是 Emuera 错误。此结论为读码推定（`List<T>.RemoveAt(-1)` 的语义 + 资源名 `ToUpper()` 的注册流程），未在两套文档中找到任何说明。
- `FORM_STR_ANY` 允许写多个参数，但实现只取 `TermList[0]`，多余的参数被忽略；`SETBGIMAGE` 则把第 2、3 项用作深度与不透明度，两者签名形似而参数用途不同。
