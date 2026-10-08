# CLEARBGIMAGE

> 来源范围：本文的“本仓库”“当前实现”在描述语义、注册或源码行为时，指 C# 参考树 `emuera.em/Emuera/`；其他 C# 版本另按文中路径标注。资料收录范围仍指仓库内的参考材料。

- **类别**：EE 扩展命令（Emuera 枚举成员 `Runtime/Script/Statements/BuiltInFunctionCode.cs:178`；两套中文文档均未收录，语义据源码）
- **签名**：`CLEARBGIMAGE`（无参数；多写参数只会得到一条级别 1 的警告，命令仍执行）
- **文档来源**：无中文文档收录。ecd 里功能最接近的是 `CBGCLEAR`（`ecd/docs/translation/Command.md:2586`「解除由 CBG 系各指令设置的全部背景图像设置」），但那是另一套 ClientBackground 机制；`grep -rn "CLEARBGIMAGE\|BGIMAGE" test/data/_extracted/` 无本命令的任何记载。依据源码：`Runtime/Script/Statements/FunctionIdentifier.cs:278`、`Runtime/Script/Statements/Instraction.Child.cs:1604`、`UI/Game/EmueraConsole.cs:693`

## 语义

清空**全部**由 `SETBGIMAGE` 添加过的背景图片，并立刻重新烘焙背景（`BakeBackground`）。背景色（`SETBGCOLOR`）不受影响；`RESETBGCOLOR` 也不影响背景图。

由于清空是「列表整体清除 + 重烘焙」，多次调用等价于调用一次；在没有任何背景图时调用也是安全无副作用的（列表本来就是空的）。

## 用法

### CLEARBGIMAGE

```erb
SETBGIMAGE bg_room.png, 0, 200
SETBGIMAGE bg_rain.png, 5
PRINTL 现在有两张背景图叠着
CLEARBGIMAGE
PRINTL 背景图已全部清除（背景色仍是 SETBGCOLOR 设定的）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:278`（`addFunction(FunctionCode.CLEARBGIMAGE, new CLEARBGIMAGE_Instruction())`，紧邻 `REMOVEBGIMAGE`，第 279 行）；枚举定义 `Runtime/Script/Statements/BuiltInFunctionCode.cs:178`
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:1604`（`CLEARBGIMAGE_Instruction`；构造第 1606~1610 行，执行第 1612~1615 行）；最终落点 `UI/Game/EmueraConsole.cs:693`（`EmueraConsole.ClearBackgroundImage`）

```text
构造期 CLEARBGIMAGE_Instruction():
    ArgBuilder = GetArgumentBuilder(FunctionArgType.VOID)   # 不接受参数，多写参数会警告「参数不是必需的」
    flag = METHOD_SAFE | EXTENDED

执行期 DoInstruction（Instraction.Child.cs:1612~1615）:
    exm.Console.ClearBackgroundImage()

EmueraConsole.ClearBackgroundImage()（EmueraConsole.cs:693~697）:
    backgroundList.Clear()
    BakeBackground()      # 重绘：清空画布 → 依列表重画（列表已空 → 只留下透明背景）
```

## 备注

- 与 `SETBGIMAGE`、`REMOVEBGIMAGE` 是同一族私有扩展，同样没有中文文档；本仓库 C# 源码是唯一权威来源（改造版里 `grep -rln CLEARBGIMAGE` 只命中 `Runtime/Script/Statements/BuiltInFunctionCode.cs`、`Runtime/Script/Statements/FunctionIdentifier.cs`、`Runtime/Script/Statements/Instraction.Child.cs`），并且**未改造的原版 Emuera 1.824（仓库根 `Emuera/`）里没有这个名字**（`grep -rl "CLEARBGIMAGE" Emuera/` 零命中）→ 属 emuera.em 新增而非原版命令。
- 与 ecd 记载的 `CBGCLEAR` 名近而实异：`CBGCLEAR` 属于 `CBG*`（ClientBackground）体系，本仓库 C# 侧没有命令实现（只在 `Runtime/Script/Statements/Function/Creator.cs:197` 注册了 `CBGCLEAR` 式中函数名），二者操作的对象不同，不能互相替代。
- 无参数：`VOID_ArgumentBuilder`（`Runtime/Script/Statements/ArgumentBuilder.cs:602` 起）在行尾后仍读到内容时给的是**警告**「不要参数」，不会让命令失败。
- 该命令不清除 `SETBGCOLOR` 设定的背景色，也不清除用 `PRINT_IMG` 等打印到行里的图片；它只清空 `EmueraConsole.backgroundList`。
