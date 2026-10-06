命令 | Era 中文文档

-
-
-
-

-

Era 中文文档

指南指南

-  快速开始
-  入门教程

 参考

翻译翻译

-  EraMaker
-  Emuera

 开发

 生态

 贡献

指南指南

-  快速开始
-  入门教程

 参考

翻译翻译

-  EraMaker
-  Emuera

 开发

 生态

 贡献

EraMaker 部分

-  Eramaker 的 CSV 文件格式
-  Eramaker 的 ERB 文件格式
-  EraBasic 的结构
-  EraBasic 的变量

Emuera 部分

-  Emuera 术语表
-  调试命令
-  调试模式
-  关于 _replace.csv
-  强制配置项目
-  流程图
-  与 Eramaker 的差异
-
新增语法

-  常规
-  运算
-  常量与变量
-  用户自定义变量
-  命令

-  Print系

-  Print 系命令辅助选择器
-  Print(|V|S|Form|FormS)(|K|D)(|L|W)
-  PrintSingle(|V|S|Form|FormS)(|K|D)
-  Print(|Form)(C|LC)(|K|D)
-  PrintData(|K|D)(|L|W)

-  函数与预处理器
-  /translation/Expression
-  自定义表达式内函数
-  头文件（ERH）
-  HTML_PRINT 相关
-  资源文件

# # 命令

>
翻译自原文档：https://osdn.net/projects/emuera/wiki/excom

## # Print系

### # Print 系命令辅助选择器

单行溢出：溢出后自动换行溢出后不换行

参数类型：字符串数值表达式字符串表达式格式化字符串格式化字符串表达式

空格填充：无填充有，文字左对齐有，文字右对齐

优先配色：适用 SetColor适用 ForceKana忽略 SetColor

换行等待：无换行无等待只换行换行后等待

```
 `Print`
```

### # Print(|V|S|Form|FormS)(|K|D)(|L|W)

这是PRINT系统的基本指令。

第一个括号中的关键词指定了参数类型。

- `无`：字符串
- `V`：数值表达式
- `S`：字符串表达式
- `Form`：格式化字符串
- `FormS`：格式化字符串表达式

第二个括号中的关键字`K`指定应用`ForceKana`指令，关键字`D`指定忽略`SetColor`指令。 从1.736版本开始，关键词`K`和`D`不能同时指定。

- `无`：忽略`ForceKana`，以`SetColor`指定的颜色作画
- `K`：应用`ForceKana`绘制
- `D`：忽略`SetColor`，以配置中指定的默认颜色绘制

第三个括号中的关键字指定了在绘图后是否应出现换行或等待。

- `无`：只打印，无换行或等待
- `K`：`Print`后换行
- `D`：在`Print`之后换行，并执行`Wait`指令

例如，`PrintSDW`即指定`字符串表达式`作为参数，以默认颜色绘制，并在`Print`之后执行`Wait`指令。

### # PrintSingle(|V|S|Form|FormS)(|K|D)

`PrintSingle`系命令几乎与`PrintL`相同，只是`PrintSingle`系命令不对文本溢出进行换行，而是始终在单行上显示。

超过屏幕边缘的文字不会被绘制。

另外，没有`(|L|W)`关键词，因为会自动添加换行符。

其他关键词的含义与`Print`系命令相同。

### # Print(|Form)(C|LC)(|K|D)

这是一条`PrintC`指令。

它是通过增加半角空格使其达到配置中`PrintC的字符数`（初始值25）所设置的字符数的`Print`的指令。

在Emuera中，`PrintC`指令在`Print`字符串被按钮化处理的过程中有些特殊。

第一个括号中的关键词指定了参数类型。

- `无`：字符串
- `Form`：格式化字符串

括号中的第二个关键词指定了对齐位置。

- `C`：右对齐（左侧用半角空格填充）
- `LC`：左对齐

第三括号中的`K`和`D`与`Print`系中的相同。

### # PrintData(|K|D)(|L|W)

```
`PrintData(数値変数：省略可)
Data(文字)
DataForm(Form文字列)
DataList
(Data or DataForm的排列)
EndList
EndData
`
```

1
2
3
4
5
6
7

Last Updated: 2021/9/30 上午5:06:17

Contributors: Miswanting

 ←  用户自定义变量  函数与预处理器  →
