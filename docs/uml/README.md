# emuera UML（当前实现）

PlantUML 源码。所有图都是**当前代码**的静态结构，不包含计划中的设计。
渲染产物（`.png` / `.svg`）是构建输出，`.gitignore` 里已排除，不入库。

| 文件 | 类型 | 内容 |
| --- | --- | --- |
| `00-overview.puml` | 组件图 | 分层总览：装载期一次定型 → 运行期只做存取；收尾三层（`parsecheck` 只读 / `loadpass` 可写 / 类只剩编排）；标记区 `jumpmark` 与位置区 `position` 各只有一份实现 |
| `01-load-classes.puml` | 类图 | 装载期：`EraEngine` / `ErbLoader` / `AstBuilder` / `declscan`（「哪一行是声明」的唯一判据）/ `EraParseTable`（`buildScriptAst` = 唯一的脚本级构建入口，声明是它的前置步骤） / `ParseIrView` 与 4/5/6 自由函数 / `LoadContext` 与 1/2/3 自由函数 / 标记区 `jumpmark` / 标签索引 `m_labelIndex` / 位置区 `PositionState`+`PositionEffects`+`position` 自由函数 / `LoadPhase` 阶段机 / 带 `origin` 切片的 pass 表 |
| `02-runtime-classes.puml` | 类图 | 运行期：`ExecutionEngine` / `ScriptRunner` 单步状态机 / `ProcessState` / `LoopFrame`（作用是显式 `ScopeId`；位置推进经 `position` 自由函数） |
| `03-expression-classes.puml` | 类图 | 表达式 AST、`AccessKind` 16 值、`VariableKey{scopeId,nameId}`、名字 intern 表、`resolverTable` |
| `04-load-pipeline.puml` | 时序图 | 装载流水线：CSV → declscan 声明扫描 → 逐文件解析 → `m_loadPasses[7]` 表驱动收尾（每步独立 sink + `replaceOrigin`）→ 诊断 |
| `05-variable-access.puml` | 活动图 | 运行期变量访问：两条入口 → 一张 resolver 表 → 唯一按名原语（解析一次产出 `IntLocus`，读/写各一个穷尽 switch）；作用域 = 段身份（装载期写进 `line.scopeKey`） |
| `06-diagnostics.puml` | 类图 | 诊断模型：`ParseDiagnostics` / `ParseDiagnostic` / `DiagEffect` / `DiagOrigin`（pass 切片）/ `DiagCode` 全码与产生步骤 |
| `07-static-checks.puml` | 卡片图 | `checkStaticValues` 检查目录（C1..C4）与已验证口径 |
| `08-save-load.puml` | 时序图 | 存档 / 读档：`erasave::Map`（条目 ↔ 身份的唯一一张表）、`SaveReadiness` 前置条件、版本常量与旧档判废、私有条目写盘名可还原段身份 |

同目录的 `.png` / `.svg` 是渲染结果（本地生成，不入库），按下面命令随时重渲染。

## 渲染

```bash
plantuml -tpng docs/uml/*.puml      # 位图
plantuml -tsvg docs/uml/*.puml      # 矢量
```

源码里带 `!pragma layout smetana`：本机没有 graphviz（`dot`），只能用 PlantUML 内置的 smetana
布局引擎。装上 graphviz 后删掉该行（以及上一行注释）即可得到更好的自动布局。

活动图（`05-variable-access.puml`）另有 `!pragma useVerticalIf on`，让 if/elseif 竖直展开；
旧版本 PlantUML 忽略该行也能渲染，只是分支会横向铺开。

## 与 Umbrello 的关系

Umbrello 不能导入 PlantUML，只认 XMI。PlantUML 可导出类图 XMI：

```bash
plantuml -txmi docs/uml/01-load-classes.puml
```

但导出的 XMI 只含类/属性/操作，**不含关联与图形布局**（UML 1.3 / XMI 1.1，Umbrello 打开后需重新排布）。
需要完整可编辑模型时，用 Umbrello 手工建立或用 Umbrello 自己保存的 XMI 作为模板。
本机 `umbrello6 --export` 在无头环境下会崩溃（KCrash / SIGSEGV），只能在其 GUI 中打开确认。

## 口径提醒（图中标注）

- **变量身份是整数，不是字符串**：`VariableKey { scopeId, nameId }`（名字用 intern 表换成 `quint32`）。
  私有存储键 `段身份文本 + 0x1F + 名字` 只在 `makePrivateStorageKey` 一处拼出来（存档写盘名、
  存储层字符串键都从这里取），比较是 `quint32` 相等。上一版两侧各拼一次、必须逐字节一致 ——
  任何拼写/分隔符疏漏都表现为「某个变量悄悄变成另一个变量」。
- **作用域身份是段身份，不是函数名**：`(脚本名, 段起点行)`，段起点行 = 该段那个 `@函数标签`
  的行号（文件头段 = -1），与 `LabelRef::functionLine` 是同一份事实。同名 `@F` 的两份声明因此是
  两个段、两套私有域（C# 把它挂在各自的 FunctionLabelLine 上）。身份文本由
  `eravar::segmentKeyText` 唯一构造、**总含 ':'**（ERB 标识符不含 ':'，所以作用域 id 与变量名 id
  不可能互相冒充）；装载期写进 `line.scopeKey`，运行期只 `intern` 一次、不按名字回查。
  `eravar::kGlobalScope = 0` 仍是「无私有域」，`kInvalidScope` 显式表示无效。
- **装载期全定型**：`AccessKind` 16 值。封闭 5 类（形参 / 伪变量 / 系统槽 / 角色 / 常数）
  各有 kind；`usesKey(kind)` 标明哪些 kind 消费 `accessKey`（守卫的适用范围本身是数据）。
  真实语料里 `Dynamic` 只剩「有系统元数据但无系统存储槽」的裸名：eraTW 0.5% / FL3.8 0.3%。
- **分派是一张表**：`ExpressionEvaluator::resolverTable()` 20 行，顺序与旧 `if` 链逐块对应
  （`kinds` 位掩码 + `needsKey` + 读/写处理函数）。`readIntNode` / `writeIntNode` 是读写各一条入口，
  FOR/NEXT 与表达式求值共用，不再各自实现一份 `switch`。
- **作用域是参数**：`eravar::ScopeId` 由 `ScriptRunner::m_currentScopeId`（换函数时更新一次）
  和 `ExecutionEngine::m_activeScope` 显式下传；求值器不读 storage 里的隐含状态。
  键自带作用域，所以**没有「作用域守卫」**。只剩一条：裸键下标数与维数不符 → 按名原语
  （其行为即旧路 `throw`）；私有键保持宽松 1 维读。
- **条件求值没有「当前位置」这个输入**：`LogicalLine::conditionNeedsScope` 是装载期的
  一个 bool —— 收尾定型对条件 AST 的那趟 `walkApply` 用 `VariableNode::needsExecutionScope(kind)`
  一趟 OR 出来（与 `accessKind` 同趟，唯一写入点），运行期只读。只有 `Param`（字符串形参的
  私有槽解析）/ `Chara` / `Const` / `Dynamic` 消费 scope；键自足的 10 类与 `Pseudo` / `System`
  不消费，于是 `evaluateCondition` 对它们直接传 `kGlobalScope`，**不取位置、不查段身份**。
  `evaluateAst` 收显式 `ScopeId`（无默认参数 —— 不允许静默回落到「解释器停在哪一行」）。
  旧形态：每条 IF/SIF/WHILE 都无条件 `executionScopeId()`（QString 哈希 + toUpper + intern 互斥）。
