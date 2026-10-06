调试命令 | Era 中文文档

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
-  函数与预处理器
-  /translation/Expression
-  自定义表达式内函数
-  头文件（ERH）
-  HTML_PRINT 相关
-  资源文件

# # 调试命令

>
翻译自原文档：https://osdn.net/projects/emuera/wiki/debugcom

调试命令在默认情况下是不可用的。

如果你想使用它们，请在帮助设置中选中 "使用调试命令"。

这个函数是一个没有调试模式的版本的简单函数。

建议在调试模式下运行游戏来调试当前的版本。

当脚本运行时（游戏中），输入以"@"开头的字符串将被接受为调试命令。

大小写敏感性取决于emuera.config中的 "忽略大小写敏感性 "选项。

调试命令的格式必须与ERB相同。

示例：

```
`;调试模式下
@MONEY = 10000
@PRINTV FLAG:200
@PRINTFORM %NAME:MASTER%のCFAG(1) = {CFLAG:MASTER:1}
@ADDCHARA 1
`
```

1
2
3
4
5

另外，可以简单地输入变量或公式，它们的值将被打印出来

(@字符后面的空格不是必须的)。

但是，改变脚本流程的命令，如`IF`和`Call`，以及需要输入的命令，如`Input`和`Wait`，不能使用。

有一些指令在一些ERB中是没有的：

- @REBOOT：重新启动并重读emuera.config、csv和erb文件。
- @OUTPUT：输出当前日志到emuera.log。 如果它已经存在，它将被覆盖。这与@OUTPUTLOG指令的行为相同。
- @EXIT：退出Emuera，这与QUIT指令相同。
- @CONFIG：打开配置对话框。
- @DEBUG：打开调试对话框。 这只有在你以调试模式启动时才可用。

如果你执行上述以外的任何其他命令，即普通的ERB命令，MASTER的名字将被强制为 "cheat"。

这是为了防止滥用调试命令，因为它也是一种欺骗行为。

Last Updated: 2021/9/30 上午5:06:17

Contributors: Miswanting

 ←  Emuera 术语表  调试模式  →
