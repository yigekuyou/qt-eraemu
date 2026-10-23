# emuera UML（当前实现）

PlantUML 源码。所有图都是**当前代码**的静态结构，不包含计划中的设计。
渲染产物（`.png` / `.svg`）是构建输出，`.gitignore` 里已排除，不入库。

| 文件 | 类型 | 内容 |
| --- | --- | --- |
| `00-overview.puml` | 组件图 | 分层总览：装载期一次定型 → 运行期只做存取；收尾三层（`parsecheck` 只读 / `loadpass` 可写 / 类只剩编排）；标记区 `jumpmark` 与位置区 `position` 各只有一份实现 |
| `01-load-classes.puml` | 类图 | 装载期：`EraEngine` / `ErbLoader` / `AstBuilder` / `EraParseTable` / `ParseIrView` 与 4/5/6 自由函数 / `LoadContext` 与 1/2/3 自由函数 / 标记区 `jumpmark` / 标签索引 `m_labelIndex` / 位置区 `PositionState`+`PositionEffects`+`position` 自由函数 / `LoadPhase` 阶段机 / 带 `origin` 切片的 pass 表 |
| `02-runtime-classes.puml` | 类图 | 运行期：`ExecutionEngine` / `ScriptRunner` 单步状态机 / `ProcessState` / `LoopFrame`（作用是显式 `ScopeId`；位置推进经 `position` 自由函数） |
| `03-expression-classes.puml` | 类图 | 表达式 AST、`AccessKind` 16 值、`VariableKey{scopeId,nameId}`、名字 intern 表、`resolverTable` |
| `04-load-pipeline.puml` | 时序图 | 装载流水线：CSV → 逐文件解析 → `m_loadPasses[7]` 表驱动收尾（每步独立 sink + `replaceOrigin`）→ 诊断 |
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
  （`addressOf`：`offset = Σ idx[d]·strides[d]`；CHARADATA 在 `roleSlotOf`：
  `slot = charaSlot·elementCount + offset`）。1D 是「无几何」的特例；多维装载期按
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
累计三十九个重构（身份整数化 / 全定型 / 作用域显式化 / 只读收尾外移 / 存档独立映射 /
私有名表前移 / 存档顺序确定性 / 写收尾外移 / 标记区外移 / 位置区外移 /
标签归属单一索引 / 诊断切片幂等 / 常数作用域参数化 / 增量装载协议显式化 /
GOTO 段身份入索引 / 静态检查用段身份 / 删第三张裸名表 / 错误去重 / 配对语法只判一次 /
越界判定只判一次 / 常量下标装载期求值 / 多维独立形态 / 私有作用域按段身份 / 存档版本常数化 / LSP 侧段身份 / 函数表按脚本换贡献 / 删装载器第二份脚本清单 / CALL 入口吃段身份原文 / 删标记区三张整表副本 / 删 ScriptData::jumpTo /
 数组形状单一入口 / 收尾 pass 表单一形态 / 删 PositionState::depth 副本 / 删只写不读的 scopeId 缓存 /
 容器形态单一化（一个键一条扁平槽位序列）/ 条件范围装载期定完（conditionNeedsScope）/
 按名原语的解析顺序单一化（IntLocus + 两个穷尽 switch）/
 字符串侧同一形态（StrLocus + StrKeyState，同一份七步行序）/
 **键不得再当名字解析**（varNameIsKey → *Raw / ResolvedKey））前后完全一致；
`ctest` 48 → 65（新增 `test_parse_checks` / `test_save_variable_map` / `test_private_scope_table` /
`test_load_contracts` / `test_save_variable_map_hashseed` / `test_jump_markings` /
`test_position_state` / `test_goto_label_lookup` / `test_structure_pairing` / `test_index_bounds` /
`test_segment_scope` / `test_array_shape` / `test_storage_form` / `test_condition_scope` /
`test_name_locus` / `test_string_locus` / `test_scope_plumbing`）。

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