- **单一按名原语，且顺序只有一份**：`VariableStorage::readIntByName` / `writeIntByName(name, indices, scope)`
  仍是「名字 + 下标 → 存储」的唯一入口，但它们不再各自重走一遍判定：`resolveIntLocus` 解析一次，
  产出 `IntLocus{ kind, key, charaId, e0..e2 }`（**位置是数据**），读/写各自只做一次
  `switch(kind)`，`kind` 是 11 值枚举且两个 switch **都没有 `default`** —— 漏一类在
  `-Wswitch` + `-Werror` 下直接编译失败。判定行序即语义：形参 → 角色（字符串/1 维/2 维）→
  ARG/LOCAL → ARGS/LOCALS/RESULTS → 系统变量 → 声明维数 ≥2 的 2D/3D → 1D。
  旧形态是**三份副本**：头注释一份（还写错成「整型读为 0」，实际读落到 1D 槽）、读链一份、
  写链一份；往一边加一类而忘另一边时编译期不报、语料也未必报，症状是「写进去的那一格
  读出来是另一格」。`ARGS/LOCALS/RESULTS` 的读写不对称是 Era 语义、不是实现分叉，由
  `test_name_locus` 的 51 条断言显式钉住（不是消掉）。`storageKeyFor` 仍是唯一 id→串入口，
  存储容器仍是字符串键（存档格式不变）。
- **没有「构建后再重建」这一步**：预扫描本就覆盖 `VARI`/`VARS`（它们复用 `#DIM` 的
  `registerScopedVariableLine`），定型失败的真因是拿**整段左值文本**（含下标）查名字表。
  新增唯一实现 `VariableTable::baseName`（构建期定型 / 收尾字符串赋值 / 收尾左值诊断三处共用），
  `strAssignPending` 从 eraTW 48,514 / FL3.8 16,979 / eraTetris 99 **降到五语料全 0**。
  字符串 `++/--` 的目标也改为构建期用同一套带折叠 resolver 解一次（全量 AST 逐字节对比：
  三语料 112.7 万行 IDENTICAL）。于是 `recompileFuncLinesInScript`（18 行）、
  `rebuildAssignmentAstsInScript`（12 行）、`applyStringAssignments` 里的无条件 FORM
  重编译（约 10 行）**全部删除**；剩余门控只剩数据（`needsCompletion`），不是再猜一遍。代价：多扫一遍，eraTW/eraMegaten 装载均 <3% 变化。
- **赋值左值的 AST 在构造期就完备**：`#DIM CONST` 标量的折叠原来只在收尾重解析里做
  （`parseScopedExpression(foldConstants=true)` 在 `parsePrimary` 装
  `setConstantValueProvider`），构造期 `resolveExpr` 没装 → 每行白解析一遍。
  现在抽出唯一查值入口 `VariableTable::constantValue(name, scope)`，两侧共用；
  `AstBuilder::build` 的 `resolveLvalue` 让赋值左值也折。全语料 852,937 行
  逐节点比对 **0 差异**后删掉重解析 —— eraTW CPU −2.7%、eraMegaten CPU −5.6%。
  原则记在此：**能在构造/定型期做的就做完，运行期只查表**；
  `strAssignPending`（VARI/VARS 是语句、预扫描只扫 `#` 行）与 GETVAR 族
  （名字是运行期字符串，按 C# 语义必须不折）是两条有理由的例外。
- **常量子树只求值一次，构建期算过的就查值**：`load_passes.cpp::constantValueAt(ctx, node, scope)`
  是装载期唯一的常量求值入口 —— `node.constantIndex.isValid()` 就直接返回构建期算好的值，
  否则才调 `evaluate`。构建期的 `constantIndexEvaluator` 与收尾 `validate()` 都只走它，
  于是装载期 `evaluate` 调用点 **2 → 1**（原先校验趟对已有 `constantIndex` 的子树又算了一遍）。
  `VariableTable::fillConstantIndex` 求值前先 `idx.constantIndex = {}`：否则同一节点按另一段
  重定型时会命中上一段的旧值（缓存反噬，症状是同一行在两次定型后给两个常量答案）。
  实测跳过 269 次（eraMegaten 266 / eraTetris 3 / 其余 0），**269/269 与逐节点重算逐节点相同**、
  mismatch 0。eraTW 跳过为 0 是**事实而非遗漏**：该语料的常量下标在构建期已折成 `LiteralNode`，
  而 `Literal` 分支本来就不求值；剩下的求值全是 `StrForm`/`If` 根（无 `constantIndex`），
  属于校验趟的**首次**求值。
- **没有「待收尾定型」的第三态**：`LogicalLine::rhsPending` 已删。它存在的前提是
  「AST 构建期看不到类型表」——路径 B（调用方在 `loadScript` **之前**建好 AST）当时确实
  没有声明表，于是字符串赋值/复合赋值把定型拆成两段、两份判据（实测就是它把
  `@HOLDER #DIM CONST SHADOW = 51` 的私有 `#DIM SHADOW` 折成字面量 51、
  又把 A–Z 系统 Int 变量挡住 `#DIMS` 的字符串声明）。现在**声明是构建入口的前置步骤**：
  `EraParseTable::buildScriptAst()` 是脚本级构建的唯一入口，它调
  `declscan::scanScript()`（「哪一行是声明、属于哪个段」的唯一扫描循环，`SourceLine` /
  `LogicalLine` / `QStringList` 三形态共用同一 `scanImpl`）后把声明登记进 **`m_variables` 本身**
  —— 构建期看到的表与 `loadScript`/收尾用的是**同一个对象**，不是两份视图
  （先前一版用临时 `VariableTable` 做「本脚本视图」正是第二份判定，已弃）。
  同一份扫描还产出 `segmentStartOfLine`（段起点）与 `orphanSharpLines`（声明区外的 `#` 行）；
  `classifySharpDirective()` 是「指令 → 语义」的唯一映射，`AstBuilder::labelNameEnd()` 是
  `@`/`$` 标签名判据的唯一实现。连带删除：`load_passes` 的整块结算（45 行）与门控、
  死函数 `parseVariableDeclaration(const LogicalLine&)`、`ErbLoader` 的三段手写扫描
  （1589 → 1462 行）。验收：全量 AST 逐字节 **1,076 万行 IDENTICAL**（eraTetris 1,027 /
  eraTW 2,111,136 / eraMegaten 5,550,572，2.05 GB dump），五语料告警逐字节不变。
  代价与收益：声明前置让 `isScopedVariableLine` 每行被调 2 次（实测 merge 跌到 9,112 ms），
  `parseScopedVarSpec` 加首字符快路径（指令头必是 `VARI`/`VARS` ⇒ 首字符必为 `V`/`v`，
  判据仍只有一段代码）后 merge 6,235 ms；最终 eraTW −6.9% wall / −5.7% CPU、
  eraMegaten −4.9% / −3.6%。
