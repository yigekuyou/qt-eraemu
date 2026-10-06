# 输入族条目改造指南（可注入输入 ⇒ 不再跳过）

## 背景
原先这些条目因「需要交互输入」被标 skip。但测试手段支持**注入输入**：
`test/example` 由 `test_cli --script "第一个是菜单选项,后续值按顺序被各等待指令消费"` 驱动。
所以它们可以改成 `call`，只需在清单的第 6 列写清「该喂什么」。

## 注入语法（取自 test/run_example.sh 的既有用法）
| 喂入值 | 写法 | 例 |
|---|---|---|
| 数值 | 直接写数字 | `7` |
| 字符串 | `s ` + 文本 | `s 名字` |
| 鼠标 | `k ` + 参数 | `k 1 10 10 1 0` |
| 菜单选项 | 第一个值 | `35` |

## 任务
对你分到的名字（清单见 `inputNN.txt`），把 `test/data/doc_smoke.tsv` 里对应行改成：
- `mode=call`
- `snippet`：一条可执行的调用（参数按 `test/data/commands/<NAME>.md` 的签名与「用法」；
  不要自己发明参数含义）
- 第 6 列 `input`：写清 runner 需要依次喂入的值（含类型与用途），如
  `1 个数值（回车确认；填 0 即可）` / `1 个字符串：s abc` / `k 1 10 10 1 0（左键点击）`
- 若该条**同一次调用会消费多个输入**，逐个列出
- 若读完文档后判定**确实无法用注入输入测**（例如要求 GUI 悬停、要求音频后端），
  仍可保留 `mode=skip`，note 里写明具体原因

## 参考
- 组 11 的既有做法：`test/example/ERB/11_INPUT.ERB`（会挂起等待，由 `--script` 的填充值恢复）
- 组 15 的鼠标注入：`test/run_example.sh` 里 `SCRIPT="15,k 1 10 10 1 0"` 及其注释
- 跳过中的危险项不要再改成 call：`FORCE_QUIT*`、`QUIT_AND_RESTART`、`SAVEGAME`、`SETTEXTBOX`、
  `TOOLTIP_IMG`、`TOOLTIP_SETCOLOR` 不在本批清单内。

## 输出
`_smoke_fix/inputNN.out.tsv`：6 列（`name kind mode snippet note input`），制表符分隔，名字与清单一一对应。
只依据 `test/data/**/*.md` 与 `test/example/**` 判断；**不要读** `src/**`、`Emuera/**`、`emuera.em/**`、`eraTW/**`、`build*/`。

## 汇报
改判为 call 的条数 / 仍保留 skip 的条数及原因 / 每条需要的注入输入一览 / 判定依据（引用了文档哪一节）。
