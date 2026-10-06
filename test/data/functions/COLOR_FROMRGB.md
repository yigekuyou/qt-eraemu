# COLOR_FROMRGB

- **类别**：式中函数（EE 扩展函数）
- **签名**：`int COLOR_FROMRGB(int red, int green, int blue)`
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 无独立小节，仅在「TOOLTIP_SETCOLOR」小节提及（「如果想用 R、G、B 值或字符串来指定，请使用 `COLOR_FROMRGB`、`COLOR_FROMNAME` 函数」）；zh 套件未收录

## 语义

把红、绿、蓝三个分量（各 0～255）合成为 `0xRRGGBB` 形式的整数返回，可直接用于 `SETCOLOR`、`TOOLTIP_SETCOLOR`、`BARCOLOR` 等需要颜色值的指令/函数。

任一分量超出 0～255 范围时抛出运行时错误（CodeEE，指明第几个参数与实际值）。

## 用法

### int COLOR_FROMRGB(int red, int green, int blue)
- `red`：红色分量（0～255）。
- `green`：绿色分量（0～255）。
- `blue`：蓝色分量（0～255）。
```erb
; 纯红 = 0xFF0000 = 16711680
SETCOLOR COLOR_FROMRGB(255, 0, 0)
PRINTL 红色文字
; 由变量合成颜色（比手写 0xRRGGBB 常量易读）
R = 100 * (100 - RATE) / 100 + 155
SETCOLOR COLOR_FROMRGB(R, 80, 80)
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:54`（`["COLOR_FROMRGB"] = new ColorFromRGBMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2738`（`ColorFromRGBMethod`，返回 long，参数 `[long, long, long]`，`CanRestructure = true`）

```text
函数 COLOR_FROMRGB(r, g, b):
    r ← 第 1 参数求值
    若 r < 0 或 r > 255: 抛出 CodeEE（"{Name}関数:第１引数が0から255の範囲外です" 之意，带实际值）
    g ← 第 2 参数求值
    若 g 超范围: 抛出 CodeEE（第２引数版）
    b ← 第 3 参数求值
    若 b 超范围: 抛出 CodeEE（第３引数版）
    返回 (r << 16) + (g << 8) + b
```

## 备注

- `CanRestructure = true`：纯函数，结果可被当作定值缓存。
- 三个参数按顺序逐一求值、逐一校验，首个越界的参数触发错误。
- 与 `COLOR_FROMNAME` 互补：一个按数值合成、一个按颜色名查找；两函数均返回不含 Alpha 的 `0xRRGGBB`。
- 三套文档均无独立小节，以上语义完全来自源码；参数名 red/green/blue 为文档生成者按语义所起，源码仅按位置取参。