- **一个脚本只扫描一次（扫描结果是值，随数据走）**：`declscan::Scan` 是纯值，
  于是「预处理 + 扫一遍」的产物可以传下去，而不是每个消费者各自重扫：
  `ErbLoader::scanFileDeclarations` 预处理一次 + 扫描一次给出 `Scan`，
  `preRegisterDeclarations`（登记）与 `LoadTimeVarTypes`（并行解析的类型快照）
  都只**消费**它；`BuiltScript { lines, scan }` 让「脚本级构建 + 装载」也只有一份
  —— `buildScriptAst` 可吃/写回 `Scan`，`loadScript` 吃 `Scan`。
  `Scan::covers(lineCount)` 是消费者的自检（`scanImpl` 每个分支恰好 append 一次段
  起点 ⇒ 同源同长度必然成立），用来判断「传进来的是不是这批行的扫描结果」，
  而不是重扫一遍再比。实测每文件扫描次数：串行目录装载 eraTW **3 → 1**、并行
  **3 → 1**、异步 eraTetris **3 → 1**、单文件 `loadFile`（构建 + 装载）**2 → 1**；
  工具路径（调用方手工构行 = 另一份数据）仍是 1 次，本来就只有一次。
  变异证据：把 `declScan` 不传给 `loadScript` → 2258 个文件回到 2 次（总 4518），
  五语料告警与全量 AST **逐字节不变** ⇒ 第二次扫描是纯冗余。
  收益：eraTW 34.76 → 29.85 s wall（−14.1%）、eraMegaten 115.0 → 101.1 s（−12.2%）。
  未做（记账）：`--async` 只在 eraTetris 4 文件上逐文件量过计数；串行 `loadFile` 仍
  会再读一次盘（`decoded` 只喂了扫描）—— HEAD 既有浪费。
- **同一份文件字节只读一次盘**：与 `Scan` 同一条思路 —— 一次读取的产物随数据走，
  下游不重读。三处都曾是「预扫描已拿到文本、下游又自己读一遍」：串行 `loadDirectory`
  的 `decoded` 只喂了扫描、`loadFile` 又读同一文件；宏表循环把同一个 `.ERH` 读了
  两遍（`collectDefines` / `collectMacroTable` 两个消费者）；异步 `prepareLoad` 的
  `scanOne` 又读一遍宏表阶段读过的 `.ERH`。现在 `loadFile(path, root, scan, content)`
  吃已读文本（`QString` 隐式共享，非空 = 同一份字节），宏表阶段只读一次到 `headerText`，
  异步把「文本 + 成败」打包成一次读取的两个事实（读取失败仍只计一次、只报一次）。
  实测每文件读盘（插桩 `TextCodecUtil::readFile`）：串行 eraTW ERB 总读 **4,594 → 2,260**
  （头文件 **4 → 1**）、并行 2,334 → 2,260、异步 2,297 → 2,260；全部读（含 148 个
  `Chara*.csv` 各 2 次）串行 4,914 → 2,580。变异：串行不传 `content` → 2,408 个文件
  回到 2 次而输出逐字节不变（第二次读盘纯冗余）；传空文本 → `test_load_contracts` ④ 变红。
  峰值 RSS 与耗时在噪声带内（被消掉的副本上限 ≈1 MB，相对 3.5/11.2 GB 不可见）。
  未做（记账）：`loadFile` 里「只为取 warnings」仍再预处理一遍（`buildAst` 内部已
  `prepareLines` —— CPU 不是读盘）；148 个 `Chara*.csv` 每文件仍读 2 次
  （`ConstantTable` 与 `CsvLoader` 是两个消费者读同一份字节，属 CSV 装载统一改造）。
- **同一份文本只预处理一次**：`buildAst` 返回 `BuiltAst{ lines, warnings, scan }`
  —— 一趟构建的三样产物一起交回（原来的 `declscan::Scan* scanOut` 因此消失，
  从「返回值 + 2 个 out 指针」变成「返回值 + 1 个诊断累加器」，与 `declscan::BuiltScript`
  同形态）。修前串行/单文件路径把同一个文本预处理两遍：`buildAst` 里那次的行被用掉、
  warnings 被丢掉；`loadFile` 又跑一遍只为取 warnings，行当场丢弃。实测
  `prepareLines` 每文件：串行 **2 → 1**（eraTW 4,516 → 2,258）、单文件 **2 → 1**、
  并行/异步本来就 1。五语料**不覆盖**这条路径（eraTW 6 条 / eraMegaten 2 条全是结构化
  解析诊断，预处理告警 0 条），因此用六组夹具逐类覆盖 `ErbPreprocessor` 的 19 处告警
  文本，HEAD 二进制 vs 修后输出逐字节 IDENTICAL ×6，并把 7 条告警在「串行 / 并行 /
  单文件 loadFile」三个入口钉进 `test_load_contracts` 12)。变异：丢弃交回的 warnings →
  7 行变 0 行、测试 2 红；恢复第二遍预处理 → 14 条（期望 7）2 红。
  未做（记账）：每文件仍有 **2 次** `ErbPreprocessor::process`（`scanFileDeclarations`
  一次产出 `Scan` + `prepareLines` 一次建 AST）。合成 1 次要让预扫描的**预处理行数组**
  活到构建期 —— eraTW 210 万逻辑行常驻，远大于现在 93 MB 原始文本，必须先管住峰值内存。
- **预处理一趟的产物，扫描与构建共用**：`ErbPreprocessor::process` 每文件 **2 → 1** 次
  （修前 `scanFileDeclarations` 一次产出 `Scan`、`prepareLines` 又一次建 AST）。障碍是
  「预处理行数组要活到构建期」，于是先量后改：全库常驻 `QList<ErbSourceLine>` 按
  `sizeof` + 实测锚点推算是 eraTW **+142 MB（+4.1%）** / eraMegaten **+366 MB（+3.2%）**
  —— 超 2% 判据、否决；改用**一份正文 + 每行 12 B 跨度** `(offset,length)`
  （eraTW +19 MB ≈ +0.56%、eraMegaten +44 MB ≈ +0.4%）通过，而且实测峰值**下降**
  （注释 / 空行 / CR 在预处理后不占地方）：eraTW 3,477,068 → 3,425,816 KB（−1.5%）、
  eraMegaten 11,119,412 → 10,981,984 KB（−1.2%）。形态是 `ErbPreprocessed{ text_, spans }`
  + `materialize()` 投影（不是第二份事实）、`ScannedFile{ pre, scan, warnings }`（一次预扫描
  的全部产物，取代原先同键同生命期的 `decoded` + `scans` 两张表），四条装载路径都从这里取。
  收益：eraTW CPU −3.2%、eraMegaten CPU −3.1%。变异：构建期无视产物自己重预处理 →
  计数回到 2.00/文件而告警逐字节不变（纯冗余）；`textAt()` 每行少取 1 字节 →
  eraTetris AST 从第 2 行起不同（证明逐字节验收对行文本灵敏）。
- **CSV 的字节也只读一次**：`CsvTextTable`（`get(path, *ok)`：首次读盘并按文件嗅探编码，
  之后同一路径的所有消费者拿同一份 `QString`，隐式共享无副本）是「CSV 一次读取」的唯一
  产物。两个消费者共用**字节**、各自保留**解析**（`ConstantTable` 建名表取第 0/1 列、
  `CsvLoader` 按 `番号/名前/基礎/…` 切行 —— 两个消费者不是两份判据）。装载链把表建在栈上
  （异步用 `shared_ptr` 活到 future 结束），`loadConstantData` 收尾 `clear()`，不常驻。
  实测读盘（插桩 `TextCodecUtil::readFile`）：eraTW `Chara*.csv` **290 → 145**、
  GameBase/Item/VariableSize 各 **2 → 1**、全库 **2,580 → 2,432**；
  eraMegaten 全库 **13,100 → 10,782（−17.7%）**；修后每文件恰好一次。
  正确性证据不只是告警逐字节：临时探针导出全部常量名表 + 全部角色变量，
  HEAD vs 修后逐字节相同（eraTW 274,384 B / 145 模板、eraMegaten 6,208,523 B / 2,057 模板）。
  变异：字节来源换回自己再读 → 读盘回升 2,432→2,577 而告警不变；
  喂错文件（全喂最后一个）→ `test_dir_tree` 4 红、角色模板从 145 塌成 1。
  记账：读失败时补回两条既有日志（`VariableSize` / `Item` 的「加载失败」），因为走文本表
  后原告警分支不再经过 —— 保住原可观测性；`VariableStorage::loadVariableSizes(path)` 无生产
  调用方（只是 read + 委托，不构成第二套判据，可下一刀清）。
