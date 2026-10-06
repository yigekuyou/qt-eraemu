# FSTRJOIN

- **类别**：EE 扩展命令（索引登记名；实为文档提取的字节切片产物，真实命令为式中函数 `STRJOIN`）
- **签名**：
  - `STRJOIN(<数组变量>{, <分隔符字符串>{, <数组下标起始位置>{, <数组元素数>}}})`
- **文档来源**：`eraTW/README集/EmueraEE Readme/私家改造版Emuera_readme.txt`（Shift-JIS 编码）Emuera1821+v7.2 条目「文字列配列の結合関数STRJOINの追加」（原文件该行以全角冒号「：」引导，其二进制字节 `0x81 0x46` 的第二字节 `0x46` 是 ASCII `F`，按字节切取关键词即误得 `FSTRJOIN`）。ecd 套件与 zh 套件均未收录；`test/data/_extracted/source_index.md` 将本名标注为 ABSENT。

## 语义

**`FSTRJOIN` 这一名字本身并不存在于任何发行版的源码或文档中**，它是从 Shift-JIS 编码的 readme 提取关键词时把全角冒号「：」的第二字节误判为 `F` 产生的伪名，实际指向的是 `STRJOIN`。

`STRJOIN` 把一维（也支持多维）数组变量的元素按指定分隔符连接成一个字符串并返回。省略分隔符时默认用 `","`（不需要分隔符就传空字符串 `""`）。指定第 3、4 参数时，只连接满足 `起始位置 ≦ i < 起始位置 + 元素数` 的元素；指定第 4 参数时第 3 参数不可省略。第 4 参数为负时报错；下标范围越界时报错。指定角色数组变量可能出错。

## 用法

### `STRJOIN(<数组变量>{, <分隔符>{, <起始位置>{, <元素数>}}})`
```erb
DIM LOCALS, 100
LOCALS:0 = "苹果"
LOCALS:1 = "香蕉"
LOCALS:2 = "橘子"
PRINTL STRJOIN(LOCALS)              ;苹果,香蕉,橘子
PRINTL STRJOIN(LOCALS, "、")        ;苹果、香蕉、橘子
PRINTL STRJOIN(LOCALS, "+", 1, 2)   ;香蕉+橘子
```

## 源码实现（emuera.em/Emuera）

- 名为 `FSTRJOIN` 的符号在本仓库（emuera.em/Emuera）源码中不存在（已全文 grep 确认）；`source_index.md` 的 ABSENT 标注对字面名成立，但语义来源其实是已实现的 `STRJOIN`。
- 注册：`Runtime/Script/Statements/Function/Creator.cs:137`（`["STRJOIN"] = new JoinMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4944`（`JoinMethod`；`GetStrValue` 在 4983 行）

```text
构造: ReturnType = string
      argumentTypeArrayEx = [ [RefAnyArray|AllowConstRef, string, int, int], OmitStart = 1 ]
      HasUniqueRestructure = true; CanRestructure = true

GetStrValue(exm, arguments):
    varTerm   = (VariableTerm)arguments[0]                       # 第1参数必须是变量
    delimiter = arguments.Count >= 2 且 arguments[1] != null ? arguments[1].GetStrValue(exm) : ","
    index1    = arguments.Count >= 3 且 arguments[2] != null ? arguments[2].GetIntValue(exm) : 0
    index2    = arguments.Count == 4 且 arguments[3] != null ? arguments[3].GetIntValue(exm)
                                                             : varTerm.GetLastLength() - index1
    p = varTerm.GetFixedVariableTerm(exm)
    if index2 < 0:
        throw CodeEE(「STRJOIN 的第 4 参数为负值」)
    p.IsArrayRangeValid(index1, index1 + index2, "STRJOIN", 2, 3)   # 越界则抛错
    return VariableEvaluator.GetJoinedStr(p, delimiter, index1, index2)   # 逐元素拼接
```

## 备注

- **索引差异**：`source_index.md` 将 `FSTRJOIN` 标为「ABSENT（EE 发行版独有，未移植）」，实际情况是该名字为编码伪名；其真实指向 `STRJOIN` 在本仓库不但存在而且属于标准 EM 函数（`Runtime/Script/Statements/Function/Creator.cs:137`）。
- readme 补充：v7.1 修改了参数规范（增加起始位置／元素数），v7.2 修正数值数组作变量、分隔符处理的问题；分隔符省略时自动用 `","`，与其它语言同名处理一致。
- 多维数组支持见 readme 后续条目（「多次元配列にも対応」时代并入）；角色变量（`Reflect` 类）传入可能出错，与 readme「キャラ変数を指定した場合エラーになるかも」一致（源码用 `RefAnyArray` 接受任意数组引用，角色变量的掩码元素拼接行为未做特别处理）。
