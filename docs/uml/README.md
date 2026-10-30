# emuera UML

这些 PlantUML 源文件描述当前实现的模块边界和跨边界协作。每张图只承担一个责任，源码路径写在图头部；渲染出的 PNG/SVG 属于本地产物，不纳入版本控制。

## 图索引

| 文件 | 图的责任 |
| --- | --- |
| `00-overview.puml` | 模块边界与依赖方向 |
| `01-load-classes.puml` | Content、GameData/ast、GameProc 的装载类型边界 |
| `02-runtime-step.puml` | `ScriptRunner` 步进、调用栈、`ExecState` |
| `03-runtime-system.puml` | `SystemStateMachine`、`SystemHost`、系统状态与输入等待 |
| `04-runtime-console-qml.puml` | `ExecutionEngine` 输出、`ConsoleBackend` 输入桥、QML 呈现与帧节拍 |
| `05-runtime-save-load.puml` | `SAVEGAME`/`LOADGAME`、`SystemHost` 回调和变量快照集成 |
| `06-expression-ast.puml` | `LogicalLine`、表达式节点和装载期访问描述符 |
| `07-variable-resolution.puml` | 作用域、名字到 locus 的唯一解析顺序 |
| `08-variable-storage.puml` | 存储容器、数组几何、`erasave::Map` 条目映射 |
| `09-diagnostics.puml` | 装载诊断数据结构、pass 切片和所有权 |
| `10-static-checks.puml` | 静态目标、结构和常量检查目录 |
| `11-save-format.puml` | `EraBinaryData` 编码、版本和失败原子性 |
| `12-load-pipeline.puml` | Content 输入到 AST、load pass 和诊断的时序 |
| `13-runtime-control-loop.puml` | SystemStateMachine、ScriptRunner、ProcessState、ExecutionEngine 的闭环、pump 防重入、等待与帧确认 |
| `14-system-host-capabilities.puml` | SystemHost 的 output/input/data/save/config/title 能力分组与可空注入边界 |
| `15-console-qml-handshake.puml` | ConsoleBackend 与 QML 的 generation、Qt.callLater、输入提交、销毁与 fallback 握手；`gridRows`/`stageRows` 口径与舞台图片层整层缩放 |
| `16-variable-persistence-boundaries.puml` | NameResolution、RuntimeStorage、SaveMapping、BinaryDocument 的边界 |

运行时故意拆成七张互补图：步进只描述脚本执行；系统图只描述帧底交接、状态分派和等待恢复；控制台图只描述信号、QML 属性和提交路径；存档图只描述运行时回调与快照；13-15 分别补足控制闭环、依赖注入和呈现握手。变量专题由 07、08、11、16 分开表达解析、运行时容器、文件格式和跨边界持久化。这样一个类只在拥有该责任的图中展开，跨图只保留接口关系。

## 源码边界

- `src/eraengine/Content` 负责 ERB/CSV 输入、编码和预处理。
- `src/eraengine/GameData/ast` 负责 AST、声明事实和装载诊断。
- `src/eraengine/GameData/Variable` 负责运行时容器、形状和存档条目映射。
- `src/eraengine/GameProc` 负责解析表、装载 pass、执行步进和系统状态。
- `src/eraengine/GameView` 负责控制台模型、输入表面和状态展示。
- `src/platform` 负责窗口、托盘和平台集成；`src/qml` 负责视图组合；`src/tools` 负责协议工具。

## 复杂度边界

`13-runtime-control-loop.puml` 是控制驱动的唯一闭环图：`ProcessState` 持有 `SystemStateCode` 与 `ExecState`，`SystemStateMachine` 负责处理器调度和 pump 门控，`ScriptRunner` 拥有调用栈，`ExecutionEngine` 只提供指令语义。`pump` 的防重入和 `TimedWait` 的 pending 语义不能被简化成互相递归的同步调用；`WaitInput`、`WaitSystemInput`、`WaitEvent` 是 `ExecState` 的统一枚举值，帧确认只表示 QML 模型/舞台布局已处理，不表示 scene-graph swap 或像素已显示。

`14-system-host-capabilities.puml` 将 `SystemHost` 按能力分组，但所有回调仍是可空注入；`ProcessState` 是控制闭环的必需状态依赖，具体 Console、存档、配置和标题提供者不是状态机所有者。`15-console-qml-handshake.puml` 约束 `generation` 为逻辑版本/按钮有效性令牌，销毁或未接线时必须存在 fallback。`16-variable-persistence-boundaries.puml` 明确解析、运行时值、条目映射和二进制编解码不可合并。

## 关键口径

变量解析只有一条顺序：段身份作用域由装载期写入 `LogicalLine::scopeKey`，运行时显式 intern 为 `ScopeId`；`VariableNode` 的 `AccessKind/VariableKey` 或 `VariableStorage::resolve*Locus` 决定位置，读写共享解析结果。

存档分成两个边界。`08-variable-storage.puml` 描述 `erasave::Map` 如何把声明映射到条目和存储槽；`11-save-format.puml` 只描述 `EraBinaryData::Version`、编码/解码和不兼容文件的原子拒绝；`05-runtime-save-load.puml` 描述状态机和执行引擎怎样调用它们。私有写盘键由 `makePrivateStorageKey(segment, name)` 统一构造，图中不再声称文件只保存裸名。

