# ARRAYSHIFT

- **类别**：命令
- **签名**：
  - `ARRAYSHIFT <目标变量>`, `<移动数量>`, `<移动产生的空白区域的初始值>`{, `<移动范围的初始值>`, `<移动的元素范围数量>`}
- **文档来源**：`ecd/docs/translation/Command.md`「数组操作」相关小节（ARRAYSHIFT）；`Era-Chinese-Documentation` 套件未收录本命令。

## 语义

把数组平移指定数量。正值向索引较大的方向移动，负值向较小的方向移动。移出数组范围的值会被舍弃，移动后产生的空白区域用第 3 参数指定的值填充。

使用可省略的第 4、第 5 参数，可以只移动一部分范围（从起始索引开始的指定个元素）。

只支持一维数组以及数组型角色变量，不能用于 `DITEMTYPE`、`TA` 等（多维数组）。起始索引或移动元素数为负时报错；移动数量为 0 时什么都不做。

## 用法

### `ARRAYSHIFT <目标变量>, <移动数量>, <空白初始值>{, <移动范围初始值>, <移动元素数>}`
- `<目标变量>`：目标一维数组变量（可整体写变量名，如 `ARRAYSHIFT FLAG, 1, 0`）。
- `<移动数量>`：数值表达式；正数向大索引方向移动，负数向小索引方向移动。
- `<移动产生的空白区域的初始值>`：数值或字符串表达式，类型须与数组一致，用于填充移动后的空位。
- `<移动范围的初始值>`（可省略）：移动范围的起始索引；省略时为 0。为负时报错。
- `<移动的元素范围数量>`（可省略）：移动的元素个数；省略时移动到数组末尾。为负时报错，为 0 时什么都不做。
```erb
;整体右移 1 位，空出的 FLAG:0 填 0
ARRAYSHIFT FLAG, 1, 0
;从索引 10 开始的 5 个元素左移 2 位
ARRAYSHIFT FLAG, -2, -1, 10, 5
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/FunctionIdentifier.cs:329`（`argb[FunctionArgType.SP_SHIFT_ARRAY]`，`METHOD_SAFE | EXTENDED`）
- 实现：`Runtime/Script/Process.ScriptProc.cs:612`（`case FunctionCode.ARRAYSHIFT` 分支）；核心算法在 `Runtime/Script/Statements/Variable/VariableEvaluator.cs:533`（`ShiftArray`，数值版）与 `:606`（字符串版）

```text
case ARRAYSHIFT:
    arg = (SpArrayShiftArgument)func.Argument
    若 arg.VarToken.Identifier 不是一维数组:
        抛出 CodeEE（"ARRAYSHIFT 只能用于一维变量"）
    dest = arg.VarToken 的固定变量项
    shift = (int)arg.Num1 求值
    若 shift == 0: 什么都不做
    start = (int)arg.Num3 求值（移动范围初始值）
    若 start < 0: 抛出 CodeEE（"ARRAYSHIFT 第4参数为负"）
    若 arg.Num4 存在:
        num = (int)arg.Num4 求值
        若 num < 0: 抛出 CodeEE（"ARRAYSHIFT 第5参数为负"）
        若 num == 0: 什么都不做
    否则:
        num = -1（表示到数组末尾）
    若 dest 是整数数组:
        def = arg.Num2 求值（long）
        VariableEvaluator.ShiftArray(dest, shift, def, start, num)
    否则:
        defs = arg.Num2 求值（string）
        VariableEvaluator.ShiftArray(dest, shift, defs, start, num)

ShiftArray(p, shift, def, start, num):   # 数值版，字符串版逻辑相同
    array = p.Identifier 的底层数组（角色变量则取对应角色的数组）
    若 start >= array.Length: 抛出 CodeEE（"ARRAYSHIFT 的范围越界"）
    若 num == -1: num = array.Length - start
    若 start + num > array.Length: num = array.Length - start   # 范围截断到数组末尾
    若 |shift| >= array.Length 且 start == 0 且 num >= array.Length:
        # 整体移出数组：全部填默认值
        array 全元素 = def; 返回
    length = num - |shift|           # 实际还能保留的重叠长度
    若 shift > 0:
        先把 array[start .. start+num-1] 备份
        若 length > 0: 把 array[start .. start+shift-1] 填 def，再把备份的前 length 个
                       元素拷回 array[start+shift .. start+shift+length-1]
        否则: 把 array[start .. start+num-1] 全部填 def
    若 shift < 0:
        先把 array[start .. start+num-1] 备份
        若 length > 0: 把 array[start+length .. start+num-1] 填 def，再把备份中从
                       |-shift| 起的 length 个元素拷回 array[start .. start+length-1]
        否则: 把 array[start .. start+num-1] 全部填 def
```

## 备注

- 文档说「不能用于 `DITEMTYPE`、`TA` 等」，源码以「必须是一维数组」来实现这一限制（多维数组直接抛错），两者一致。
- 源码数值版用 `Buffer.BlockCopy` 备份与回拷，语义同上伪代码。
- 第 2 参数（shift）为 0 时直接跳出，不做任何检查；第 3 参数类型与数组类型不符时会走对应求值路径抛错。
- zh 套件未收录本命令。