- **异步装载的完成信号只有一个发布点**：`EraEngine::completeAsyncLoad(ok)` 是
  `scriptsLoaded` 的唯一出口，且用 `QTimer::singleShot(0)` 投递到**下一轮事件循环**
  —— 修前失败路径在 `loadAsync()` 内**同步** emit，调用方 `connect → loadAsync → exec()`
  时 `quit()` 落在还没跑起来的循环上 = 永久挂死（`--async` 在「目录里没有 ERB」时
  rc=124）。「恰好一次」由世代字段保证（登记时记下当前世代，同代第二次 = warn + 不重发；
  `stopAsyncLoad()` 推进世代，旧定时器在回调里被世代检查丢弃）。同时
  **零工作量是显式成功状态**：空目录 = 零个文件的空集，由 `parsed >= total (0)` 收尾，
  与同步 `loadDirectory`「空文件列表 → true」同一事实（`LoadPrep::ok` 这个「future 已完成」
  的第二份副本因此删除）。六种边界夹具（只有 CSV / 空目录 / 空 ERB 目录 / 只有 .ERH /
  1 个 ERB / 全被跳过）×（同步 / 异步）修后全部 rc=0 且输出逐字节相同；`ecd --async`
  从 rc=124 变 rc=0 / 27 ms。变异：撤掉修复 → 8 红（原缺陷被超时判红）；
  完成信号发两次 → 9 红。行为变化显式记账：删掉一条「后台预扫描失败（文件列表为空…）」
  诊断（空目录不再算失败，读取失败仍有计数 + 逐条告警）、`--async` 装载失败不再退出 1
  （与同步一致）、`test_file_case` 一条断言随契约改为「完成推迟到事件循环 + 恰好一次」。
  保留的不对称（有意）：`loadDirectoryAsync` 对「上一轮仍在进行 / 目录无效」仍是同步
  **拒绝**（那是拒绝，不是装载完成）。
- **同一诊断只报一次（A/B 镜像按 (文件,行,消息) 抑制）**：eraMegaten
  `SHOP_LABORATORY.ERB:108` 同一现象报两遍 —— A 是左值专用 `RejectLine`（带 snippet），
  B 是 `validateArguments` 通用出口 `"%1 (%2)"` 把**同一个** `typeError` 再包一次并追加原文
  后缀。判据做成数据：「本行已有 A 级拒绝诊断」是 `ParseDiagnostics` 里的**显式登记**
  （唯一登记点 `add()` 收下 `effect == RejectLine` 时写入 `(文件 \x1f 行 \x1f 消息)`；
  唯一查表点 `hasRejectFact`），B 的出口只加一个查表条件。派生集合随 `replaceOrigin` /
  `removeOriginFiles` / `clear` 整体重建，切片重跑不会读到已删事实。**键含 message** 是精确性
  的关键（同一行可以有两个不同事实，夹具钉住「下标越界 A + 不能赋值 const B」两条都留）。
  效果：eraMegaten 基线 **411 B/2 行 → 166 B/1 行**（用户裁决允许这 1 行变化），其余四语料
  逐字节不变，AST 1076 万行 IDENTICAL。变异：键丢掉 message → 5 红（跨事实误吞）；
  删掉抑制 → 25 红（镜像重现）。未修（记账）：3/6 内部「实参循环 validate + 左值专用诊断」
  会产两条 **A/A** 重复，要收需让实参循环跳过左值槽位。
- **逻辑舞台 + 等比缩放呈现**（显示层，不动引擎语义）：舞台是 **C++ 的事实**
  （配置 `ウィンドウ幅/高さ`：eraTW 1400x750、eraTetris 760x640），网格由舞台推导
  （列 = 舞台宽 ÷ (字号/2)、行 = 舞台高 ÷ 行高）→ `maxLineUnits` 与 `GETCLIENTSIZE`
  都读这同一份网格；呈现层只做**一个 `Item.scale`**（contain，居中，可 >1 以适配大屏）。
  于是**窗口尺寸不进入任何排版公式**：实测 eraTetris 在 475x400 / 760x640 / 1400x750 /
  1901x1033 四窗口下 grid 恒为 84x33、clientSize 84x33、字号 18.00；eraTW 恒为 175x46 /
  字号 16.00。舞台可以大于窗口（eraTW 在 475x400 里整块缩到 0.3393），也可以小于窗口
  （tetris@1901x1033 scale 1.5063）；contain 取两边比的小者，黑边定义明确。
  反向约束也去掉了：QML **不再**反向定义画布（删掉 `updateGrid()`），网格只有一个来源。
  记账：桌面窗口 == 配置时 scale 仍略 <1（tetris 0.892 / tw 0.911），因为舞台是整块配置
  画布而窗口高度含 69px 界面件（菜单栏 28 + 输入行 ~29 + 状态条 12）—— 要让桌面 1:1 得把
  界面件高度从行数语义里排除，属引擎侧，未做；界面件在屏幕空间不随舞台缩放（有意：
  Android 上输入要可读）；xcb/wayland 与 Android 真机未跑。
- **窗口几何只有一个来源**（显示层，不动引擎语义）：
  `targetWindowSize()` 是唯一尺寸来源（配置值，仅 320x240 下限，**不做屏幕钳制**），
  `applyWindowSize()` 与 `verifyWindowSize()`（visible 后 0ms 与 300ms 各对账一次）都用它；
  QML 侧不再有任何窗口几何绑定（删掉拿 `gui.windowWidth` 自己缩放的 `gameStage`）。
  修前配置 760x640 而实际 `QWindow` 是 1901x1033 —— 两边各说各话。网格同理：
  `columnsFor/rowsFor` 是纯函数（列 = ⌊(宽−滚动条)/(字号/2)⌋、行 = ⌊(高−1)/max(行高, 字号+1)⌋），
  `cellW/cellH` 由网格反算，同一份网格经 `updateGrid()` 推给 C++（`maxLineUnits` /
  `GETCLIENTSIZE`），排版宽度与显示永远一致；字形 `Scale` **只压缩不放大**。
  修前 475x400 下行距 11.8 px < 字高 18 px → 文字重叠、`[是]/[否]` 挤成一条。
  实测四尺寸 `cellH ≥ 字号+1` 全部成立（最紧 eraTW@1400x750 = 17.050 ≥ 17），
  字号全程等于配置值。原生菜单栏的能力判定也收成一处（`platformSupportsNativeMenuBar()`
  只认 xcb/wayland）：修前只看 D-Bus 有宿主，于是 offscreen/minimal 在
  `setInitialProperty → Qt6LabsPlatform` 段错误（rc=139）。
  记账：xcb/wayland 未真跑（本机无 X 服务器）；wayland 下合成器仍可能改写几何
  （日志会打「启动几何被窗口管理器改写」）；eraTW@1400x750 余量只有 0.05 px。
