ERB 的命令 | Era 中文文档

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

## 文件参考

-

Config 文件参考

CSV 文件参考

## EraBasic 语言参考

-

ERB 文件结构

ERB 的变量

ERB 的表达式

ERB 的命令

ERB 的语句

ERB 的复合语句

ERB 的内置流程

## 速查

-

兼容性矩阵

术语中英日对照

-

错误索引表

版本特性索引

On this page

# ERB 的命令 ​

命令是让引擎“做事”的语句。EraBasic 的命令非常多，本页按用途分类整理成索引，并给出常用命令。每个命令的完整签名、参数与示例，请点击分类标题进入命令详解。

## 命令总表 ​

下面是全部命令的可搜索总表。输入命令名或签名片段即可筛选，也可以用下拉框按分组过滤；点击命令名跳到详细说明。

全部分组值类型规范PRINT系列显示处理·字体处理·显示方式参考字符串操作·引用算术角色操作·引用变量操作·变量引用·CSV引用游戏存档的操作日期·时间的获取输入·等待循环·分支语法随机数的控制调试辅助·系统流的控制CALL·JUMP·GOTO系RETURN系DEBUG系HTML系工具提示系AWAIT 相关图像处理相关未整理项目共 263 条

|命令|签名|分组|

|PRINT|`PRINT(|V|S|FORM|FORMS)(|K|D)(|L|W)`|PRINT系列|
|PRINTSINGLE|`PRINTSINGLE(|V|S|FORM|FORMS)(|K|D)`|PRINT系列|
|PRINT|`PRINT(|FORM)(C|LC)(|K|D)`|PRINT系列|
|PRINTDATA|`PRINTDATA(|K|D)(|L|W)`|PRINT系列|
|PRINTBUTTON|`PRINTBUTTON(|C|LC) <字符串表达式>, <数值表达式或字符串表达式>`|PRINT系列|
|PRINTPLAIN|`PRINTPLAIN (|FORM）`|PRINT系列|
|CUSTOMDRAWLINE|`CUSTOMDRAWLINE <文本>`|PRINT系列|
|DRAWLINEFORM|`DRAWLINEFORM <FORM格式文本>`|PRINT系列|
|REUSELASTLINE|`REUSELASTLINE <FORM格式文本>`|PRINT系列|
|CLEARLINE|`CLEARLINE <行数>`|PRINT系列|
|PRINT_IMG|`PRINT_IMG <字符串表达式>`|PRINT系列|
|PRINT_RECT|`PRINT_RECT <数值表达式>`|PRINT系列|
|PRINT_RECT|`PRINT_RECT <数值表达式>, <数值表达式>, <数值表达式>, <数值表达式>`|PRINT系列|
|PRINT_SPACE|`PRINT_SPACE <数值表达式>`|PRINT系列|
|SETCOLOR|`SETCOLOR <红>, <绿>, <蓝>`|显示处理·字体处理·显示方式参考|
|SETCOLOR|`SETCOLOR <RGB>`|显示处理·字体处理·显示方式参考|
|RESETCOLOR|`RESETCOLOR`|显示处理·字体处理·显示方式参考|
|SETBGCOLOR|`SETBGCOLOR <红>, <绿>, <蓝>`|显示处理·字体处理·显示方式参考|
|SETBGCOLOR|`SETBGCOLOR <RGB>`|显示处理·字体处理·显示方式参考|
|RESETBGCOLOR|`RESETBGCOLOR`|显示处理·字体处理·显示方式参考|
|SETCOLORBYNAME|`SETCOLORBYNAME <文本>`|显示处理·字体处理·显示方式参考|
|SETBGCOLORBYNAME|`SETBGCOLORBYNAME <文本>`|显示处理·字体处理·显示方式参考|
|GETCOLOR|`GETCOLOR`|显示处理·字体处理·显示方式参考|
|GETDEFCOLOR|`GETDEFCOLOR`|显示处理·字体处理·显示方式参考|
|GETBGCOLOR|`GETBGCOLOR`|显示处理·字体处理·显示方式参考|
|GETDEFBGCOLOR|`GETDEFBGCOLOR`|显示处理·字体处理·显示方式参考|
|GETFOCUSCOLOR|`GETFOCUSCOLOR`|显示处理·字体处理·显示方式参考|
|FONTBOLD|`FONTBOLD`|显示处理·字体处理·显示方式参考|
|FONTITALIC|`FONTITALIC`|显示处理·字体处理·显示方式参考|
|FONTREGULAR|`FONTREGULAR`|显示处理·字体处理·显示方式参考|
|FONTSTYLE|`FONTSTYLE <数值表达式>`|显示处理·字体处理·显示方式参考|
|GETSTYLE|`GETSTYLE`|显示处理·字体处理·显示方式参考|
|CHKFONT|`CHKFONT <字符串表达式>`|显示处理·字体处理·显示方式参考|
|SETFONT|`SETFONT <字符串表达式>`|显示处理·字体处理·显示方式参考|
|GETFONT|`GETFONT`|显示处理·字体处理·显示方式参考|
|FORCEKANA|`FORCEKANA <数值表达式>`|显示处理·字体处理·显示方式参考|
|ALIGNMENT|`ALIGNMENT <LEFT or CENTER or RIGHT>`|显示处理·字体处理·显示方式参考|
|CURRENTALIGN|`CURRENTALIGN`|显示处理·字体处理·显示方式参考|
|REDRAW|`REDRAW <数值表达式>`|显示处理·字体处理·显示方式参考|
|CURRENTREDRAW|`CURRENTREDRAW`|显示处理·字体处理·显示方式参考|
|PRINTCPERLINE|`PRINTCPERLINE`|显示处理·字体处理·显示方式参考|
|LINEISEMPTY|`LINEISEMPTY`|显示处理·字体处理·显示方式参考|
|BARSTR|`BARSTR <变量>, <最大值>, <长度>`|显示处理·字体处理·显示方式参考|
|MONEYSTR|`MONEYSTR <数值>{, <格式指示符>}`|显示处理·字体处理·显示方式参考|
|SKIPDISP|`SKIPDISP <数值>`|显示处理·字体处理·显示方式参考|
|NOSKIP|`NOSKIP`|显示处理·字体处理·显示方式参考|
|ENDNOSKIP|`ENDNOSKIP`|显示处理·字体处理·显示方式参考|
|ISSKIP|`ISSKIP`|显示处理·字体处理·显示方式参考|
|MOUSESKIP|`MOUSESKIP`|显示处理·字体处理·显示方式参考|
|TOUPPER|`TOUPPER <字符串表达式>`|字符串操作·引用|
|TOLOWER|`TOLOWER <字符串表达式>`|字符串操作·引用|
|TOHALF|`TOHALF <字符串表达式>`|字符串操作·引用|
|TOFULL|`TOFULL <字符串表达式>`|字符串操作·引用|
|TOSTR|`TOSTR <数值表达式>, <格式指示符>`|字符串操作·引用|
|ISNUMERIC|`ISNUMERIC <字符串表达式>`|字符串操作·引用|
|TOINT|`TOINT <字符串表达式>`|字符串操作·引用|
|STRLEN|`STRLEN <文本>`|字符串操作·引用|
|STRLENS|`STRLENS <字符串表达式>`|字符串操作·引用|
|STRLENFORM|`STRLENFORM <FORM格式文本>`|字符串操作·引用|
|STRLENU|`STRLENU <文本>`|字符串操作·引用|
|STRLENSU|`STRLENSU <字符串表达式>`|字符串操作·引用|
|STRLENFORMU|`STRLENFORMU <FORM格式文本>`|字符串操作·引用|
|SUBSTRING|`SUBSTRING <字符串表达式>, <数值表达式>, <数值表达式>`|字符串操作·引用|
|SUBSTRINGU|`SUBSTRINGU <字符串表达式>, <数值表达式>, <数值表达式>`|字符串操作·引用|
|CHARATU|`CHARATU <字符串表达式>, <文字位置>`|字符串操作·引用|
|STRFIND|`STRFIND <字符串表达式>, <字符串表达式>(, <数值表达式>)`|字符串操作·引用|
|STRFINDU|`STRFINDU <检索对象>, <检索字符串>{, <起始位置>}`|字符串操作·引用|
|STRCOUNT|`STRCOUNT <检索对象字符串>, <检索字符串>`|字符串操作·引用|
|SPLIT|`SPLIT <字符串表达式>, <字符串表达式>, <字符串变量>`|字符串操作·引用|
|REPLACE|`REPLACE <原始字符串>, <查找字符串>, <替换字符串>`|字符串操作·引用|
|ESCAPE|`ESCAPE <字符串>`|字符串操作·引用|
|UNICODE|`UNICODE <数值表达式>`|字符串操作·引用|
|ENCODETOUNI|`ENCODETOUNI <对象字符串(FORM格式字符串)>`|字符串操作·引用|
|POWER|`POWER <变量>, <数值表达式>, <数值表达式>`|算术|
|ABS|`ABS <数值表达式>`|算术|
|SIGN|`SIGN <数值表达式>`|算术|
|SQRT|`SQRT <数值表达式>`|算术|
|GETBIT|`GETBIT <数值表达式>, <数值表达式>`|算术|
|MAX|`MAX <数值表达式>(, <数值表达式>...)`|算术|
|MIN|`MIN <数值表达式>(, <数值表达式>...)`|算术|
|LIMIT|`LIMIT <数值表达式>, <数值表达式>, <数值表达式>`|算术|
|INRANGE|`INRANGE <数值表达式>, <数值表达式>, <数值表达式>`|算术|
|SETBIT|`SETBIT <数值型变量>, <数值表达式>{, <数值表达式>,...}`|算术|
|CLEARBIT|`CLEARBIT <数值型变量>, <数值表达式>{, <数值表达式>,...}`|算术|
|INVERTBIT|`INVERTBIT <数值型变量>, <数值表达式>{, <数值表达式>,...}`|算术|
|ADDCHARA|`ADDCHARA <数值表达式>(, <数值表达式>, <数值表达式>, ...)`|角色操作·引用|
|ADDSPCHARA|`ADDSPCHARA <数值表达式>(, <数值表达式>, <数值表达式>, ...)`|角色操作·引用|
|DELCHARA|`DELCHARA <数值表达式>(, <数值表达式>, <数值表达式>, ...)`|角色操作·引用|
|SWAPCHARA|`SWAPCHARA <数值表达式>, <数值表达式>`|角色操作·引用|
|SORTCHARA|`SORTCHARA <角色变量> {, <FORWARD or BACK>}`|角色操作·引用|
|GETCHARA|`GETCHARA <角色编号>, (<0 或 0 以外>，可省略)`|角色操作·引用|
|GETSPCHARA|`GETSPCHARA <角色编号>`|角色操作·引用|
|ADDDEFCHARA|`ADDDEFCHARA`|角色操作·引用|
|ADDVOIDCHARA|`ADDVOIDCHARA`|角色操作·引用|
|DELALLCHARA|`DELALLCHARA`|角色操作·引用|
|PICKUPCHARA|`PICKUPCHARA <目标角色>(, <目标角色>, ....)`|角色操作·引用|
|EXISTCSV|`EXISTCSV <数值表达式>, <数值表达式>`|角色操作·引用|
|FINDCHARA|`FINDCHARA <角色变量>, <式>(, <数值表达式>, <数值表达式>)`|角色操作·引用|
|FINDLASTCHARA|`FINDLASTCHARA <角色变量>, <式>(, <数值表达式>, <数值表达式>)`|角色操作·引用|
|COPYCHARA|`COPYCHARA <数值表达式>, <数值表达式>`|角色操作·引用|
|ADDCOPYCHARA|`ADDCOPYCHARA <数值表达式>`|角色操作·引用|
|VARSIZE|`VARSIZE <变量名>`|变量操作·变量引用·CSV引用|
|RESETDATA|`RESETDATA`|变量操作·变量引用·CSV引用|
|RESETGLOBAL|`RESETGLOBAL`|变量操作·变量引用·CSV引用|
|RESET_STAIN|`RESET_STAIN <数值表达式>`|变量操作·变量引用·CSV引用|
|SWAP|`SWAP <变量1>, <变量2>`|变量操作·变量引用·CSV引用|
|CSVNAME|`CSVNAME <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVCALLNAME|`CSVCALLNAME <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVNICKNAME|`CSVNICKNAME <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVMASTERNAME|`CSVMASTERNAME <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVBASE|`CSVBASE <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVCSTR|`CSVCSTR <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVABL|`CSVABL <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVTALENT|`CSVTALENT <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVMARK|`CSVMARK <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVEXP|`CSVEXP <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVRELATION|`CSVRELATION <数值表达式>, <数值表达式>, <数值表达式>`|变量操作·变量引用·CSV引用|
|CSVJULE|`CSVJULE <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVEQUIP|`CSVEQUIP <数值表达式>, <数值表达式>(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|CSVCFLAG|`CSVCFLAG <数值表达式>, <数值表达式>,(, <数值表达式>)`|变量操作·变量引用·CSV引用|
|GETNUM|`GETNUM <变量名>, <字符串表达式>`|变量操作·变量引用·CSV引用|
|GETPALAMLV|`GETPALAMLV <数值表达式>, <判断的 LV 上限>`|变量操作·变量引用·CSV引用|
|GETEXPLV|`GETEXPLV <数值表达式>, <判断的 LV 上限>`|变量操作·变量引用·CSV引用|
|FINDELEMENT|`FINDELEMENT <一元数组>, <检索值>, <初始索引>, <终止索引>, <全词匹配>`|变量操作·变量引用·CSV引用|
|FINDLASTELEMENT|`FINDLASTELEMENT <一元数组>, <检索值>, <初始索引>, <终止索引>, <全词匹配>`|变量操作·变量引用·CSV引用|
|VARSET|`VARSET <变量名>{, <数值表达式 or 字符串表达式>, <初始索引>, <终止索引+1>}`|变量操作·变量引用·CSV引用|
|CVARSET|`CVARSET <角色变量>{, <数值表达式>, <表达式>, <初始角色编号>, <终止角色编号+1>}`|变量操作·变量引用·CSV引用|
|ARRAYSHIFT|`ARRAYSHIFT <目标变量>, <移动数量>, <移动产生的空白区域的初始值>{, <移动范围的初始值>, <移动的元素范围数量>}`|变量操作·变量引用·CSV引用|
|ARRAYREMOVE|`ARRAYREMOVE <目标变量>, <删除范围的初始值>, <删除的元素数>`|变量操作·变量引用·CSV引用|
|ARRAYSORT|`ARRAYSORT <目标变量>{, <排序方式（FORWARD 或 BACK）>, <起始索引>, <目标元素数>}`|变量操作·变量引用·CSV引用|
|ARRAYCOPY|`ARRAYCOPY <复制源变量名>, <复制目标变量名>`|变量操作·变量引用·CSV引用|
|CUPCHECK|`CUPCHECK <已登录角色编号>`|变量操作·变量引用·CSV引用|
|SAVEDATA|`SAVEDATA <数值表达式>, <字符串表达式>`|游戏存档的操作|
|LOADDATA|`LOADDATA <数值表达式>`|游戏存档的操作|
|DELDATA|`DELDATA <数值表达式>`|游戏存档的操作|
|CHKDATA|`CHKDATA <数值表达式>`|游戏存档的操作|
|SAVENOS|`SAVENOS <数值变量>`|游戏存档的操作|
|SAVEGLOBAL|`SAVEGLOBAL`|游戏存档的操作|
|LOADGLOBAL|`LOADGLOBAL`|游戏存档的操作|
|OUTPUTLOG|`OUTPUTLOG`|游戏存档的操作|
|GETTIME|`GETTIME`|日期·时间的获取|
|GETMILLISECOND|`GETMILLISECOND`|日期·时间的获取|
|GETSECOND|`GETSECOND`|日期·时间的获取|
|FORCEWAIT|`FORCEWAIT`|输入·等待|
|INPUT|`INPUT {<数值>}`|输入·等待|
|INPUTS|`INPUTS {<字符串>}`|输入·等待|
|TINPUT|`TINPUT <数值>, <数值>{, <数值>, <字符串>}`|输入·等待|
|TINPUTS|`TINPUTS <数值>, <字符串表达式>{, <数值>, <字符串>}`|输入·等待|
|TWAIT|`TWAIT <数值>, <数值>`|输入·等待|
|ONEINPUT|`ONEINPUT {<数值>}`|输入·等待|
|ONEINPUTS|`ONEINPUTS {<字符串>}`|输入·等待|
|TONEINPUT|`TONEINPUT <数值>, <数值>{, <数值>, <字符串>}`|输入·等待|
|TONEINPUTS|`TONEINPUTS <数值>, <字符串表达式>{, <数值>, <字符串>}`|输入·等待|
|WAITANYKEY|`WAITANYKEY`|输入·等待|
|FOR|`FOR <数值型变量>, <数值表达式>, <数值表达式>{, <数值表达式>}`|循环·分支语法|
|NEXT|`NEXT`|循环·分支语法|
|WHILE|`WHILE <数值表达式>`|循环·分支语法|
|WEND|`WEND`|循环·分支语法|
|DO|`DO`|循环·分支语法|
|LOOP|`LOOP <数值表达式>`|循环·分支语法|
|SELECTCASE|`SELECTCASE <式>`|循环·分支语法|
|CASE|`CASE <CASE条件式>(, <CASE条件式>, <CASE条件式> ……)`|循环·分支语法|
|CASEELSE|`CASEELSE`|循环·分支语法|
|ENDSELECT|`ENDSELECT`|循环·分支语法|
|RANDOMIZE|`RANDOMIZE <数值表达式>`|随机数的控制|
|DUMPRAND|`DUMPRAND`|随机数的控制|
|INITRAND|`INITRAND`|随机数的控制|
|BEGIN|`BEGIN <关键字>`|调试辅助·系统流的控制|
|CALLTRAIN|`CALLTRAIN <命令数>`|调试辅助·系统流的控制|
|DOTRAIN|`DOTRAIN <数值表达式>`|调试辅助·系统流的控制|
|THROW|`THROW <FORM 格式文本>`|调试辅助·系统流的控制|
|TRYJUMP|`TRYJUMP <字符串> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYCALL|`TRYCALL <字符串> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYGOTO|`TRYGOTO <字符串>`|CALL·JUMP·GOTO系|
|JUMPFORM|`JUMPFORM <FORM格式文本> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|CALLFORM|`CALLFORM <FORM格式文本> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|GOTOFORM|`GOTOFORM <FORM格式文本>`|CALL·JUMP·GOTO系|
|TRYJUMPFORM|`TRYJUMPFORM <FORM格式文本> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYCALLFORM|`TRYCALLFORM <FORM格式文本> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYGOTOFORM|`TRYGOTOFORM <FORM格式文本>`|CALL·JUMP·GOTO系|
|CALLF|`CALLF <字符串> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|CALLFORMF|`CALLFORMF <FORM格式文本> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|CALL|`CALL·JUMP·GOTO系2 (TRYC-CATCH-ENDCATCH)`|CALL·JUMP·GOTO系|
|TRYCJUMP|`TRYCJUMP <字符串> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYCCALL|`TRYCCALL <字符串> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYCGOTO|`TRYCGOTO <字符串>`|CALL·JUMP·GOTO系|
|TRYCJUMPFORM|`TRYCJUMPFORM <FORM格式文本> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYCCALLFORM|`TRYCCALLFORM <FORM格式文本> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|TRYCGOTOFORM|`TRYCGOTOFORM <FORM格式文本>`|CALL·JUMP·GOTO系|
|CATCH|`CATCH`|CALL·JUMP·GOTO系|
|ENDCATCH|`ENDCATCH`|CALL·JUMP·GOTO系|
|TRYCALLLIST|`TRYCALLLIST`|CALL·JUMP·GOTO系|
|TRYJUMPLIST|`TRYJUMPLIST`|CALL·JUMP·GOTO系|
|TRYGOTOLIST|`TRYGOTOLIST`|CALL·JUMP·GOTO系|
|FUNC|`FUNC <字符串> (, 参数1, 参数2……)`|CALL·JUMP·GOTO系|
|ENDFUNC|`ENDFUNC`|CALL·JUMP·GOTO系|
|RETURN|`RETURN <数值表达式>(, <数值表达式>, <数值表达式>, ...)`|RETURN系|
|RETURNFORM|`RETURNFORM <FORM格式文本>(, <FORM格式文本>, <FORM格式文本>, ...)`|RETURN系|
|RETURNF|`RETURNF <式>`|RETURN系|
|DEBUGPRINT|`DEBUGPRINT <字符串>`|DEBUG系|
|DEBUGPRINTL|`DEBUGPRINTL <字符串>`|DEBUG系|
|DEBUGPRINTFORM|`DEBUGPRINTFORM <FORM格式文本>`|DEBUG系|
|DEBUGPRINTFORML|`DEBUGPRINTFORML <FORM格式文本>`|DEBUG系|
|ASSERT|`ASSERT <数值表达式>`|DEBUG系|
|HTML_PRINT|`HTML_PRINT <字符串表达式>`|HTML系|
|HTML_TAGSPLIT|`HTML_TAGSPLIT <字符串表达式>`|HTML系|
|TOOLTIP_SETCOLOR|`TOOLTIP_SETCOLOR <数值表达式>, <数值表达式>`|工具提示系|
|TOOLTIP_SETDELAY|`TOOLTIP_SETDELAY <数值表达式>`|工具提示系|
|TOOLTIP_SETDURATION|`TOOLTIP_SETDURATION <数值表达式>`|工具提示系|
|AWAIT|`AWAIT {<时间>}`|AWAIT 相关|
|GETKEY|`GETKEY <键码>`|AWAIT 相关|
|GETKEYTRIGGERED|`GETKEYTRIGGERED <键码>`|AWAIT 相关|
|MOUSEX|`MOUSEX`|AWAIT 相关|
|MOUSEY|`MOUSEY`|AWAIT 相关|
|ISACTIVE|`ISACTIVE`|AWAIT 相关|
|GCREATE|`GCREATE <ID>, <宽度>, <高度>`|图像处理相关|
|GCREATEFROMFILE|`GCREATEFROMFILE <ID>, <文件路径>`|图像处理相关|
|GDISPOSE|`GDISPOSE <ID>`|图像处理相关|
|GCLEAR|`GCLEAR <ID>, <颜色>`|图像处理相关|
|GFILLRECTANGLE|`GFILLRECTANGLE <ID>, <x>, <y>, <宽度>, <高度>`|图像处理相关|
|GDRAWG|`GDRAWG <目标ID>, <源ID>, <目标X>, <目标Y>, <目标宽度>, <目标高度>, <源X>, <源Y>, <源宽度>, <源高度>`|图像处理相关|
|GDRAWG|`GDRAWG <目标ID>, <源ID>, <目标X>, <目标Y>, <目标宽度>, <目标高度>, <源X>, <源Y>, <源宽度>, <源高度>, <颜色矩阵>`|图像处理相关|
|GDRAWGWITHMASK|`GDRAWGWITHMASK <目标ID>, <源ID>, <掩码ID>, <目标X>, <目标Y>`|图像处理相关|
|GDRAWSPRITE|`GDRAWSPRITE <ID>, <精灵名>`|图像处理相关|
|GDRAWSPRITE|`GDRAWSPRITE <ID>, <精灵名>, <目标X>, <目标Y>`|图像处理相关|
|GDRAWSPRITE|`GDRAWSPRITE <ID>, <精灵名>, <目标X>, <目标Y>, <目标宽度>, <目标高度>`|图像处理相关|
|GDRAWSPRITE|`GDRAWSPRITE <ID>, <精灵名>, <目标X>, <目标Y>, <目标宽度>, <目标高度>, <颜色矩阵>`|图像处理相关|
|GSETCOLOR|`GSETCOLOR <ID>, <颜色>, <x>, <y>`|图像处理相关|
|GSETBRUSH|`GSETBRUSH <ID>, <颜色>`|图像处理相关|
|GSETFONT|`GSETFONT <ID>, <字体名>, <字号>`|图像处理相关|
|GSETPEN|`GSETPEN <ID>, <颜色>, <笔宽>`|图像处理相关|
|GCREATED|`GCREATED <ID>`|图像处理相关|
|GWIDTH|`GWIDTH <ID>`|图像处理相关|
|GHEIGHT|`GHEIGHT <ID>`|图像处理相关|
|GGETCOLOR|`GGETCOLOR <ID>, <x>, <y>`|图像处理相关|
|GSAVE|`GSAVE <ID>, <文件编号>`|图像处理相关|
|GLOAD|`GLOAD <ID>, <文件编号>`|图像处理相关|
|SPRITECREATE|`SPRITECREATE <精灵名>, <Graphics ID>`|图像处理相关|
|SPRITECREATE|`SPRITECREATE <精灵名>, <Graphics ID>, <x>, <y>, <宽度>, <高度>`|图像处理相关|
|SPRITEANIMECREATE|`SPRITEANIMECREATE <精灵名>, <宽度>, <高度>`|图像处理相关|
|SPRITEANIMEADDFRAME|`SPRITEANIMEADDFRAME <精灵名>, <Graphics ID>, <x>, <y>, <宽度>, <高度>, <偏移X>, <偏移Y>, <延迟>`|图像处理相关|
|SPRITEDISPOSE|`SPRITEDISPOSE <精灵名>`|图像处理相关|
|SPRITEGETCOLOR|`SPRITEGETCOLOR <精灵名>, <x>, <y>`|图像处理相关|
|SPRITECREATED|`SPRITECREATED <精灵名>`|图像处理相关|
|SPRITEWIDTH|`SPRITEWIDTH <精灵名>`|图像处理相关|
|SPRITEHEIGHT|`SPRITEHEIGHT <精灵名>`|图像处理相关|
|SPRITEPOSX|`SPRITEPOSX <精灵名>`|图像处理相关|
|SPRITEPOSY|`SPRITEPOSY <精灵名>`|图像处理相关|
|SPRITESETPOS|`SPRITESETPOS <精灵名>, <位置X>, <位置Y>`|图像处理相关|
|SPRITEMOVE|`SPRITEMOVE <精灵名>, <移动X>, <移动Y>`|图像处理相关|
|CBGSETG|`CBGSETG <Graphics ID>, <x>, <y>, <z深度>`|图像处理相关|
|CBGSETSPRITE|`CBGSETSPRITE <精灵名>, <x>, <y>, <z深度>`|图像处理相关|
|CBGCLEAR|`CBGCLEAR`|图像处理相关|
|CBGCLEARBUTTON|`CBGCLEARBUTTON`|图像处理相关|
|CBGREMOVERANGE|`CBGREMOVERANGE <z最小值>, <z最大值>`|图像处理相关|
|CBGREMOVEBMAP|`CBGREMOVEBMAP`|图像处理相关|
|CBGSETBMAPG|`CBGSETBMAPG <Graphics ID>`|图像处理相关|
|CBGSETBUTTONSPRITE|`CBGSETBUTTONSPRITE <按钮值>, <精灵名>, <选中精灵名>, <x>, <y>, <z深度>`|图像处理相关|
|CBGSETBUTTONSPRITE|`CBGSETBUTTONSPRITE <按钮值>, <精灵名>, <选中精灵名>, <x>, <y>, <z深度>, <工具提示>`|图像处理相关|
|SETANIMETIMER|`SETANIMETIMER <时间>`|图像处理相关|
|CLEARTEXTBOX|`CLEARTEXTBOX`|未整理项目|
|STRDATA|`STRDATA <赋值目标字符串变量>`|未整理项目|
|STOPCALLTRAIN|`STOPCALLTRAIN`|未整理项目|

## 输出与显示 ​

|命令|说明|

|`PRINT`|输出一行（不换行）|
|`PRINTL`|输出并换行|
|`PRINTS` / `PRINTV` / `PRINTFORM`|输出字符串 / 数值 / 格式化字符串|
|`PRINTC`|按指定宽度对齐输出|
|`PRINTBUTTON`|生成可点击按钮|
|`PRINTDATA`|随机显示一组文本|
|`CLEARLINE`|删除若干行|
|`DRAWLINE`|绘制分隔线|

→ PRINT 系列

## 显示、字体与颜色 ​

|命令|说明|

|`SETCOLOR` / `RESETCOLOR`|设置 / 重置文字颜色|
|`SETBGCOLOR` / `RESETBGCOLOR`|设置 / 重置背景颜色|
|`SETCOLORBYNAME`|用颜色名设置文字颜色|
|`FONTBOLD` / `FONTITALIC` / `FONTREGULAR`|加粗 / 倾斜 / 恢复|
|`FONTSTYLE`|设置文字样式|
|`SETFONT` / `CHKFONT`|设置 / 检查字体|
|`ALIGNMENT`|设置对齐方式|
|`REDRAW`|控制重绘|

→ 显示处理

## 字符串操作 ​

|命令|说明|

|`TOSTR` / `TOINT`|数值与字符串互转|
|`STRLEN` / `STRLENS`|求字符串长度|
|`SUBSTRING` / `CHARATU`|截取子串 / 取字符|
|`STRFIND` / `STRCOUNT`|查找 / 统计|
|`SPLIT` / `REPLACE`|分割 / 替换|
|`UNICODE` / `ENCODETOUNI`|Unicode 与编码|

→ 字符串操作

## 算术 ​

|命令|说明|

|`ABS` / `SIGN` / `SQRT`|绝对值 / 符号 / 平方根|
|`MAX` / `MIN` / `LIMIT` / `INRANGE`|最大 / 最小 / 限制 / 区间判断|
|`GETBIT` / `SETBIT` / `CLEARBIT` / `INVERTBIT`|位操作|
|`POWER`|乘方|

→ 算术

## 角色操作 ​

|命令|说明|

|`ADDCHARA`|添加角色|
|`DELCHARA` / `DELALLCHARA`|删除单个 / 全部角色|
|`SWAPCHARA` / `SORTCHARA`|交换 / 排序|
|`GETCHARA` / `FINDCHARA`|查找角色|
|`COPYCHARA` / `ADDCOPYCHARA`|复制角色|
|`EXISTCSV`|判断角色是否已定义|

→ 角色操作

## 变量与 CSV 引用 ​

|命令|说明|

|`VARSIZE`|取数组大小|
|`VARSET` / `CVARSET`|批量赋值|
|`ARRAYSHIFT` / `ARRAYREMOVE` / `ARRAYSORT` / `ARRAYCOPY`|数组操作|
|`RESETDATA` / `RESETGLOBAL`|初始化数据|
|`CSVNAME` / `CSVBASE` / `CSVABL` 等|直接读取 CSV 数据|
|`GETNUM`|由名称取编号|

→ 变量操作

## 存档 ​

|命令|说明|

|`SAVEDATA` / `LOADDATA`|直接保存 / 读取指定槽位|
|`SAVEGAME` / `LOADGAME`|呼出标准存档 / 读取界面|
|`CHKDATA` / `DELDATA`|检查 / 删除存档|
|`SAVEGLOBAL` / `LOADGLOBAL`|保存 / 读取全局变量|
|`OUTPUTLOG`|输出日志|

→ 游戏存档

## 输入与等待 ​

|命令|说明|

|`INPUT` / `INPUTS`|读取数值 / 字符串|
|`TINPUT` / `TINPUTS`|带时间限制的输入|
|`ONEINPUT` / `ONEINPUTS`|只接受一个字符|
|`TWAIT` / `WAITANYKEY`|等待 / 任意键|

→ 输入与等待

## 流程控制 ​

|命令|说明|

|`IF` / `ELSEIF` / `ELSE` / `ENDIF`|条件分支|
|`REPEAT` / `REND`、`FOR` / `NEXT`、`WHILE` / `WEND`、`DO` / `LOOP`|循环|
|`SELECTCASE` / `CASE` / `CASEELSE` / `ENDSELECT`|多路分支|
|`CONTINUE` / `BREAK`|继续 / 跳出循环|
|`GOTO` / `JUMP` / `CALL`|跳转 / 调用|
|`RETURN` / `RETURNFORM` / `RETURNF`|返回|

→ 循环·分支语法、CALL·JUMP·GOTO 系、RETURN 系

## 随机数 ​

|命令|说明|

|`RANDOMIZE`|以指定值初始化随机数|
|`DUMPRAND` / `INITRAND`|保存 / 恢复随机数状态|

→ 随机数的控制

## 系统与调试 ​

|命令|说明|

|`BEGIN`|切换游戏流程|
|`DOTRAIN` / `CALLTRAIN`|强制训练 / 连续训练|
|`THROW` / `ASSERT`|主动报错|
|`DEBUGPRINT` 等|调试输出|

→ 调试辅助、DEBUG 系

## 其他 ​

|分类|链接|

|日期与时间|日期·时间的获取|
|HTML|HTML 系|
|工具提示|工具提示系|
|图像处理|图像处理相关|

>
各命令的参数类型（`<数式>`、`<字符串表达式>` 等）见值类型规范。

Pager
Previous pageERB 的表达式

Next pageERB 的语句

GPL-3.0+ Licensed

Copyright © 2021-Present Miswanting
