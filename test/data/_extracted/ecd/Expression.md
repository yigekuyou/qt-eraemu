表达式内函数 | Era 中文文档

-

-

-

-

-

-

-

-

-

-

-

-

-

-

-

-

-

-

Skip to content

Era 中文文档

-
指南

- 快速开始
- 入门教程

-
参考

- 参考目录
- EraBasic 语言参考手册

-
翻译

- EraMaker文档翻译
- Emuera文档翻译

- 开发
- 生态
- 贡献
-
镜像

- 全球
- 中国大陆

-

Menu
Return to top

 Sidebar Navigation

## EraMaker 部分

-

Eramaker 的 CSV 文件格式

Eramaker 的 ERB 文件格式

EraBasic 的结构

EraBasic 的变量

## Emuera 部分

-

使用指南

配置项详解

快捷键

Emuera 术语表

调试命令

调试模式

关于 _replace.csv

强制配置项目

流程图

与 Eramaker 的差异

### 新增语法

-

Emuera 扩展语法

运算

常量与变量

用户自定义变量

命令

函数与预处理指令

表达式内函数

自定义表达式内函数

头文件（ERH）

HTML_PRINT 相关

资源文件

On this page

# 表达式内函数 ​

>
翻译自原文档：https://osdn.net/projects/emuera/wiki/exmeth

Emuera 1.712新增了新的语法，被称为表达式内函数，或者叫“方法”（Method）。

ERB脚本中，函数是指以 `@ ~ ~` 的形式定义，通过`CALL`命令来调用的东西。 而表达式内函数则是同样以 `@ ~ ~` 形式定义，但直接在表达式中使用的东西。 简单的说，表达式内函数是可以在表达式中使用的函数。

下面演示了表达式内函数的使用：

erb
```
`A = ABS(A)
IF STRLENS(STR:0) > A
	LOCALS:0 = %SUBSTRING(STR:0, A, 1)%
ENDIF`
```

在上面的脚本中， 令`A`为`A`的绝对值， 若`STR:0`的字符串长度比`A`大， 则令`LOCALS:0`为`STR:0`的`A`位置以后的截取内容。

若不使用表达式内函数，上面的脚本可以重写为下面：

erb
```
`ABS A
A = RESULT
STRLENS STR:0
IF RESULT > A
	SUBSTRING STR:0, A, 1
	LOCALS:0 = %RESULTS:0%
ENDIF`
```

除了会产生中间值`RESULT`与`RESULTS`以外，这两段脚本效果完全相同。

## 示例 ​

以下内容解释了本文用到的一些符号。 例如，

```
`	int STRLENS(str s)
	str SUBSTRING(str s, int start = 0, int length = -1)`
```

行首的`int`和`str`是表达式内函数的返回值类型。 `int`是数值型，`str`是字符串型。 下面脚本第一行是正确的，第二行是错误的。

erb
```
`	A = STRLENS("abc")
	A = SUBSTRING("abc", 0, 1)
	; 错误，试图为数值型变量赋予字符串型的值`
```

接下来的`STRLENS`和`SUBSTRING`是表达式内函数的名字。

`( )`中的文字`str s`等表示表达式内函数的参数。 当有多个参数时，使用逗号分隔。 `STRLENS`有一个参数，`SUBSTRING`有3个参数。

参数的第一个单词表示参数的值类型。 `STRLENS`参数是字符串型`str`。 `SUBSTRING`的第一参数是字符串型`str`，第二、三参数是64位数值型`int`。

接下来的`str`、`start`、`length`的单词是参数的名字。 参数的名字通常为了方便理解参数的作用而起。

参数名字之后的 `=0` 表示这个参数可以省略，省略时使用的默认值。 下面脚本每一行的意思都相同：

erb
```
`	STR = SUBSTRING(RESULTS)
	STR = SUBSTRING(RESULTS, 0)
	STR = SUBSTRING(RESULTS, , -1)
	STR = SUBSTRING(RESULTS, 0, -1)`
```

也可以省略第一个参数。

erb
```
`	;int RAND(int min = 0, int max)
	A = RAND(100)
	A = RAND( , 100)
	A = RAND(0, 100)`
```

除此之外，表达式内函数也可以没有参数。

```
`	int GETTIME()`
```

虽然`GETTIME`没有参数，但是`()`是必须的。（为了区分变量和表达式内函数）

```
`	int FINDCHARA(var key, ? value, int start = 0)`
```

这段脚本中，`var`表示变量类型。这里应当输入的是`TALENT`等变量。 `?` 表示可以接受多种类型的值。