- **GETVAR / GETVARS / SETVAR 的名字解析有缓存（唯一表、唯一键）**：键 =
  `(名字文本.trimmed(), 段身份文本)`，值是不可变 AST 的 `QSharedPointer`；容量 512、
  满时淘汰 stamp 最小者（近似 LRU）。失效只有一个入口、6 个调用点（`clear` /
  `beginIncrementalLoad` / `loadScript` / `markLoadComplete` / `setExpressionEvaluator` /
  `setConstantTable`）—— 脚本替换后**不复活旧 AST**。实测：命中率 91.67%，
  单次 0.55 µs vs 重解析 13.2 µs（24×；Debug -O0，绝对值偏大、比例有意义）。
  取证开关 `EMUERA_NO_RUNTIME_REF_CACHE=1` 旁路到旧实现（语义完全相同），
  护栏靠它证明「缓存不改语义」（13 条红全是"缓存确实生效"类）。
- **形状只剩装载期来源**：`shapeOf` 的回落从三条减到两条（声明 → VariableSize.csv），
  删掉「未声明多维容器的运行期外接几何」。那一条让 `VARSIZE` 对同一变量在「先读」与
  「先写」后给**两个答案**（答案依赖执行历史）。可达性实测：五语料 495 个「无可用声明」
  的多维下标节点**全部**是 `#DIM REF X,0,0`（占位 lengths 不可用，存储键落到调用方目标数组），
  真正落到未声明键的实例 **0**。删掉后「未声明名 + 多维写」仍能写读（取址不受影响，
  `extendExtents`/`addressOf` 不变），但 `shapeOf` 不再为它回答尺寸。
- **CHARADATA 的角色块：基址表 + 连续 arena**：`CharaArena{ QList<Cell> cells; QHash<int,qint64> bases; }`，
  取址公式仍是唯一一条 `slot = bases[charaSlot] + offset`（**未分配 = 缺键 → -1**）。
  读路径只做一次 `constFind`，**分配动作只存在于 `ensureBlock`**，读路径根本调不到它 ——
  读纯性由数据形态保证，不靠调用方纪律（变异：读路径顺手分配 → `allocatedSlots` 从 4 变 204，1 条红）。
  旧形态把「角色号 × 每角色元素数」当 stride：eraMegaten 模板号到 9002、每角色 7505 元素、
  有用叶 4.33M，**已分配 67,567,733 槽**；新形态 **14,541,824 槽（−78.5%）**，
  快照点 VmHWM **792 MB → 291 MB（−502 MB / −63%）**。端到端峰值只降 3.8%（−443 MB）：
  11.6 GB 峰值发生在之后的 ERB 装载，角色槽位只占 4%（记账，别把两个数字混用）。
  存档**不按槽位区间枚举**（按 Map 条目 + (x,y,z) 下标读），所以输出字节不变；
  arena 是整块分配，因此补了一条 `elementCount > kMaxMultiDimElements` 拒绝分配
  （语料实测最大 5100，零影响）。
- **元素形参的缺省值只有一份判据**：`hasStringDefault(decl)` = `hasDefault && type == Str`，
  三个消费者（`bindArguments` 省略分支、尾部缺省循环、`invokeUserFunction::bindDefault`）。
  旧形态是两条绑定路径各写一遍，且其中一条**没有** `Str` 分支 —— 式子调用的
  `@F(SS:0=\"x\")` 缺省值、以及式子调用传的字符串实参，全部被写进无关整数键（读到空串）。
  另一处两份都错：`&& !defaultStr.isEmpty()` 把 `@F(S=\"\")` 的空串缺省退化成整数 0，
  语句路径写成 `\"0\"`、式子路径写成 `\"\"` —— 两路互不一致。对齐 C# `ErbLoader` 的
  「缺省值必须与形参同型」，空串是合法缺省值。eraTW 的真实形状
  `@RECOVER_PERMIL(ARG,回復割合,ARGS,回復速度の影響,ARGS:1=\"\")` 已进护栏。
  **这是行为修复**：旧行为下 `ARGS:1 == \"\"` 恒假（值是 `\"0\"`）。
- **`dump_warnings` 的参数位置不再有隐含含义**：`argv[2]` 只有不以 `--` 开头时才是输出文件，
  否则报错退出（退出码 2 + 固定文本）。旧实现把 `dirname --jumpcheck` 里的开关当文件名，
  在 cwd 写出一个叫 `--jumpcheck` 的警告文件，同时开关照常生效、退出码 0 ——
  「看起来成功」的垃圾文件。未知开关、缺参数、多余位置参数同样报错。
- **音频管线：池只管状态位，播放器是 QML 的一层**：`AudioPipelinePool` 保存
  `{source, playing}` 与音量、循环次数，发 `channelPlay/channelStop/channelVolume`；
  真正出声的是 `AudioPlayers.qml` 里每条管线一个 `MediaPlayer + AudioOutput`（0 = BGM，
  1..N = SE）。契约用 `test_audio_pipeline_contracts` 钉住（不依赖声卡）：
  同一 BGM 再 `PLAYBGM` 必须**重发** `channelPlay`（对齐 EE `Sound.play()` 先 `stop()`
  再打开文件 —— 旧实现"同源早退"会让重播接着上一半播）；BGM 是 `loops=-1`，
  永远不会 `EndOfMedia`，所以坏文件只能靠 `InvalidMedia` → `stopBgm()` 回收
  （旧实现"播放中"永久亮着，SE 侧却会自动归还）。设备侧实测（60 s 1 kHz 经
  sink monitor 用 cava 采样）：全程 0 静音帧、能量落在 1 kHz 的 bar、循环点无凹陷、
  时长误差 20 ms、音量 100→50→0 单调、STOP 拆除延迟 ≈0。
  **记账（未修）**：仓库自带 `test/example/sound/se01.wav` 是 8 kHz / 50 ms，
  经 Qt FFmpeg 重采样路径**完全无声**（阈值实测：8 kHz ≤70 ms 无声、≥80 ms 有声；
  48 kHz 50 ms 正常）—— 根因在后端启动延迟，需单独调查。
- **删掉「当前帧」这个隐含输入**：存储层的每个「名字 → 存储」入口都必须说清用哪个作用域。
  无 scope 的那一族现在只剩名字里带 `*InCurrentFrame` 的一种（生产 **0 调用**，旧名字已删
  —— 任何退回隐式的调用点是**编译错误**，不是靠评审看出来）。显式 scope 重载
  （`set/getGlobalInt1D/2D/3D`、`set/getGlobalStr1D/2D`、`getStringSlot`、`setStringSlot`、
  `arraySize`/`arrayLengths`/`ensureArraySize`）没有默认参数。
  **病因证据**：把 `enterCall` 与 `bindArguments` 的顺序颠倒（测试开关
  `ScriptRunner::setTestBindBeforeEnter`，默认关）—— 改前 **5 条断言红**（元素形参
  写进调用者帧、调用者同名私有变量被污染），改后同一颠倒 **全绿**。上一刀那条
  `bindArguments` 的 `Q_ASSERT` 因此删掉：它钉的是旧假设，显式化之后顺序无关。
  扩展层没有段身份，于是 `forksupport::currentFrameScope(storage)` 是一次**显式声明**。
  副作用清理：`ExpressionEvaluator::evaluateIndexedVariable`（0 调用点、无 scope 参数）删除。
