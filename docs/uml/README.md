# emuera UML（当前实现）

PlantUML 源码。所有图都是**当前代码**的静态结构，不包含计划中的设计。
渲染产物（`.png` / `.svg`）是构建输出，`.gitignore` 里已排除，不入库。

| 文件 | 类型 | 内容 |
| --- | --- | --- |
| `00-overview.puml` | 组件图 | 分层总览：装载期一次定型 → 运行期只做存取；收尾三层（`parsecheck` 只读 / `loadpass` 可写 / 类只剩编排）；标记区 `jumpmark` 与位置区 `position` 各只有一份实现 |
| `01-load-classes.puml` | 类图 | 装载期：`EraEngine` / `ErbLoader` / `AstBuilder` / `EraParseTable` / `ParseIrView` 与 4/5/6 自由函数 / `LoadContext` 与 1/2/3 自由函数 / 标记区 `jumpmark` / 标签索引 `m_labelIndex` / 位置区 `PositionState`+`PositionEffects`+`position` 自由函数 / `LoadPhase` 阶段机 / 带 `origin` 切片的 pass 表 |
| `02-runtime-classes.puml` | 类图 | 运行期：`ExecutionEngine` / `ScriptRunner` 单步状态机 / `ProcessState` / `LoopFrame`（作用是显式 `ScopeId`；位置推进经 `position` 自由函数） |
| `03-expression-classes.puml` | 类图 | 表达式 AST、`AccessKind` 16 值、`VariableKey{scopeId,nameId}`、名字 intern 表、`resolverTable` |
| `04-load-pipeline.puml` | 时序图 | 装载流水线：CSV → 逐文件解析 → `kLoadPasses[7]` 表驱动收尾（每步独立 sink + `replaceOrigin`）→ 诊断 |
| `05-variable-access.puml` | 活动图 | 运行期变量访问：两条入口 → 一张 resolver 表 → 唯一按名原语 |
| `06-diagnostics.puml` | 类图 | 诊断模型：`ParseDiagnostics` / `ParseDiagnostic` / `DiagEffect` / `DiagOrigin`（pass 切片）/ `DiagCode` 全码与产生步骤 |
| `07-static-checks.puml` | 卡片图 | `checkStaticValues` 检查目录（C1..C4）与已验证口径 |
| `08-save-load.puml` | 时序图 | 存档 / 读档：`erasave::Map`（条目 ↔ 身份的唯一一张表）、`SaveReadiness` 前置条件 |

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
  上一版把私有名编成 `函数名 + 0x1F + 名字`，而 `computeVariableAccess` 与 `setPrivateScope`
  各拼一次、必须逐字节一致 —— 任何拼写/分隔符疏漏都表现为「某个变量悄悄变成另一个变量」。
  现在这个拼接只在 `makePrivateStorageKey` 一处，比较是 `quint32` 相等。
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
- **单一按名原语**：`VariableStorage::readIntByName` / `writeIntByName(name, indices, scope)` 是
  「名字 + 下标 → 存储」的唯一实现（顺序：形参 → 角色 → ARG/LOCAL → ARGS/LOCALS/RESULTS →
  系统变量 → 2D/3D → 1D）。存储容器仍是字符串键（存档格式不变），id → 串只在 `storageKeyFor` 一处。