输入行不属于舞台正文。`EraEngine::syncConsoleGrid()` 的 `gridRows` 按**窗口高 / 行高**计算（含输入行，脚本看到的行数与 C# 一致）；输入框是 QML 独立控件（`ConsoleInput.qml`，位于 `Stage` 之外），因此 `ConsoleBackend::stageRows()` 暴露 `gridRows - 1` 作为正文可绘制行数。QML 必须按 `stageRows` 分摊行高：`cellHeight = gridHeightPx / stageRows`。若按 `gridRows` 分摊，正文与图片层会比舞台少一整行高度，最后一行落到输入行位置。

输入行本身由 QML 输入栏承载，最底一行随之移交。C# 的窗口最底一行是独立的输入框（`MainWindow.richTextBox1`），`doInputToEmueraProgram` 结尾的 `Print(str); PrintFlush(false);` 又把提交值写进正文；era 脚本也会把输入提示打到最新那一行。本移植的输入栏在舞台之外，所以当正文溢出舞台（`总行数 > stageRows()`）时，`ConsoleBackend::updateStageModels()` 把最新一行从舞台快照中摘出、作为 `inputEchoLine` 单独递交，`ConsoleInput.qml` 把它排在输入框左侧（`consoleInputEcho`）；没溢出时（eraTW 标题这 45 行定页画面）不摘，全部行都在舞台上。这样「舞台 45 行 + 输入回显 1 行 = 窗口 46 行」，与 C# 的行数一致。

舞台打印图片层的坐标与缩放。子项的**位置**按格子（`col/row`），**尺寸**用 C++ 量出的图片原始尺寸（`pxWidth/pxHeight`，= C# `ConsoleImagePart` 的 `Width/height`）：满格（`cols × 列宽`、`rows × 行高`）会把 180×180 的画像枠画成 184×176、180×260 的立絵画成 184×256，图本身被拉伸，框和立絵的边缘天然对不上，外层 `stageImageLayer` 只套一次等比 `Scale`（`cellHeight / lineHeight`）。画布列宽取「舞台真实列宽 ÷ 这个 scale」，缩放之后正好落回舞台的列，靠右的图不会被整体拉偏。纵向位置 = **行首**（行号 × `lineHeight`）**+ ypos 像素位移**（`pxTop = raw_ypos × fontSize / 100`，对齐 C# `ConsoleImagePart.DrawTo` 的 `rect.Y = pointY + top`）：位移发生在格子上，不是「算出负几格再乘行高」—— 后者会把不足一格的偏移放大成整格，画像枠/時間停止/特效这类叠层就会离开立絵一格，而 C# 里字号 16 / 行高 20 时它只差 4px。历史视图仍按行委托渲染，不用这层。

左右对齐只平移整行，不改变图片之间的相对位置。C# `ConsoleDisplayLine.SetAlignment` 用行的**像素宽**（`Σ part.Width`，图片算 `ConsoleImagePart.Width`）求 `movetoX`：左对齐且是逻辑行时直接返回（不动），居中 `WindowX/2 - width/2`，右对齐 `WindowX - width`。eraTW 的 `@画像セット` 把立絵放进 `レイヤー階数 0`、画像枠放进 `最大レイヤー数`，两者是**两次 `HTML_PRINT`、两行**，但 `@画像表示単独HTML` 为它们算出的 `width/height` 是同一个值（都是 `默认角色画像横幅 × 拡大比率 / 字号`），两行行宽相同 → 平移量相同 → 左对齐和居中都让框贴着立絵走，切换对齐方式不会把框推开。本移植按**列**算平移（`pointOffset = maxCols()/2 - widthUnits()/2`，网格是整数列），同一行里的文字与图片共用这一个偏移，因此二者仍然同列。其他会改变两层相对位置的情况包括：行高 ≠ 字号时每上一层低 `(行高 − 字号)`（eraTW 的 `emuera.config` 是字号 16 / 一行の高さ 16，`100*(行高/字号)` 的整数除法正好抵消一整行；改成 20 就会出现 4px），以及某个角色的枠资源缺失（那一层少一张图，同层后续角色的框会整排左移）。

eraTW 左对齐的实际拼接顺序是 `<shape type='space' ...><p align='left'><img ...>`：占位在对齐标签前。`renderTemplate()` 遇到开始标签 `<p align>` 只能改变本行对齐，不能提交已经积累的占位，否则每层会多出一行，框与立绘相隔两行而 `ypos` 只补偿一行，导致框低一整格。字符串槽的结构化值与纯文本覆盖后读回都必须保留 `ypos`，并产生同样的行数。

静态检查发现的未定义函数属于内容/脚本诊断。运行时不会伪造 `@GAMEOVER` 实现；如果脚本在执行时调用缺失标签，仍按执行错误处理。

## 渲染与检查

```sh
plantuml -tpng docs/uml/*.puml
plantuml -tsvg docs/uml/*.puml
```

当前环境使用 PlantUML 的 `smetana` 布局，因此不依赖 Graphviz。只检查语法而不提交生成的图片时，可使用：

```sh
set -eu
for f in docs/uml/*.puml; do
  err=$(mktemp)
  if ! plantuml -pipe -tsvg < "$f" >/dev/null 2>"$err" || [ -s "$err" ]; then
    cat "$err" >&2
    rm -f "$err"
    exit 1
  fi
  rm -f "$err"
done
```

可用 `rg -n 'class |signal|Q_PROPERTY|saveTo|loadFrom|runSlice|onFramePresented|notifyFramePresented|generation|Qt.callLater|SystemHost|ExecState|SystemStateCode' src/eraengine src/qml` 对照图中的接口；建议检查 13-16 是否仍分别只表达控制闭环、宿主能力、呈现握手和变量持久化边界。图中文字只保留能从当前源码确认、且对边界有帮助的事实；历史性能记录、重复实现说明和已删除的装载阶段放在版本历史中，不放在结构图索引中。