- **键不得再当名字解析（这一刀是 bug 修复，不是等价重构）**：`REF` 别名
  （`applyReferenceAlias`）把 `ctx.varName` 改写成的已经是**存储键**，但终局读把它当名字
  交给按名入口，里面 `storageName(key, m_privateScopeId)` 又解析一次 —— 目标叫 `RG`、
  本段恰好又有私有 `RG` 时，键被改道到 `段\x1fRG`。实测：`#DIM REF P` ← `RG` 后
  `P:0 == 77` 读回 **0**（字符串同病）。修法是让「谁解析过」变成数据
  （`DynamicContext::varNameIsKey`），终局读二选一：键 → `*Raw` / `StrKeyState::ResolvedKey`，
  名字 → 按名入口；`resolveStrLocus` 的 `ResolvedKey` 顺带认出「键本身就是角色变量名」
  （`#DIMS REF PS` ← `CSTR`），保住角色字符串路径。读写不对称作证：写路径一直是对的。
  另外把 (a) 类「隐式当前帧写入今天偶然正确」的顺序前提从注释变成 `Q_ASSERT`
  （`bindArguments` 里校验当前帧 == 被调段），而不是把 26 个调用点改成显式 scope。
- **字符串侧是同一个病的第二份**：`getStringSlot` / `setStringSlot` / `getStringSlotRaw` 各自
  手写过 `LOCALS → ARGS → SAVEDATA_TEXT → 角色 → 扁平槽`；读写必须永远同步，加一类忘一边
  编译期不报（症状：写得进读不出，或读到的其实是另一格）。现在同一形态 ——
  `resolveStrLocus` 一份七步行序，`StrLocus` 是数据，读 `readStrLocus` / 写 `setStringSlot`
  各一个穷尽 switch。「键已解析」不再是一份代码，而是参数 `StrKeyState::ResolvedKey`
  （Raw 入口绝不二次 `storageName`，否则私有名 / REF 别名会被按 scope 改道）。
  读/写的**不对称是 Era 语义**、由 `test_string_locus` 47 条断言钉住而非消掉：
  读越界给空且不扩容，写 `LOCALS/ARGS` 懒扩容，`setStringSlot` 的 bool 是「下标非负」
  而不是「写成功」（越界写 qWarning 后忽略）。
- `writeLoopCounter` 写的是「计数器固定下标上的循环值」——值是值，不是下标，因此没有「循环值越界」检查。
- **收尾 pass 表**：`m_loadPasses[7] = { label, run, origin, gate, phaseAfter }`，表即流程
  （顺序 = 执行顺序，日志文案也取自表）。`run` 的类型是 `PassAction`
  （内部一个 `std::function<void(LoadContext&, ParseDiagnostics&)>`，**无默认构造 + 拒绝 `nullptr_t`**）
  —— 所以「两个都空 / 两个都非空 / 谁是哪一个」在类型上不存在，驱动只有 `run(ctx, sink)` 一种写法；
  旧实现是两个字段（自由函数指针 + 成员函数指针）+ 一句「恰有一个非空」的注释 + 一处 `if` 分派。
  1/2/3 装 loadpass 自由函数，4/5/6/7 装只捕获 `this` 的 lambda（转调 const `run*`）；表因此是
  **实例成员**（成员步骤要绑 `this`；`EraParseTable` 是 `QObject`，不可拷贝，无悬空风险）。
  只读保证仍是编译期：lambda 第一参数无名 → 拿不到可写 `LoadContext`，`run*` 是 const，
  `makeIrView()` 只给 const 指针。`origin` 是「这一步拥有哪个诊断切片」，`gate` 是「受哪个开关门控」
  （`Always` / `NotCalledWarning`），`phaseAfter` 让阶段推进也是数据而不是分支。
  4/5/6(+7) 是只读步骤，可脱离 `markLoadComplete` 单独调用。
- **一次失败只留一条消息**：具体错误经 `ScriptRunner::reportError()` 发出并推进错误纪元；
  `stepOnce` 在执行前取快照，纪元未变才发兜底的 `execution error`。用计数而不是每步清零的
  bool —— 式中调用会递归进 `stepOnce`，清零会抹掉外层的「已报过」。静默路径仍有兜底。
- **配对语法只判一次**：`jumpmark::buildJumpMarkings` 一趟扫行同时产出运行期跳转表与
  `ScriptData::structureIssues`（`StructureIssueKind` 17 个值，一个 kind 一个消息模板）；
  5/6 的 `checkStructuresInScript` 只把记录翻成诊断，**不再有第二个 nestStack**。
  两份投影的差异显式成字段而不是第二份实现：`staticVisible`（运行期看得见、静态看不见，
  体内非法指令那一行）、`jumpPaired`（运行期已消费、静态仍不弹）。原先两遍判定已经互相
  矛盾——交叉嵌套 `IF/REPEAT/ENDIF/REND` 运行期两对都配上、静态报错并留下来闭合；
  体内非法指令运行期照旧配对、静态跳过该行。哨兵值也一并消失：`jumpTo` 只含已解析目标，
  `-1` 有名字（`kNoJumpTarget`），`SIF` 跳 `lines.size()` 写明是合法位置；
  `test_structure_pairing` 用 31 个夹具 / 24 组逐字诊断把每个异常形态钉住。
- **越界判定只有一份，谁报什么是一张表**：`GameData/ast/index_bounds.{h,cpp}` 提供
  唯一判定（维数/越界/常量下标/常量除零）与唯一文案；`issuePolicy(Kind)` 决定
  `severity / effect / producer`。八类事实各有唯一生产者：`TooManyIndices`、
  `DimensionMismatch`、`DeclaredLiteralOutOfRange`、`ConstantDivisionByZero` → 3/6 且
  `RejectLine`；`NegativeConstantIndex`、`SystemSizeExceeded`、`UnknownStringIndex`、
  `DynamicDivisionByZero` → 6/6 且 `ReportOnly`；`ConstantIndexInvalid` → 3/6 且
  `RejectLine`。6/6 对 `line.isError` 的行整体让位（它是 3/6 的残余）。
  「是不是常量、值是多少」也不是模式匹配，而是装载期算出来的一个字段：
  `ExpressionNode::constantIndex`（Unknown / Value / Invalid），在
  `VariableTable::applyTypes` 一趟定型时写入（scope 是行所属函数，显式传入），
  判定只读它。五语料约 2.7 万处常量表达式下标（`ARR:(10+0)`、`ARR:PL`）因此进入判定。此前两侧已矛盾：`PRINTV 10 / 0` 同一行既 RejectLine 又 ReportOnly。
- **容器只有一种形态**：一个存储键 = 一条扁平槽位序列，维数不决定容器、只决定取址
  （`addressOf`：`offset = Σ idx[d]·strides[d]`；CHARADATA 在 `CharaArena::slotOf`：
  `slot = bases[charaSlot] + offset`）。1D 是「无几何」的特例；多维装载期按
  `lengths` 乘积真实分配（上限 `4'000'000`，语料实测最大 1e6），越界读 0、写 `qWarning`
  后忽略。几何缓存唯一写入点 `installGeometry`，形状仍只由 `shapeOf` 回答。
  旧形态是 1D/2D/3D 三套平行容器（各有取址与懒扩容规则）+ CHARADATA 每角色嵌套
  `QList`；收敛后 1D 槽位浪费率 14.5–39.6% → 1.0–2.0%，但 CHARADATA 的
  「角色号 × 每角色元素数」在模板号稀疏时撑大槽位（eraMegaten 4.33M 有用叶 →
  65.3M 槽，VmHWM +5.2%），这是本形态的直接代价、已记账。
