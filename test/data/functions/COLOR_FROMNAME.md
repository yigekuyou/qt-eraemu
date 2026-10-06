# COLOR_FROMNAME

- **类别**：式中函数（EE 扩展函数）
- **签名**：`int COLOR_FROMNAME(str colorname)`
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 无独立小节，仅在「TOOLTIP_SETCOLOR」小节提及（「如果想用 R、G、B 值或字符串来指定，请使用 `COLOR_FROMRGB`、`COLOR_FROMNAME` 函数」）；zh 套件未收录

## 语义

把 .NET/C# 的颜色名称（如 `"Red"`、`"MidnightBlue"`，即 `System.Drawing.Color.FromName` 认识的 140 个左右的已知颜色名，不区分大小写）转换为 `0x00RRGGBB` 形式的整数返回，可直接用于 `SETCOLOR`、`TOOLTIP_SETCOLOR` 等。

- 名称是已知颜色时：返回该颜色的 `(R<<16)+(G<<8)+B`。
- 名称是 `"transparent"`（不区分大小写）时：抛出运行时错误（透明色不被支持）。
- 名称未知时：`Color.FromName` 返回 A=0 的空颜色，函数返回 `-1`，不报错。

## 用法

### int COLOR_FROMNAME(str colorname)
- `colorname`：颜色名称（字符串表达式）。
```erb
; 用颜色名设置前景色
SETCOLOR COLOR_FROMNAME("Red")
PRINTL 红色文字
SETCOLOR COLOR_FROMNAME("MidnightBlue")
PRINTL 午夜蓝文字
; 未知颜色名返回 -1
IF COLOR_FROMNAME("NotAColor") == -1
    PRINTL 未知的颜色名
ENDIF
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:53`（`["COLOR_FROMNAME"] = new ColorFromNameMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2711`（`ColorFromNameMethod`，返回 long，参数 `[string]`，`CanRestructure = true`）

```text
函数 COLOR_FROMNAME(colorName):
    color ← Color.FromName(colorName)      ; .NET 已知颜色名查找（不区分大小写）
    若 color.A > 0:                        ; 找到了实际颜色
        返回 (color.R << 16) + (color.G << 8) + color.B
    否则:                                  ; A == 0：未知颜色名或透明
        若 colorName 与 "transparent" 忽略大小写相等:
            抛出 CodeEE（"无色透明(Transparent)は色として指定できません" 之意）
        返回 -1                            ; 未知颜色名，静默返回 -1
```

## 备注

- `CanRestructure = true`：颜色名到数值的映射固定，结果可被当作定值缓存。
- 未知颜色名返回 `-1` 而非报错——这是源码显式行为（原日语注释 `指定された色名"..."は無効な色名です` 的抛错代码被替换为 `i = -1`），注意 `-1` 作 `0xFFFFFF` 解释时会变成白色，若用作 SETCOLOR 需自行检查。
- 返回值不含 Alpha 通道；Emuera 的颜色参数普遍是 `0xRRGGBB`。
- 同族函数：`COLOR_FROMRGB`（按 R,G,B 数值合成）。
- 三套文档均无独立小节，以上语义完全来自源码。
