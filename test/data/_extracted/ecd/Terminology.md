术语中英日对照 | Era 中文文档

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

# 术语中英日对照 ​

Era 生态里的术语大多来自日文，而英文资料、变量名、`CSV` 字段又往往各用一套叫法。查文档时，“同一个东西为什么有三种名字”是最常见的困惑之一。

本页给出一张 中文 / 英文 / 日文 对照表，可以按任意一种语言搜索。

共 85 条

|中文|English|日本語|说明|

|普通模式|Normal Mode|通常モード|直接双击 exe 启动的模式|
|解析模式|Analysis Mode|解析モード|拖入文件启动，做语法检查|
|调试模式|Debug Mode|デバッグモード|以 `-debug` 参数启动|
|引擎|Engine|エンジン|运行游戏的程序本体|
|改造版|Modded build|私家改造版|在 Emuera 基础上增加功能的版本|
|主窗口|Main Window|メインウィンドウ||
|主控制台|Main Console|メインコンソール|主窗口中输入输出的部分|
|调试窗口|Debug Window|デバッグウィンドウ||
|调试控制台|Debug Console|デバッグコンソール||
|设置对话框|Configuration Dialog|設定ダイアログ||
|剪贴板对话框|Clipboard Dialog|クリップボードダイアログ|`Ctrl+C` 打开|
|命令|Command|命令|如 `PRINT`、`WAIT`|
|函数|Function|関数|以 `@名称` 定义、用 `CALL` 调用|
|事件函数|Event Function|イベント関数|由引擎在特定时机调用|
|表达式内函数|In-expression function|式中関数|可在表达式中直接调用|
|内置函数|Built-in function|組み込み関数|引擎自带，无需定义|
|用户定义函数|User-defined function|ユーザー定義関数||
|参数|Argument / Parameter|引数|`ARG` / `ARGS`|
|返回值|Return value|返り値|放在 `RESULT` / `RESULTS`|
|调用|Call|呼び出し||
|预处理指令|Preprocessor|プリプロセッサ|以 `#` 开头的行|
|属性|Attribute|属性|如 `#PRI`、`#FUNCTION`|
|定义|Definition|定義|如 `#DIM`、`#DEFINE`|
|宏|Macro|マクロ|`#DEFINE` 的字符串替换|
|头文件|Header file|ヘッダーファイル|扩展名 `.ERH`|
|行|Line|行|物理行|
|语句|Statement|文|一个处理单位|
|表达式|Expression|式|能算出结果的东西|
|数值表达式|Numeric expression|数式|结果为数值|
|字符串表达式|String expression|文字列式|结果为字符串|
|格式化字符串|Formatted string|書式付文字列|FORM 语法|
|赋值|Assignment|代入|`变量 = 值`|
|注释|Comment|コメント|`;`|
|标签|Label|ラベル|`$名称`|
|行连接|Line continuation|行連結|`{ }` 跨行|
|变量|Variable|変数||
|伪变量|Pseudo variable|擬似変数|如 `RAND`、`CHARANUM`|
|数组变量|Array variable|配列変数|如 `FLAG`、`STR`|
|角色变量|Character variable|キャラクタ変数|第一维是角色编号|
|双重数组变量|Double array variable|二重配列変数|既是角色变量又是数组变量|
|多维数组变量|Multidimensional array|多次元配列変数|如 `DA`、`TA`|
|局部变量|Local variable|ローカル変数|`LOCAL` / `LOCALS`|
|广域变量|Nonlocal variable|広域変数|所有函数共享|
|全局变量|Global variable|グローバル変数|跨存档共享|
|私有变量|Private variable|プライベート変数|`#DIM` 定义，仅本函数可见|
|引用型变量|Reference variable|参照型変数|`REF`|
|维度|Dimension|次元||
|元素数|Number of elements|要素数||
|索引 / 下标|Index|添字|从 0 开始|
|数值|Integer / Number|数値|64 位有符号整数|
|字符串|String|文字列||
|文本|Text|テキスト||
|全角 / 半角|Full-width / Half-width|全角 / 半角||
|角色编号|Character number|番号|`番号` → `NO`|
|名字|Name|名前|`名前` → `NAME`|
|称呼|Call name|呼び名|`呼び名` → `CALLNAME`|
|昵称|Nickname|あだ名|`あだ名` → `NICKNAME`|
|基础值|Base|基礎|`基礎` → `BASE` / `MAXBASE`|
|能力|Ability|能力|`能力` → `ABL`|
|素质 / 天赋|Talent|素質|`素質` → `TALENT`|
|经验|Experience|経験|`経験` → `EXP`|
|刻印|Mark|刻印|`刻印` → `MARK`|
|相性 / 关系|Relation|相性|`相性` → `RELATION`|
|角色标志|Character flag|フラグ|`フラグ` → `CFLAG`|
|助手|Assistant|助手|`助手` → `ISASSI`|
|物品|Item|アイテム|`Item.csv` → `ITEM`|
|训练命令|Train command|調教コマンド|`Train.csv` → `TRAINNAME`|
|字体名|Font name|フォント名||
|字号|Font size|フォントサイズ||
|行高|Line height|一行の高さ||
|文字色|Text color|文字色||
|背景色|Background color|背景色||
|对齐|Alignment|位置揃え|`LEFT` / `CENTER` / `RIGHT`|
|绘制接口|Drawing interface|描画インターフェース|`WINAPI` / `GRAPHICS` / `TEXTRENDERER`|
|按钮|Button|ボタン|`[0]` 形式，可点击|
|精灵|Sprite|スプライト||
|资源|Resource|リソース|`resources/` 中的图片等|
|存档|Save|セーブ||
|读档|Load|ロード||
|存档槽|Save slot|セーブデータ||
|全局存档|Global save|グローバルセーブ|`SAVEGLOBAL`|
|存档简介|Save comment|セーブデータのコメント|`@SAVEINFO` 生成|
|兼容性开关|Compatibility option|互換性オプション|让 Emuera 模仿 Eramaker|
|警告等级|Warning level|警告レベル|0～3|
|无限循环|Infinite loop|無限ループ|超过时限且无 `WAIT` 时警告|

## 使用建议 ​

- 查 `CSV` 字段：日文列就是写进 `CSV/` 的原文（如 `名前`、`素質`），中文列是它的含义，说明列里给出了对应的变量名（如 `NAME`、`TALENT`）。
- 读英文资料：英文列可以帮你在英文 wiki / 源码里对上号。
- 看源码：`.ref/Emuera` 里的类名、变量名多为英文，可参考Emuera 源码分析。

## 相关链接 ​

- 术语表（官方翻译）
- CSV 文件参考
- ERB 的变量
- 配置项详解

Pager
Previous page兼容性矩阵

Next page错误索引表

GPL-3.0+ Licensed

Copyright © 2021-Present Miswanting