- **形状只有一份、一个入口**：`VariableStorage::shapeOf(name, scope)` 返回
  `{ lengths, elementCount, lengthOf(d) }`，回落顺序**只在那里**：声明（`VariableDecl::lengths`
  / `elementCount`，含 CHARADATA 与 REF 目标）→ 无声明时 `VariableConfig`（`VariableSize.csv`，
  装载期尺寸表）→ 未声明的运行期多维容器（外接几何；1D 槽位数**不是**形状）。
  `VARSIZE` 函数、`VARSIZE` 语句（逐维写 `RESULT`）、`arraySize` / `arrayLengths`、
  多维下标翻译与越界判定全部读它，不再各自回落。容器当前分配量叫 `allocatedSlots`
  —— 它是容器事实，旧名 `arraySizeRaw` 会被误当成形状读者（那条回落链曾让
  `VARSIZE(TA)`（3D）恒 0、`VARSIZE(U1,3)` 回落成首维、CHARADATA 多维恒 0，
  `arraySize` 还按「当前帧」解析而与 `VARSIZE` 不一致）。
- **装载阶段是显式状态机**：`LoadPhase { Empty, Loading, Bound, Finalized }`。每个公开入口声明它要求的
  阶段；越界调用 = `qWarning` + no-op（不 assert —— release 下也必须安全，且诊断容器不变）。
- **诊断按 pass 切片，重跑幂等**：`DiagOrigin { Load, Pass1..Pass7 }`。每一步建独立 sink，
  驱动**运行与否**都 `replaceOrigin(pass.origin, sink)`（gate 跳过 = 切片置空，Pass7 不再残留
  上一次的 `kNotCalled`）。盖章只在 `replaceOrigin` 一处，各 `add()` 调用点不传 origin。
  因此跑一次与跑三次的 `all()` 逐字段相同：`readOnlyPassRunCount` 与 `clearDiagnostics()` 都删掉了
  ——幂等由数据保证，不由调用方纪律保证。`Load` 切片是装载期逐行事实（`kSharpLine` / `kDeclError` /
  `kUnknownInstruction`），它不重跑；同名脚本被替换时由 `removeOriginFiles` 精确删掉旧 AST 的那部分。
- **增量装载是显式协议**：`Finalized` 之后直接 `loadScript` = `qWarning` + `return false`；
  要追加必须先 `beginIncrementalLoad()`（Finalized → Loading），装完再 `markLoadComplete()`。
  「重收尾」与「一次性装载」逐字段相同（`test_load_contracts` 4b 钉住）。
- **私有名表在装载期建立**：`VariableTable::buildPrivateNameTable()` 由声明（`scope == Local`）一次建好，
  `finalizeBindings()` 末尾 `installPrivateScopeTable()` 装进存储层。安装后 `setPrivateScope` 只写
  当前帧（无 insert / reserve / 循环），正确性不再依赖调用方把 `names` 传全。
- **收尾分三层，输入类型不同**：`parsecheck` 收全 `const` 的 `ParseIrView`（4/5/6，输出只有诊断）；
  `loadpass` 收可写的 `LoadContext`（1/2/3，纯数据视图，11 个字段：scripts / variables / functions /
  constantTable / storage / evaluator / gameBaseData / labelIndex / paramNames / 两个 bool 引用）；
  它**没有** `diagnostics` 字段（诊断是每步一个 sink），也**没有**运行期位置（作用域是 `evaluate()`
  的参数——曾经靠「解释器在哪一行」反推私有常数作用域，装载期会解析到错误的函数）；
  `EraParseTable` 只剩「解析入口 + pass 表 + 阶段机 + 位置区壳子 + getter」，1/2/3 在类内已无成员入口。
  `LoadContext` 不是拥有者：被引用的表仍由解析表持有；文件里 `rg EraParseTable` = 0。
- **「裸标签属于哪个脚本」只有一张索引**：`m_labelIndex : QHash<QString, QList<LabelRef>>`，
  键是大写折叠的标签名，值是**全部声明**的全序列表（脚本名 `QString::compare` 升序 → 同脚本内行号升序，
  `labelRefLess`）。追加点唯一（`loadScript` 的标签登记），固化点唯一（1/6 `finalizeLabelIndex`，
  只 `sort` 不去重），**择一规则唯一**（`position::firstLabel` 取全序第一条，`callableOnly` / `onlyScript`
  只是过滤）。CALL / GOTO / `getLabelPosition` / `hasLabel` 因此是同一个答案。
  曾有**四张**表（索引、`ScriptData::labelPositions` last-wins、`ErbLoader::m_labelPositions`
  的 `"script:label"` 拼接串且大小写敏感、`ErbLoader::m_labels` 的裸名 last-wins）能对
  同一个名字给出不同行号——后三张已全部删除。
  每条 `LabelRef` 还带 `functionLine`（所属函数段的起点行，文件头段为 `-1`）：
  GOTO 的「`$` 只在本函数内」因此是一次**字段比较**（`firstGotoLabel`），
  不是运行期从函数头扫行。段身份由调用方给：有帧 = 帧的 `entryLine`（所以
  `callLabelAt` 进入同名函数的第二份声明时也只命中正在执行的那一份），
  无帧 = `executionScope()` 回查。eraTW 实测：`ownerFunction` 与「最近前置 `@`」
  2,111,136 行 0 处不一致，833 个 `$` 标签的 `functionLine` 0 处不一致。
  静态检查（4/6）也调**同一个** `position::firstGotoLabel`：曾按 `ownerFunction` 名字
  分组，同名 `@F` 声明两次时第二份的 `$X` 会让第一份的 `GOTO X` 静默，运行期却报错。
- **常数解析的作用域是参数**：`setConstProvider` / `setConstArrayChecker` / `setConstArraySizeProvider` /
  `setConstArrayProvider` 四个回调都收 `eravar::ScopeId`（`evaluate()` 的第四参数原样转发），
  回调不得自己推导作用域。装载期因此按「这行所属函数」解析私有常数，而不是按「解释器停在哪一行」。
- **装载之外的两块也各自只有一份实现**：
  * **标记区**（`jump_markings.{h,cpp}`，`namespace jumpmark`）：`buildJumpMarkings(ScriptData&)`
    是唯一的构建点（配对索引 + `lines[i].jumpTo`；`endifLines` / `elseLines` / `jumpToEnd`
    这三份整表副本与 `ScriptData::jumpTo` 这张 QHash 已删除，需要时由行字段精确重建），读侧 6 个
    `inline` 自由函数只吃 `const ScriptData*`（nullptr 安全）。`EraParseTable` 里
    这 7 个方法已删除、不留 forwarder。
  * **位置区**（`position_state.{h,cpp}`，`namespace position`）：状态是纯数据
    `PositionState`（`script` / `line` / `callStack` / `jumped`；栈深没有字段、
    `depth()` 返回 `callStack.size()`，作用域也没有字段、`executionScopeId()`
    每次按当前行 `scopeKey` 现算 —— 于是 `m_position` 不是 `mutable`，
    const 方法改不了位置），
    20 个自由函数做转移，只置 `PositionEffects` 的位（`callStackChanged` /
    `memorySpaceChanged` / `positionChanged` / `jumpRequested`）；`EraParseTable` 的 11 个
    方法是「打包 `ScriptView` → 调自由函数 → `emitEffects`」。信号顺序写死在
    `emitEffects`（规范序 `callStack → space → position → jump`），`clear()` 是唯一
    反序例外（`positionChanged` 先、`callStackChanged` 后），两者都由
    `test_position_state` 录信号断言。文件里 `rg EraParseTable|m_[a-z]` = 0。
