> 本文是冒烟夹具的工作指南；下述输入/输出路径均相对仓库根。其历史 skip 决策不能作为 C++ 当前实现状态的证明。

# 冒烟清单校正指南（依据 test/data 的语义文档）

## 任务
校正 `/mnt/DATA/github/emuera/test/data/doc_smoke.tsv` 中你负责的条目（清单见 `smokeNN.txt`），
产出**修正后的 TSV 行**写到 `_smoke_fix/smokeNN.out.tsv`。这是给 `test/example/` 的
「文档语义冒烟组」用的调用清单。

## 禁止读取的范围（重要，勿违反）

判断依据**只有** `test/data/**/*.md` 与 `test/example/**`。以下目录一律**不要读、不要引用**：

| 目录 | 原因 |
|---|---|
| `Emuera/**`、`emuera.em/**`、`eraTW/**` 等 C# 源码树 | 第三方源码；语义已抽取进 `test/data` 的 md，无需再读 |
| `build/`、`build-debug/` | 构建产物 |

理由：`test/data` 的文档已写清每条命令/函数的签名、语义、示例、副作用与**实现现状**（哪些 C# 参考树无实现、
哪些注册被注释），足够完成任务。读源码既无必要，还会因多棵树版本不同（EMv17/EEv41 与 EMv18/EEv56 等）
相互矛盾，越读越乱。

## 输入
- 当前草稿行：`test/data/doc_smoke.tsv`，列：`name  kind  mode  snippet  note`
- 每条命令/函数的文档：`test/data/commands/<NAME>.md`、`test/data/functions/<NAME>.md`
  （看「语义」「用法」「备注」三节：签名、示例、可用上下文、副作用、错误行为）
- example 运行环境（判断标识符是否可用）：
  - `test/example/CSV/Chara/Chara0.csv` —— 只有 **番号 0 一个角色定义**（`ADDCHARA 0` 合法，`ADDCHARA 10` 不合法）
  - `test/example/CSV/{ABL,BASE,DAY,Str,StrName}.csv` 存在
  - `test/example/ERB/TEST_HEADER.ERH` 定义的广域变量（`DOC_WIDE_27`、`DOC_WIDE_S27`、`GLOBAL DOC_GVAR_27`、`WEBP_CM`）
  - 其它组的写法可参考：`test/example/ERB/0X_*.ERB`、`2X_DOC_*.ERB`（它们如何准备变量与角色）

## 输出
`_smoke_fix/smokeNN.out.tsv`，每行 5 列（制表符分隔），与你分到的名字一一对应（顺序可不同，名字必须齐）：

| 列 | 取值 |
|---|---|
| name | 名字 |
| kind | `func` / `cmd`（照抄草稿） |
| mode | `call` = 可安全直接执行；`skip` = 不宜执行，note 里给具体原因 |
| snippet | mode=call 时必填：**可执行**的一行或多行 ERB（多行用 `\n` 两个字符分隔）。函数写成 `CVTMP = NAME(...)` 或 `CVTMP_S = NAME(...)`（返回字符串时） |
| note | mode=skip 的原因（具体、可读，如「分支关键字，需嵌入结构，由组 3/28 覆盖」「会写存档」「需要交互输入」） |

## 判定规则
1. **能真实执行的才给 call**：
   - 参数用文档签名/示例的形态，标识符必须在 example 里存在（角色用 0；A~Z、`FLAG`、`STR`、`CVTMP`/`CVTMP_S` 可用）。
   - 需要前置状态的，把前置写在同一条 snippet 里（用 `\n` 分隔），例如先 `GCREATE` 再 `GSAVE`。
   - **不得**包含：等待输入、结束/重启程序、写盘/改存档、播放音频、依赖 GUI 控件、可能无限循环。
2. **下列情形一律 skip**（note 写清）：
   - 分支/流程关键字：`IF/SIF/ELSE/ELSEIF/ENDIF/SELECTCASE/CASE/CASEELSE/ENDSELECT/BREAK/CONTINUE/FOR/NEXT/WHILE/WEND/DO/LOOP/REPEAT/REND/BEGIN/ENDCALL/FUNC/ENDFUNC/CATCH/ENDCATCH/DATA/DATAFORM/DATALIST/ENDLIST/ENDDATA/…` —— 它们是结构的一部分，不能单独作为语句执行；注明「由组 3/4/5/28 覆盖」。
   - 会改变全局流程或破坏环境的：`BEGIN/RESTART/QUIT/FORCE_*/SAVEDATA/LOADDATA/DELDATA/SAVEGAME/LOADGAME/SAVEGLOBAL/LOADGLOBAL/SAVECHARA/LOADCHARA/DOTRAIN/CALLTRAIN/STOPCALLTRAIN/…`
   - 需要交互：`INPUT/INPUTS/ONEINPUT(S)/TINPUT(S)/TONEINPUT(S)/WAIT/WAITANYKEY/FORCEWAIT/AWAIT/TWAIT/INPUTMOUSEKEY/BINPUT(S)/ONEBINPUT(S)/FLOWINPUT/INPUTANY/…`
   - 需要资源或后端：音频（`PLAYBGM/PLAYSOUND/…`）、GUI（`TOOLTIP_*/SETTEXTBOX/GETTEXTBOX/HTML_PRINT_ISLAND*/…`）、图像文件（`GLOAD/GCREATEFROMFILE/SPRITEDISPOSEALL/…` 视文档而定）
   - C# 参考树无实现（不代表 C++ 移植状态；文档「备注」写明的）：`COLUMN*`、`LCSVISASSI`、`OCLEARLINE`、`CHKVARDATA`、`CHKGLOBALDATA`、`FIND_VARDATA`、`REF`、`REFBYNAME`、`SAVEVAR`、`LOADVAR`、`GETTEXTSIZE` 等。
3. **函数尽量给 call**：返回 `str` 的用 `CVTMP_S = NAME(...)`；参数个数按签名（可选参数可省略）；变参至少给 2 个；参数是「变量引用」的传 `CVTMP`/`FLAG`/`STR` 等真实变量名，而不是字面量。
4. 拿不准的宁 skip，note 写「语义未确证」或具体顾虑。

## 汇报
分到的名字数 / 改成 call 的条数 / 新增 skip 的条数与原因分布 / 你对草稿做的典型修正（举例 3~5 个）。
