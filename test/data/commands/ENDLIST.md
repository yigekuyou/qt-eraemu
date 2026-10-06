# ENDLIST

- **类别**：命令
- **签名**：`ENDLIST`
- **文档来源**：`ecd/docs/translation/Command.md`（「PRINT系列」节 `### PRINTDATA(|K|D)(|L|W)` 中「`DATALIST`～`ENDLIST` 中每个 `DATA` 或 `DATAFORM` 都相当于一行」的说明及语法框架）；`Era-Chinese-Documentation` 在 `zh/Command.md:175` 出现 `EndList` 字样但无语义说明。

## 语义

`DATALIST`～`ENDLIST` 用于在 `PRINTDATA`（或 `STRDATA`）块内定义一条多行候选文本：`DATALIST` 与 `ENDLIST` 之间的每个 `DATA`/`DATAFORM` 都作为该候选项的一行。`ENDLIST` 结束这个多行组，装载期把组内各行打包成 `PRINTDATA`/`STRDATA` 的一个候选条目。运行期 `ENDLIST` 本身是空操作。`ENDLIST` 必须直接配对最近的 `DATALIST`，否则报解析警告；空的 `DATALIST`（组内无 DATA）会收到警告。

## 用法

### `ENDLIST`

无参数，位于 `DATALIST` 块末尾。

```erb
PRINTDATA
    DATA 单行候选
    DATALIST          ;这个多行候选整体算一条
        DATA 三行文本的
        DATAFORM 第 \@(1+1)\@ 行
        DATA 第三行
    ENDLIST
ENDDATA
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:308` → `argb[FunctionArgType.VOID], METHOD_SAFE | EXTENDED | PARTIAL`
- 实现：运行期无独立指令（PARTIAL，装载期处理）；装载期处理在 `Runtime/Script/Loader/ErbLoader.cs:1332`（`case FunctionCode.ENDLIST`）

```text
装载期（ErbLoader）:
    遇到 ENDLIST:
        若 nestStack 为空或栈顶不是 DATALIST 行:
            发出解析警告（UnexpectedEndlist），跳过
        若 tempLineList 为空（DATALIST 与 ENDLIST 之间没有 DATA）:
            发出警告（DatalistDataIsMissing）
        弹出 DATALIST 行
        把 tempLineList（组内按序收集的 DATA/DATAFORM 行）
            作为一个整体条目追加到外层 PRINTDATA/STRDATA 行
            的 dataList 中

    相关装载约束:
        DATALIST 只能出现在 PRINTDATA 系/STRDATA 内（ErbLoader.cs:1319）
        DATALIST 内只允许 DATA / DATAFORM / ENDLIST（ErbLoader.cs:992）

运行期:
    无操作（ENDLIST 未生成运行期指令，运行期动作全部由
    PRINTDATA/STRDATA 行本身完成：从 dataList 随机选条目输出）
```

## 备注

- `ENDLIST` 与 `ENDDATA` 一样只有装载期语义（`PARTIAL` 标记），没有 `DoInstruction` 实现；这与 ecd 文档把它们描述为纯语法结构一致。
- `STRDATA` 文档补充：`DATALIST` 中的 `DATA` 系内容用换行符连接后作为一个字符串返回——即 ENDLIST 打包的多行条目在 STRDATA 里是含换行的单条候选。
- `ENDDATA` 处遇到未闭合的 `DATALIST` 会报 `DatalistNotClosed`（ErbLoader.cs:1385）。