- **收尾并行由数据形状决定**：`2/6 finalizeBindings` 的三个写阶段、`3/6 finalizeValidation`
  与 `4/5/6` 三个只读检查，全都是「逐脚本独立、只写本脚本的行或本任务的诊断」，
  驱动统一为（`loadpass::runPerScript` / `parsecheck::runPerScript` 同形）：脚本名排序 →
  每任务独占 `ParseDiagnostics` → `QtConcurrent::blockingMapped` → 按名字序归并
  （脚本 < 4 或单线程时串行直跑）。归并序只由输入决定，与装载序 / 哈希种子 / 线程完成序
  无关；`sortForOutput()` 仍是同排序键重复项的最后兜底。两处「有脚本内顺序依赖」的地方
  合成同一任务：3/6 的逐行校验 + `validateArguments`、2/6 的字符串赋值里的 FORM 重编译。
  实测 eraTW（2258 脚本，12 线程）：`4/5/6` 10.5s → 1.9s、`3/6` 9.1s → 1.7s、
  `2/6` 4.3s → 2.0s、`markLoadComplete` 23.3s → 6.5s；未排序输出与基线逐字节相同。
- **存档有独立的键映射层**：`erasave::Map`（`save_variable_map.{h,cpp}`）是「存档条目 ↔ 变量身份」的
  唯一一张表，`Entry = { name, key, slot, dimensions, user, persisted }`。文件里写的是**裸名**，
  身份不进文件；存档层不再认识 `FUNC\x1fNAME` 这种键形态，也不必改那 31 个字符串键容器。
  前置条件由 `SaveReadiness`（`Ready` / `NoDeclarationTable` / `DeclarationsNotResolved`）显式回答。
- **诊断顺序可复现**：装载并行 + `QHash` 随机化哈希种子会让诊断的*产生顺序*在进程间漂移
  （条数与内容不变）。收尾末尾 `ParseDiagnostics::sortForOutput()` 按 `(文件, 行, 列, code, message)`
  稳定排序一次，文本视图同步重建 —— `dump_warnings` 的基线比对才立得住。
- **诊断两种后果**：`DiagEffect` 与 `DiagSeverity` 正交。`RejectLine`（该行失效）当前只有 6 处，
  全在 3/6 `finalizeValidation`；`checkStaticValues` 全部 `ReportOnly` —— 静态检查不允许改变运行行为，
  这条不变式由字段表达，不再只靠注释。

## 回归基线

`dump_warnings` 五语料（`eraTW` / `eraMegaten` / `eraTetris` / `FL3.8` / `ecd`）：
**6 / 2 / 0 / 0 / 0** 行（`eraMegaten` 的目录参数必须是 `eraMegaten/data`；用错路径会得到
0 字节的假基线），与 `/tmp/uml-baseline/*.raw.txt` **未排序**逐字节相同。
累计五十九个重构（身份整数化 / 全定型 / 作用域显式化 / 只读收尾外移 / 存档独立映射 /
私有名表前移 / 存档顺序确定性 / 写收尾外移 / 标记区外移 / 位置区外移 /
标签归属单一索引 / 诊断切片幂等 / 常数作用域参数化 / 增量装载协议显式化 /
GOTO 段身份入索引 / 静态检查用段身份 / 删第三张裸名表 / 错误去重 / 配对语法只判一次 /
越界判定只判一次 / 常量下标装载期求值 / 多维独立形态 / 私有作用域按段身份 / 存档版本常数化 / LSP 侧段身份 / 函数表按脚本换贡献 / 删装载器第二份脚本清单 / CALL 入口吃段身份原文 / 删标记区三张整表副本 / 删 ScriptData::jumpTo /
 数组形状单一入口 / 收尾 pass 表单一形态 / 删 PositionState::depth 副本 / 删只写不读的 scopeId 缓存 /
 容器形态单一化（一个键一条扁平槽位序列）/ 条件范围装载期定完（conditionNeedsScope）/
 按名原语的解析顺序单一化（IntLocus + 两个穷尽 switch）/
 字符串侧同一形态（StrLocus + StrKeyState，同一份七步行序）/
 **键不得再当名字解析**（varNameIsKey → *Raw / ResolvedKey）/
 删掉「当前帧」这个隐含输入（显式 scope 重载 + *InCurrentFrame 生产 0 调用）/
 音频管线契约（同曲重播从头 / 坏媒体回收）/
 元素形参两条绑定路径同构（hasStringDefault）/
 角色基址表 + arena / loopEndLines 立为唯一反查索引 / 形状只剩装载期来源 /
 GETVAR 族名字解析缓存 /
 赋值左值构造期折完常量 /
 消灭「构建后再重建」 /
 常量子树不求值第二次（constantValueAt 唯一入口）/
 声明前置：删掉「待收尾定型」第三态（rhsPending）/
 一个脚本只扫描一次（Scan 随数据传递）/
 文件字节只读一次盘（内容随数据走）/
 文本只预处理一次（BuiltAst 交回 warnings）/
 预处理产物共用（一份正文 + 12 B/行跨度）/
 CSV 字节只读一次（CsvTextTable，两个消费者共用字节不共用解析）/
 异步完成信号单一发布点（completeAsyncLoad + 零工作量 = 显式成功））
 前后完全一致；
`ctest` 48 → 71（新增 `test_parse_checks` / `test_save_variable_map` / `test_private_scope_table` /
`test_load_contracts` / `test_save_variable_map_hashseed` / `test_jump_markings` /
`test_position_state` / `test_goto_label_lookup` / `test_structure_pairing` / `test_index_bounds` /
`test_segment_scope` / `test_array_shape` / `test_storage_form` / `test_condition_scope` /
`test_name_locus` / `test_string_locus` / `test_scope_plumbing` / `test_audio_pipeline_contracts` / `test_bind_defaults` / `test_dump_warnings_args` / `test_chara_arena` / `test_runtime_ref_cache` / `test_async_empty_dir`）。

装载耗时（eraTW，2260 文件 / 2.11M 逻辑行，12 线程）：`load` 41 s → **22.8 s**
（scan 129 ms / parse 8.4 s / merge 4.9 s），收尾 `markLoadComplete` 23.3 s → 6.5 s，
其中 4/5/6 10.5 s → 1.9 s、3/6 9.1 s → 1.7 s、2/6 4.3 s → 2.0 s；峰值 RSS 3.42 → 3.03 GB。

存档字节：`erasave::Map` 在 `Map::build` 末尾把条目排成**只由数据决定的全序**（主键 `slot`
→ 次键大写名 → 内建先于用户 → 私有作用域名），存档字节与 `QT_HASH_SEED` 无关 ——
测试不再固定播种（另有 `test_save_variable_map_hashseed` 用 `QT_HASH_SEED=42` 直接证明），
旧 golden 降级为「解码 + 回放」fixture（按名读，天然与写出顺序无关），新 golden 从
排序后实现生成（`normal-new.bin` md5 `d3b24b45…`，再生成开关 `EMUERA_SAVE_MAP_REGEN_GOLDEN=1`，
改动理由写在测试头注释）。容器版本是**一个**常量 `EraBinaryData::Version`（1809）：
二进制头的版本字段、`decode()` 的校验、文本起始标记 `__EMUERA_<Version>_STRAT__` 都由它派生
—— 改它就是主动判废所有既有存档，解码侧因此**没有**多版本分支、也没有 extension 计数/标记字。
旧档在读入处**原子判废**（「存档格式版本不兼容：本版本 N，旧存档（版本 M）请删除」，失败不触碰目标）。
本轮 golden 的字节差异只有两类原因：版本字段、私有条目写盘名（`裸名` → `段身份文本 + 0x1F + 名字`）。