# GETNUMB

- **类别**：式中函数
- **签名**：int GETNUMB(str varname, str key)
- **文档来源**：ecd/Expression.md 未收录；ecd/Command.md 未收录（仅 `ecd/Error_Index.md` 收录其运行期错误消息「GETNUMBの1番目の引数(…が変数名ではありません」）；zh 套件未收录。语义依据源码推导。

## 语义

与 `GETNUM` 同类：把变量对应的 CSV 文本索引（或 EE 关键字索引）转换成数值索引并返回；未定义时返回 `-1`。

与 GETNUM 的区别在于第 1 参数不是变量引用，而是**变量的名字字符串**（如 `"ABL"`），因此可以在变量名需要动态拼接的场合使用（配合 `VARSIZE`、`VARSET` 系的字符串版用法）。若第 1 参数不是已存在的变量名，抛出 CodeEE 运行期错误，而不是返回 `-1`。

## 用法

### int GETNUMB(str varname, str key)
- `varname`：字符串表达式，变量名（如 `"ABL"`）。
- `key`：字符串表达式，CSV 中定义的文本索引（名称列）。
- 返回值：对应的数值索引；未定义时 `-1`；`varname` 不是变量名时抛出运行期错误。
```erb
NAME = "ABL"
A = GETNUMB(NAME, "技巧")     ; 等价于 GETNUM(ABL, "技巧")（若 abl.csv 定义了 2,技巧 则为 2）
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:108`（`["GETNUMB"] = new GetnumBMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:3778`（`GetnumBMethod`）；转调 `Runtime/Script/Data/ConstantData.cs:830`（`ConstantData.TryKeywordToInteger`）

```text
构造：返回类型 = Int64；参数 = [string, string]；CanRestructure = true。
     （源码中注释掉的 CheckArgumentType 曾用于校验第 1 参数是变量名，现改为运行期抛错）

GetIntValue(exm, args):
    var ← GlobalStatic.IdentifierDictionary.GetVariableToken(args[0].GetStrValue(exm), null, true)
    若 var == null:
        抛出 CodeEE（"GETNUMB 的第 1 参数(\"" + args[0] + "\")不是变量名"）
    key ← args[1].GetStrValue(exm)
    若 Constant.TryKeywordToInteger(out ret, var.Code, key, -1, args[0].GetStrValue(exm)):
        返回 ret
    否则:
        返回 -1

TryKeywordToInteger:                          # 与 GETNUM 相同，见 GETNUM.md
    空 key 返回 false；先查该变量 code 的关键字词典，再查 ERD 词典（varname）
```

## 备注

- 两套文档（ecd、zh）均未收录本函数，签名依据源码构造（`argumentTypeArray = [string, string]`）与实现得出。
- 源码中留有注释「GETNUMBは使ってないのでテストしていない」（GETNUMB 没被用到所以没测试），且 EE_ERD 分支由原 4 参 `TryKeywordToInteger` 改为带 varname 的 5 参版本。
- 与 GETNUM 不同，第 1 参数错误时抛出运行期错误而非返回 `-1`；文档无从对照（未收录）。
- 旧版（被注释的）`CheckArgumentType` 曾在解析期校验第 1 参数是否为变量名，现行为是求值期抛错。