- `writeLoopCounter` 写的是「计数器固定下标上的循环值」——值是值，不是下标，因此没有「循环值越界」检查。
- **收尾 pass 表**：`kLoadPasses[7] = { label, fn, memberFn, origin, gate, phaseAfter }`，表即流程
  （顺序 = 执行顺序，日志文案也取自表）。七步的 `fn` 签名统一为 `(LoadContext&, ParseDiagnostics&)`
  ——1/2/3 是 loadpass 的自由函数，4/5/6/7 是成员 `run*`（转调 parsecheck）；`memberFn` 为空的表项
  由驱动直接调 `fn`。`origin` 是「这一步拥有哪个诊断切片」，`gate` 是「受哪个开关门控」
  （`Always` / `NotCalledWarning`），`phaseAfter` 让阶段推进也是数据而不是分支。
  4/5/6(+7) 是只读步骤，可脱离 `markLoadComplete` 单独调用。
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
  曾有三张表（索引、`ScriptData::labelPositions` last-wins、`ErbLoader::m_labelPositions`
  的 `"script:label"` 拼接串且大小写敏感）能对同一个名字给出三个行号——后两张已删除。
  每条 `LabelRef` 还带 `functionLine`（所属函数段的起点行，文件头段为 `-1`）：
  GOTO 的「`$` 只在本函数内」因此是一次**字段比较**（`firstGotoLabel`），
  不是运行期从函数头扫行。段身份由调用方给：有帧 = 帧的 `entryLine`（所以
  `callLabelAt` 进入同名函数的第二份声明时也只命中正在执行的那一份），
  无帧 = `executionScope()` 回查。eraTW 实测：`ownerFunction` 与「最近前置 `@`」
  2,111,136 行 0 处不一致，833 个 `$` 标签的 `functionLine` 0 处不一致。
- **常数解析的作用域是参数**：`setConstProvider` / `setConstArrayChecker` / `setConstArraySizeProvider` /
  `setConstArrayProvider` 四个回调都收 `eravar::ScopeId`（`evaluate()` 的第四参数原样转发），
  回调不得自己推导作用域。装载期因此按「这行所属函数」解析私有常数，而不是按「解释器停在哪一行」。
- **装载之外的两块也各自只有一份实现**：
  * **标记区**（`jump_markings.{h,cpp}`，`namespace jumpmark`）：`buildJumpMarkings(ScriptData&)`
    是唯一的构建点（10 张行号索引表 + `lines[i].lineIndex/nextLine`），读侧 6 个
    `inline` 自由函数只吃 `const ScriptData*`（nullptr 安全）。`EraParseTable` 里
    这 7 个方法已删除、不留 forwarder。
  * **位置区**（`position_state.{h,cpp}`，`namespace position`）：状态是纯数据
    `PositionState`（`script` / `line` / `callStack` / `depth` / `jumped` / 作用域 id 缓存），
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
累计十五个重构（身份整数化 / 全定型 / 作用域显式化 / 只读收尾外移 / 存档独立映射 /
私有名表前移 / 存档顺序确定性 / 写收尾外移 / 标记区外移 / 位置区外移 /
标签归属单一索引 / 诊断切片幂等 / 常数作用域参数化 / 增量装载协议显式化 /
GOTO 段身份入索引）前后完全一致；
`ctest` 48 → 56（新增 `test_parse_checks` / `test_save_variable_map` / `test_private_scope_table` /
`test_load_contracts` / `test_save_variable_map_hashseed` / `test_jump_markings` /
`test_position_state` / `test_goto_label_lookup`）。

装载耗时（eraTW，2260 文件 / 2.11M 逻辑行，12 线程）：`load` 41 s → **22.8 s**
（scan 129 ms / parse 8.4 s / merge 4.9 s），收尾 `markLoadComplete` 23.3 s → 6.5 s，
其中 4/5/6 10.5 s → 1.9 s、3/6 9.1 s → 1.7 s、2/6 4.3 s → 2.0 s；峰值 RSS 3.42 → 3.03 GB。

存档字节：`erasave::Map` 在 `Map::build` 末尾把条目排成**只由数据决定的全序**（主键 `slot`
→ 次键大写名 → 内建先于用户 → 私有作用域名），存档字节与 `QT_HASH_SEED` 无关 ——
测试不再固定播种（另有 `test_save_variable_map_hashseed` 用 `QT_HASH_SEED=42` 直接证明），
旧 golden 降级为「解码 + 回放」fixture（按名读，天然与写出顺序无关），新 golden 从
排序后实现生成（`normal-new.bin` md5 `995642c9…`，再生成开关见测试头注释）。