# SUMCARRAY

- **类别**：式中函数
- **签名**：int SUMCARRAY(var carray, int start = 0, int end = CHARANUM)
- **文档来源**：`ecd/Expression.md`（签名表，正文无详解小节）；`ecd/Error_Index.md`（越界报错条目）

## 语义

计算**角色变量**（`CFLAG`、`CSTR` 等带角色下标的数组）在角色区间 `[start, end)`（含头不含尾）内对应元素的合计并返回。第 1 参数必须是角色数据数值变量，可带元素下标（如 `CFLAG:2` 表示对"每个角色的 CFLAG:2"求和）。`start` 默认 0，`end` 默认 `CHARANUM`（当前登记角色数）。`start`、`end` 超出角色范围（`0 <= start < CHARANUM`、`0 <= end <= CHARANUM`）时抛出运行时错误「SUMCARRAY関数の範囲指定がキャラクタ配列の範囲を超えています」。

## 用法

### int SUMCARRAY(var carray, int start = 0, int end = CHARANUM)

- `carray`：角色数值变量（如 `CFLAG`、`DOWNBASE`），可带元素下标定位某一列。
- `start`：起始角色编号（含），可省略，默认 0。
- `end`：终止角色编号（不含），可省略，默认 `CHARANUM`。

```erb
; 求所有角色 CFLAG:3 的总和
TOTAL = SUMCARRAY(CFLAG:3)

; 求第 2~4 号角色（2,3，不含 4）CFLAG:3 的总和
PART = SUMCARRAY(CFLAG:3, 2, 4)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:89`（`["SUMCARRAY"] = new SumArrayMethod(true)`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3260`（`private sealed class SumArrayMethod`，`isCharaRange=true`），求和在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:264`（`GetArraySumChara`）

```text
函数 SUMCARRAY(charaVarTerm, start = 0, end):
    若未给 end:
        end = exm.VEvaluator.CHARANUM        ; 当前角色数
    p = charaVarTerm.GetFixedVariableTerm(exm)
    charaNum = CHARANUM
    若 start >= charaNum 或 start < 0 或 end > charaNum 或 end < 0:
        throw CodeEE("SUMCARRAY関数の範囲指定がキャラクタ配列の範囲を超えています(start～end)")
        ; 源码经 trerror.CharacterRangeInvalid 格式化
    返回 VariableEvaluator.GetArraySumChara(p, start, end)

GetArraySumChara(p, index1, index2):
    sum = 0
    对 i in index1 .. index2-1:              ; i 遍历角色编号
        sum += 元素[i, p.Index2]             ; p.Index2 即第 1 参数固定的元素下标
    返回 sum
```

## 备注

- `ecd/Expression.md` 只给出签名（`end = CHARANUM`），无正文详解；`ecd/Error_Index.md` 收录了其越界报错文案，与源码一致（源码已资源化为 `trerror.CharacterRangeInvalid`）。
- 与 `SUMARRAY` 共用 `SumArrayMethod` 类：`SUMARRAY` 走 `IsArrayRangeValid + GetArraySum`（沿最后一维），`SUMCARRAY` 走角色范围检查 + `GetArraySumChara`（沿角色维），且第 1 参数类型要求为角色变量（`ArgType.CharacterData | RefIntArray`）。
- 源码中 `GetArraySumChara` 的下标为 `[i, p.Index2]`，即角色编号在前、元素下标在后，与 EraBasic 角色变量的维度约定一致。
