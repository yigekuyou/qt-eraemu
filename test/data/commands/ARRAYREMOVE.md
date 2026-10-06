# ARRAYREMOVE

- **类别**：命令
- **签名**：
  - `ARRAYREMOVE <目标变量>`, `<删除范围的初始值>`, `<删除的元素数>`
- **文档来源**：`ecd/docs/translation/Command.md`「数组操作」相关小节（ARRAYREMOVE）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

部分删除数组元素。从指定的初始值（索引）开始删除指定元素数个元素，并把后面的值向前填补。数组本身大小不变，尾部腾出的空位以 0 或空字符串填充。

把删除元素数设为 0 或更小时，会删除从初始值到末尾的全部元素。

只支持一维数组以及数组型角色变量，不能用于 `DITEMTYPE`、`TA` 等（多维数组）。起始索引为负或越界时报错。

## 用法

### `ARRAYREMOVE <目标变量>, <删除范围的初始值>, <删除的元素数>`
- `<目标变量>`：目标一维数组变量。
- `<删除范围的初始值>`：数值表达式，删除起始索引；为负时报错。
- `<删除的元素数>`：数值表达式；≤ 0 时删除从起始索引到末尾的全部元素。
```erb
;删除 FLAG 的第 5、6、7 个元素，后面元素前移
ARRAYREMOVE FLAG, 5, 3
;删除从索引 2 起到末尾的全部元素
ARRAYREMOVE FLAG, 2, 0
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:330`（`argb[FunctionArgType.SP_CONTROL_ARRAY]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:647`（`case FunctionCode.ARRAYREMOVE` 分支）；核心算法在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:678`（`RemoveArray`）

```text
case ARRAYREMOVE:
    arg = (SpArrayControlArgument)func.Argument
    若 arg.VarToken.Identifier 不是一维数组:
        抛出 CodeEE（"ARRAYREMOVE 只能用于一维变量"）
    p = arg.VarToken 的固定变量项
    start = (int)arg.Num1 求值
    num   = (int)arg.Num2 求值
    若 start < 0: 抛出 CodeEE（"ARRAYREMOVE 第2参数为负"）
    # 源码中 num<0 与 num<=0 的显式检查被注释掉了，交给 RemoveArray 内部处理
    VariableEvaluator.RemoveArray(p, start, num)

RemoveArray(p, start, num):
    array = p.Identifier 的底层数组（角色变量则取对应角色的数组）
    若 start >= array.Length: 抛出 CodeEE（"ARRAYREMOVE 的范围越界"）
        # 注：该越界检查只存在于整数数组分支；字符串数组分支没有此检查，
        #     越界时会在后续 Array.Copy 处抛出普通异常（非 CodeEE）
    若 num <= 0: num = array.Length          # ≤0 → 删到末尾
    新建与 array 同长、全 0（或空串）的临时数组 temp
    若 start > 0:
        把 array[0 .. start-1] 拷入 temp[0 .. start-1]
    若 start + num < array.Length:
        把 array[start+num .. 末尾] 拷入 temp[start ..]
        （即后面的元素向前填补删除区，temp 尾部保持 0/空串）
    把 temp 整体拷回 array
```

## 备注

- 文档「删除元素数设为 0 或更小时删除从初始值到末尾的全部元素」与源码 `num <= 0 → num = array.Length` 语义吻合（配合后续截断，等效于删到末尾）。
- 数组长度不变，只是内容前移、尾部补 0/空串，这是与「真正缩短数组」不同的重要语义。
- 与 ARRAYSHIFT、ARRAYSORT 共用「必须一维数组」的限制。
- zh 套件未收录本命令。
