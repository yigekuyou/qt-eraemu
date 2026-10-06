# CMATCH

- **类别**：式中函数
- **签名**：`int CMATCH(var carray, ? value, int start = 0, int end = CHARANUM)`
- **文档来源**：`ecd/Expression.md`（函数目录签名，与 MATCH 系列并列）；zh 套件未收录

## 语义

在角色变量 `carray`（如 `CFLAG`、`CSTR` 等带角色维度的数组变量）中，统计「其对应元素等于 `value`」的角色个数，返回该计数（写入匹配元素的角色数量）。

检索范围是角色编号 `[start, end)`（左闭右开），`start` 默认 0，`end` 默认 `CHARANUM`（当前角色数）。`value` 的类型必须与 `carray` 的元素类型一致（数值变量配数值、字符串变量配字符串）。

是 `MATCH` 的角色变量版；范围越界（`start < 0`、`start ≥ CHARANUM`、`end > CHARANUM`、`end < 0`）时抛出运行时错误。

## 用法

### int CMATCH(var carray, ? value, int start = 0, int end = CHARANUM)
- `carray`：角色数组变量（第 0 参数）。作为变量引用传入，不是传值。
- `value`：要匹配的值，类型须与数组元素一致。
- `start`：起始角色编号（可省略，默认 0）。
- `end`：结束角色编号（可省略，默认 CHARANUM），不含该编号本身。
```erb
; 统计 CFLAG:2（好感度）等于 100 的角色数
COUNT = CMATCH(CFLAG, 100, 2)
; 等价于固定全范围
PRINTFORML 好感度100的角色有 {CMATCH(CFLAG, 100)} 人
; 字符串角色变量
PRINTFORML 名字为"安娜"的角色数 = {CMATCH(CSTR, "安娜")}
; 指定范围：只看第 0～4 号角色
PRINTFORML 前 5 人中 = {CMATCH(CFLAG, 100, 0, 5)} 人
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:91`（`["CMATCH"] = new MatchMethod(true)`；与 `MATCH` 共用 `MatchMethod` 类）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3333`（`MatchMethod`）；计数逻辑转调 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:340/353`（`GetMatchChara(long/string)`）

```text
函数 CMATCH(varTerm, value, start = 0, end = CHARANUM):
    start ← 若第 3 参数给定则求值，否则 0
    end   ← 若第 4 参数给定则求值，否则 exm.VEvaluator.CHARANUM
    p ← varTerm 取得固定变量项（固定角色维外的索引，如 CFLAG 的元素号）
    charaNum ← CHARANUM
    若 start ≥ charaNum 或 start < 0 或 end > charaNum 或 end < 0:
        抛出 CodeEE（"CMATCH関数の範囲指定がキャラクタ配列の範囲を超えています(start～end)"）
    ret ← 0
    若 value 为数值:
        对 i = start .. end-1:
            若 变量[i, p.Index2, p.Index3] 的整数值 == value: ret++
    若 value 为字符串:
        targetIsNullOrEmpty ← (value 为空)
        对 i = start .. end-1:
            s ← 变量[i, p.Index2, p.Index3] 的字符串值
            若 s == value 或 (value 为空 且 s 为空): ret++
    返回 ret
```

## 备注

- 文档与源码的差异：
  - ecd 签名 `int CMATCH(var carray, ? value, ...)` 中第 1 参数名暗示「角色变量」，源码（EE 版）的 `MatchMethod(true)` 构造里参数类型放宽为 `ArgType.Any`（原本的角色变量检查代码被注释掉），即第 1 参数实际不强制校验为角色变量，运行时按 `[角色号, 固定索引...]` 的方式取值。
  - 字符串匹配时，源码对「目标为空串」做了特判：空串与空元素（未赋值）也算匹配。
- `start`/`end` 校验与 `MATCH` 不同：`MATCH` 用 `IsArrayRangeValid`（报错把范围参数标为第 3、4 参数），`CMATCH` 手工校验并报「范围超出角色数组范围」。
- 同族函数：`MATCH`（普通一维数组版）、`GROUPMATCH`（多值相等计数）、`FINDCHARA`/`FINDLASTCHARA`（返回首个/末个匹配的角色编号而非计数）。
- 两套文档均未给出示例与详细说明，仅 ecd Expression.md 有签名一行；以上示例为按语义构造。