`FINDCHARA`中第二参数的类型是由第一参数决定的。

```
`	int MAX(int n，int m ...)`
```

省略号表示不限制参数的数量。

erb
```
`	M = MAX(A，B，C，E，D，F，G）`
```

`M`将等于`A~G`中数值最大的变量。

## 内置表达式内函数一览 ​

```
`int GETCHARA(int no, int flag = 0)
int GETSPCHARA(int no)
int FINDCHARA(var key, ? value, int start = 0, int end = ※)
int FINDLASTCHARA(var key, ? value, int start = 0, int end = ※)
str CSVNAME(int no, int flag = 0)
str CSVCALLNAME(int no, int flag = 0)
str CSVNICKNAME(int no, int flag = 0)
str CSVMASTERNAME(int no, int flag = 0)
str CSVCSTR(int no, int index, int flag = 0)
int CSVBASE(int no, int index, int flag = 0)
int CSVABL(int no, int index, int flag = 0)
int CSVTALENT(int no, int index, int flag = 0)
int CSVMARK(int no, int index, int flag = 0)
int CSVEXP(int no, int index, int flag = 0)
int CSVRELATION(int no, int index, int flag = 0)
int CSVJUEL(int no, int index, int flag = 0)
int CSVEQUIP(int no, int index, int flag = 0)
int CSVCFLAG(int no, int index, int flag = 0)
int EXISTCSV(int no, int flag = 0)
int GETNUM(var key, str name)
int STRLENS(str s)
int STRLENSU(str s)
str SUBSTRING(str s, int start = 0, int length = -1)
str SUBSTRINGU(str s, int start = 0, int length = -1)
str CHARATU(str s, int position = 0)
int STRFIND(str str, str find, int start = 0)
int STRFINDU(str str, str find, int start = 0)
int STRCOUNT(str input, str match)
str UNICODE(int value)
int ENCODETOUNI(str value, int position = 0)
str REPLACE(str source, str match, str newvalue)
str ESCAPE(str value)
int VARSIZE(str name, int dim = 0)
int GETTIME()
str GETTIMES()
int GETMILLISECOND()
int GETSECOND()
int CHKFONT(str fontname)
int POWER(int x, int y)
int RAND(int min = 0, int max)
int ABS(int n)
int SIGN(int n)
int MAX(int n, int m...)
int MIN(int n, int m...)
int LIMIT(int value, int min, int max)
int INRANGE(int value, int min, int max)
int SQRT(int n)
int GETBIT(int n, int m)
int CBRT(int value)
int LOG(int value)
int LOG10(int value)
int EXPONENT(int value)
str GETFONT()
int GETCOLOR()
int GETDEFCOLOR()
int GETBGCOLOR()
int GETDEFBGCOLOR()
int GETFOCUSCOLOR()
int GETSTYLE()
str CURRENTALIGN()
int CURRENTREDRAW()
str TOSTR(int value, str format = "")
int GETPALAMLV(int value, int maxLV)
int GETEXPLV(int value, int maxLV)
str TOUPPER(str value)
str TOLOWER(str value)
str TOHALF(str value)
str TOFULL(str value)
int SUMARRAY(var array, int start = 0, int end = ※)
int MATCH(var array, ? value, int start = 0, int end = ※)
int MAXARRAY(var array, int start = 0, int end = ※)
int MINARRAY(var array, int start = 0, int end = ※)
int SUMCARRAY(var carray, int start = 0, int end = CHARANUM)
int CMATCH(var carray, ? value, int start = 0, int end = CHARANUM)
int MAXCARRAY(var carray, int start = 0, int end = CHARANUM)
int MINCARRAY(var carray, int start = 0, int end = CHARANUM)
int ISNUMERIC(str value)
int TOINT(str value)
int CHKDATA(int value)
int SAVENOS()
int PRINTCPERLINE()
int LINEISEMPTY()
int GROUPMATCH(? key, ? value1, ? value2...)
int NOSAMES(? value1, ? value2...)
int ALLSAMES(? value1, ? value2...)
int ISSKIP()
int MOUSESKIP()
str CONVERT(int value, ※)
str MONEYSTR(int value, str format = "")
int FINDELEMENT(var array, ? value, int start = 0, int end = ※, int flag)
int FINDLASTELEMENT(var array, ? value, int start = 0, int end = ※, int flag)
str BARSTR(int value, int max, int length)`
```

Pager
Previous page函数与预处理指令

Next page自定义表达式内函数

GPL-3.0+ Licensed

Copyright © 2021-Present Miswanting
