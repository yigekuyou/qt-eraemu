ERB 的内置流程 | Era 中文文档

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

# ERB 的内置流程 ​

Emuera 内部有一套固定的游戏流程。它会在特定时机自动调用名字特定的函数——我们要做的，就是把逻辑填进这些“框架函数”里。教程版见内置流程，完整的流程图见程序流程。

## 引擎自动调用的函数 ​

|函数|调用时机|用途|

|`@SYSTEM_TITLE`|CSV 加载完毕、执行 `BEGIN TITLE` 时|标题画面|
|`@TITLE_LOADGAME`|标准标题画面选择“读取存档”时|自定义读取画面|
|`@EVENTFIRST`|新游戏开始（`BEGIN FIRST`）时|初始化、开场|
|`@SHOW_SHOP`|进入商店/主循环时|主界面与操作菜单|
|`@EVENTTRAIN`|开始训练时|训练前的准备|
|`@SHOW_STATUS`|需要显示状态时|状态栏|
|`@SHOW_USERCOM`|显示指令菜单时|列出可选指令|
|`@USERCOM`|玩家选择指令后|处理选择|
|`@EVENTCOM`|执行指令时|指令的具体效果|
|`@EVENTCOMEND`|指令结束后|收尾处理|
|`@EVENTEND`|游戏结束时|结算画面|
|`@SYSTEM_AUTOSAVE`|自动保存时|自定义自动存档内容|
|`@EVENTLOAD`|存档读取完毕后|读档后的处理|
|`@CALLTRAINEND`|`CALLTRAIN` 自动执行结束后|收尾|

>
`@CALLTRAINEND` 不是事件函数，不能多重定义。其余带 `EVENT` 前缀的多为事件函数，可以定义多个，全部都会被调用。

## 用 BEGIN 切换流程 ​

`BEGIN` 是推进游戏流程的命令：

|关键字|含义|

|`BEGIN FIRST`|从头开始|
|`BEGIN TITLE`|回到标题画面|
|`BEGIN TRAIN`|开始训练|
|`BEGIN AFTERTRAIN`|结束训练|
|`BEGIN ABLUP`|进入升级界面|
|`BEGIN TURNEND`|结束回合|

调用 `BEGIN` 会终止当前函数的执行，且不会返回原来的函数。

## 流程概览 ​

下面这张图是交互式的，点击节点即可跳到对应条目：

启动

@SYSTEM_TITLE标题画面。未定义时使用引擎自带标题。

↓

新游戏 / 读取存档

从头开始执行 BEGIN FIRST。@EVENTFIRST新游戏初始化、开场。@TITLE_LOADGAME在标准标题画面选择“读取存档”时调用。@EVENTLOAD存档读取完毕后调用。

↓

主循环

@SHOW_SHOP进入商店 / 主界面，玩家在这里反复操作。

↓

训练循环

@EVENTTRAIN开始训练时调用。→@SHOW_STATUS显示状态栏。→@SHOW_USERCOM显示可选指令菜单。→@USERCOM玩家选择指令后处理。→@EVENTCOM执行指令的具体效果。→@EVENTCOMEND指令结束后的收尾。

↓

结束

@EVENTEND游戏结束时调用。

## 最小示例 ​

erb
```
`; 不定义 @SYSTEM_TITLE，直接使用引擎自带的标题画面

@EVENTFIRST
  PRINTL 欢迎来到我的游戏！
  WAIT
  BEGIN TITLE`
```

## 相关链接 ​

- 内置流程（教程）
- 程序流程
- 函数与预处理指令
- ERB 的复合语句

Pager
Previous pageERB 的复合语句

Next page兼容性矩阵

GPL-3.0+ Licensed

Copyright © 2021-Present Miswanting
