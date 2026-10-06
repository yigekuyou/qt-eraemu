错误索引表 | Era 中文文档

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

# 错误索引表 ​

本页汇总 Emuera 常见的错误与警告信息。前几节是按含义整理的常见问题；页面末尾还嵌入了从引擎源码提取的完整错误信息索引，可以按日文原文搜索。

>
报错时，除了画面上的信息，根目录下的 `emuera.log` 里还有更详细的记录（文件名、行号、函数名等）。排错方法的教程版见错误与异常。

## 运行错误 ​

在游戏运行过程中出现。

|错误信息（原文）|含义|常见原因与对策|

|`関数"@XXX"が見つかりません`|找不到函数 `@XXX`|函数名拼错、忘了定义，或定义在未被读取的文件里|
|`予期しないスクリプト終端です`|预料之外的脚本结尾|函数或复合语句没有写完，检查出错位置附近|
|`配列の範囲外です`|访问了数组范围之外的位置|数组从 0 开始；检查下标是否越界或为负|
|`...はキャラ登録番号の範囲外です`|角色编号超出范围|检查 `ADDCHARA` / 角色变量使用的编号|
|`代入文の左辺に変更できない変数を指定することはできません`|不能给不可修改的变量赋值|不要给常量、伪变量或只读变量赋值|
|`'='の前後で型が一致しません`|赋值号两边类型不同|字符串变量要用 `#DIMS` 定义，值要加引号|
|`ASSERT文の引数が0です`|`ASSERT` 断言失败|前提条件不成立；检查触发该断言的数据|
|`...は数値として解釈できません`|无法作为数值解释|检查该处是否应当传入字符串|
|`...が64ビット符号付き整数の範囲外です`|数值超出 64 位整数范围|检查计算是否溢出，或输入的数值过大|

## 代码错误（语法与结构） ​

在加载脚本、解析语法时出现。

|错误信息（原文）|含义|常见原因与对策|

|`IFに対応するENDIFが設定されていない`|缺少与 `IF` 配对的 `ENDIF`|补齐 `ENDIF`；用编辑器的折叠功能检查嵌套|
|`SELECTCASEに対応するENDSELECTが設定されていない`|`SELECTCASE` 没有对应的 `ENDSELECT`|补齐 `ENDSELECT`|
|`IF～ENDIFの外で...`|在 `IF～ENDIF` 之外使用了只能在内部使用的命令|把该命令移进 `IF～ENDIF` 之内|
|`REPEAT, FOR, WHILE, DOの中以外で...`|在循环之外使用了 `BREAK` / `CONTINUE` 等|只在循环内使用循环控制|
|`ELSE文より後で...` / `CASEELSE文より後で...`|在 `ELSE` / `CASEELSE` 之后使用了不该用的命令|调整顺序|
|`SIF文の次の行がありません`|`SIF` 后面没有下一行|在 `SIF` 下一行写上要执行的语句|
|`SIF文の次の行をラベル行にすることはできません`|`SIF` 的下一行是标签|调整代码顺序|
|`SIF文の次の行が空行またはコメント行です(eramaker:SIF文は意味を失います)`|`SIF` 的下一行是空行 / 注释|删掉中间的空行、注释|
|`REPEAT文が入れ子にされています（無限ループの恐れがあります）`|`REPEAT` 嵌套异常|检查 `REPEAT` / `REND` 是否配对|
|`'('に対応する')'が見つかりません`|缺少配对的 `)`|检查括号是否成对|
|`'['に対応する']'が見つかりません`|缺少配对的 `]`|检查方括号是否成对|
|`'{\'が使われましたが対応する\'}\'が見つかりません`|行连接 `{` 没有闭合|补齐 `}`|
|`'%'が使われましたが対応する'%'が見つかりません`|FORM 的 `%` 没有闭合|补齐成对的 `%`|
|`'?と'#'の数が正しく対応していません`|三元的 `?` 与 `#` 数量不匹配|检查三元表达式的写法|
|`$で始まるラベルに引数が設定されています`|标签后面写了参数|标签不能带参数|
|`@の使い方が不正です`|`@` 用法不正确|检查函数定义行|
|`DATALISTが閉じられていません`|`DATALIST` 没有闭合|补齐 `ENDLIST`|

## 警告 ​

警告不会中断运行，但往往预示着潜在问题。

|警告信息（原文）|含义|建议|

|`#FUNCTIONで始まる関数の戻り値に文字列型が指定されました`|`#FUNCTION` 函数返回了字符串|数值函数应返回数值|
|`#FUCNTIONSで始まる関数の戻り値に数値型が指定されました`|`#FUNCTIONS` 函数返回了数值|字符串函数应返回字符串|
|`#PRIと#LATERが重複して使われています(この関数は2度呼ばれます)`|同时指定 `#PRI` 与 `#LATER`|该函数会被调用两次，确认是否本意|
|`#ONLYが指定されたイベント関数では#PRIは機能しません`|`#ONLY` 使 `#PRI` 失效|不要同时使用这两个属性|
|`#ONLYが指定されたイベント関数では#LATERは機能しません`|`#ONLY` 使 `#LATER` 失效|同上|
|`#ONLYが指定されたイベント関数では#SINGLEは機能しません`|`#ONLY` 使 `#SINGLE` 失效|同上|
|`#PRIが重複して使われています`|`#PRI` 重复|每个函数只写一处|
|`この関数にはすでに#LOCALSIZEが定義されています。（以前の定義は無視されます）`|重复定义 `#LOCALSIZE`|每个函数只保留一处|
|`（構文上の注意）比較演算子が連続しています。`|比较运算符连续出现|检查表达式是否写错|
|`[]の使い方が不正です`|`[]` 用法不正确|检查特殊区块的写法|
|`[SKIPSTART]が重複して使用されています`|`[SKIPSTART]` 重复|检查区块是否嵌套错误|
|`[SKIPSTART]と対応しない[SKIPEND]です`|`[SKIPEND]` 没有对应的 `[SKIPSTART]`|补齐配对|

## 完整错误信息索引 ​

下面是从 Emuera 参考工程源码（`ParserMediator.Warn` / `CodeEE` / `ExeEE` / `PrintError`）中提取的全部错误与警告文案，共 500 余条。由于数量庞大，这里保持日文原文（你实际看到的就是这些），并标注级别与来源文件，方便搜索定位。

- 级别：`警告`（不中断）、`错误`（中断当前脚本）、`致命`（无法恢复）。
- 想了解某条信息的含义与处理，可先在上面的常见表中查找；更深入的可对照开发中的源码导读。

全部级别警告错误致命错误共 572 条

|原文|级别|来源|

|","が必要です|警告|`GameData/ConstantData.cs`|
|","で始まっています|警告|`GameData/ConstantData.cs`|
|（構文上の注意）比較演算子が連続しています。|警告|`GameData/Expression/ExpressionParser.cs`|
|[]の使い方が不正です|警告|`GameProc/ErbLoader.cs`|
|[SKIPSTART]が重複して使用されています|警告|`GameProc/ErbLoader.cs`|
|[SKIPSTART]と対応しない[SKIPEND]です|警告|`GameProc/ErbLoader.cs`|
|#FUCNTIONSで始まる関数の戻り値に数値型が指定されました|警告|`GameProc/Function/Instraction.Child.cs`|
|#FUNCTIONで始まる関数の戻り値に文字列型が指定されました|警告|`GameProc/Function/Instraction.Child.cs`|
|#LATERが重複して使われています|警告|`GameProc/LogicalLineParser.cs`|
|#ONLYが指定されたイベント関数では#LATERは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|#ONLYが指定されたイベント関数では#PRIは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|#ONLYが指定されたイベント関数では#SINGLEは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|#ONLYが重複して使われています|警告|`GameProc/LogicalLineParser.cs`|
|#PRIが重複して使われています|警告|`GameProc/LogicalLineParser.cs`|
|#PRIと#LATERが重複して使われています(この関数は2度呼ばれます)|警告|`GameProc/LogicalLineParser.cs`|
|#SINGLEが重複して使われています|警告|`GameProc/LogicalLineParser.cs`|
|#の識別子の後に余分な文字があります|警告|`GameProc/LogicalLineParser.cs`|
|$で始まるラベルに引数が設定されています|警告|`GameProc/LogicalLineParser.cs`|
|CASEELSE文より後で|警告|`GameProc/ErbLoader.cs`|
|CASEの引数がありません|警告|`GameProc/ErbLoader.cs`|
|CASEの引数の型がSELECTCASEと一致しません|警告|`GameProc/ErbLoader.cs`|
|DATALISTが閉じられていません|警告|`GameProc/ErbLoader.cs`|
|DATALIST構文に使用できない命令\'|警告|`GameProc/ErbLoader.cs`|
|DATALIST命令に表示データが与えられていません（このDATALISTは空文字列を表示します）|警告|`GameProc/ErbLoader.cs`|
|ELSE文より後で|警告|`GameProc/ErbLoader.cs`|
|EVENT関数中にCALLEVENT命令は使用できません|警告|`GameProc/Function/Instraction.Child.cs`|
|GAMEBASE.CSVの読み込み中にエラーが発生したため、読みこみを中断します|警告|`GameData/GameBase.cs`|
|IF～ENDIFの外で|警告|`GameProc/ErbLoader.cs`|
|NOSKIP系命令が入れ子にされています|警告|`GameProc/ErbLoader.cs`|
|PALAMNAMEの要素数がJUELより少なくなっています（JUELに合わせます）|警告|`GameData/ConstantData.cs`|
|PALAMとJUELとPALAMNAMEの要素数が不適切です|警告|`GameData/ConstantData.cs`|
|PRINTDATA系命令が入れ子にされています|警告|`GameProc/ErbLoader.cs`|
|PRINTDATA系命令の中にSTRDATA系命令が含まれています|警告|`GameProc/ErbLoader.cs`|
|REPEAT, FOR, WHILE, DOの中以外で|警告|`GameProc/ErbLoader.cs`|
|REPEAT文が入れ子にされています（無限ループの恐れがあります）|警告|`GameProc/ErbLoader.cs`|
|REPEAT文の中でカウンタ変数にCOUNT:0を用いたFORが使われています（無限ループの恐れがあります）|警告|`GameProc/ErbLoader.cs`|
|RETURNFは#FUNCTION以外では使用できません|警告|`GameProc/Function/Instraction.Child.cs`|
|SELECTCASE～ENDSELECTの外で|警告|`GameProc/ErbLoader.cs`|
|SELECTCASEの引数がありません|警告|`GameProc/ErbLoader.cs`|
|SELECTCASE構文の分岐の外に命令\'|警告|`GameProc/ErbLoader.cs`|
|SIF文の次の行がありません|警告|`GameProc/Function/Instraction.Child.cs`|
|SIF文の次の行が空行またはコメント行です(eramaker:SIF文は意味を失います)|警告|`GameProc/Function/Instraction.Child.cs`|
|SIF文の次の行を|警告|`GameProc/Function/Instraction.Child.cs`|
|SIF文の次の行をラベル行にすることはできません|警告|`GameProc/Function/Instraction.Child.cs`|
|STRDATA系命令の中にPRINTDATA系命令が含まれています|警告|`GameProc/ErbLoader.cs`|
|STRDATA命令が入れ子にされています|警告|`GameProc/ErbLoader.cs`|
|TRYCALLLIST系命令が入れ子にされています|警告|`GameProc/ErbLoader.cs`|
|TRYCALLLIST系命令中に無効な|警告|`GameProc/ErbLoader.cs`|
|TRYGOTOLISTの呼び出し対象に[～～]が設定されています|警告|`GameProc/ErbLoader.cs`|
|TRYGOTOLISTの呼び出し対象に引数が設定されています|警告|`GameProc/ErbLoader.cs`|
|アニメーションスプライトのサイズが宣言されていません|警告|`Content/AppContents.cs`|
|アニメーションスプライトのサイズの指定が適切ではありません|警告|`Content/AppContents.cs`|
|アニメーションスプライトのフレームの追加に失敗しました:|警告|`Content/AppContents.cs`|
|イベント関数@|警告|`GameProc/ErbLoader.cs`|
|イベント関数では#|警告|`GameProc/LogicalLineParser.cs`|
|イベント関数以外では#LATERは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|イベント関数以外では#ONLYは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|イベント関数以外では#PRIは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|イベント関数以外では#SINGLEは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|カウンタ変数にCOUNT:|警告|`GameProc/ErbLoader.cs`|
|カウンタ変数にCOUNT:0を用いたFOR文の中でREPEATが呼び出されています|警告|`GameProc/ErbLoader.cs`|
|コード:0のセーブデータはいかなるコードのスクリプトからも読めるデータとして扱われます|警告|`GameData/GameBase.cs`|
|コード中でローカル変数を@付きで呼ぶことは推奨されません(代わりに*.ERHファイルの利用を検討してください)|警告|`GameData/IdentifierDictionary.cs`|
|このイベント関数"@|警告|`GameProc/LogicalLineParser.cs`|
|このイベント関数には#LATERが宣言されていますが無視されます|警告|`GameProc/LogicalLineParser.cs`|
|このイベント関数には#PRIが宣言されていますが無視されます|警告|`GameProc/LogicalLineParser.cs`|
|このイベント関数には#SINGLEが宣言されていますが無視されます|警告|`GameProc/LogicalLineParser.cs`|
|このバリアント動作させるにはVer.|警告|`GameData/GameBase.cs`|
|この関数にはすでに#LOCALSIZEが定義されています。（以前の定義は無視されます）|警告|`GameProc/LogicalLineParser.cs`|
|この関数にはすでに#LOCALSSIZEが定義されています。（以前の定義は無視されます）|警告|`GameProc/LogicalLineParser.cs`|
|システム関数|警告|`GameData/Variable/VariableLocal.cs`|
|システム関数@|警告|`GameProc/ErbLoader.cs`|
|システム関数に#|警告|`GameProc/LogicalLineParser.cs`|
|スプライトの高さ又は幅には正の値のみ指定できます:|警告|`Content/AppContents.cs`|
|ディフォルトエラー（Emuera設定漏れ）|警告|`GameProc/ErbLoader.cs`|
|バージョン指定を読み取れなかったので処理を省略します|警告|`GameData/GameBase.cs`|
|フレーム表示時間には正の値のみ指定できます:|警告|`Content/AppContents.cs`|
|プログラムミス|警告|`GameData/ConstantData.cs`|
|ラベル名$|警告|`GameProc/ErbLoader.cs`|
|ローカル変数でない一次元配列のサイズを100未満にはできません|警告|`GameData/ConstantData.cs`|
|ローカル変数のサイズを1未満にはできません|警告|`GameData/ConstantData.cs`|
|一つ目の値を整数値に変換できません|警告|`GameData/ConstantData.cs`|
|一つ目の値を変数名として認識できません|警告|`GameData/ConstantData.cs`|
|一次元配列のサイズを1000000より大きくすることはできません|警告|`GameData/ConstantData.cs`|
|一次元配列のサイズ指定に不必要なデータは無視されます|警告|`GameData/ConstantData.cs`|
|画像リソースの作成に失敗しました:|警告|`Content/AppContents.cs`|
|解釈できない#行です|警告|`GameProc/HeaderFileLoader.cs`|
|関数|警告|`GameProc/LogicalLineParser.cs`|
|関数@|警告|`GameProc/ErbLoader.cs`|
|関数MOUSESKIP()は推奨されません。代わりに関数MESSKIP()を使用してください|警告|`GameData/Function/Creator.Method.cs`|
|関数が定義されるより前に行があります|警告|`GameProc/ErbLoader.cs`|
|関数宣言に引数変数"|警告|`GameData/Variable/VariableLocal.cs`|
|関数宣言の直後以外で#行が使われています|警告|`GameProc/ErbLoader.cs`|
|禁止設定された名前配列です|警告|`GameData/ConstantData.cs`|
|金額が読み取れません|警告|`GameData/ConstantData.cs`|
|作成に失敗したリソースを元にスプライトを作成しようとしました:|警告|`Content/AppContents.cs`|
|三つ目の識別子がありません|警告|`GameData/ConstantData.cs`|
|三つ目の値を整数値として認識できません|警告|`GameData/ConstantData.cs`|
|三次元配列のサイズ指定には3つの数値が必要です|警告|`GameData/ConstantData.cs`|
|三次元配列のサイズ指定に不必要なデータは無視されます|警告|`GameData/ConstantData.cs`|
|三次元配列の要素数は最大で1000万個までです|警告|`GameData/ConstantData.cs`|
|使用禁止にできない変数に対して負の配列長が指定されています|警告|`GameData/ConstantData.cs`|
|四つ目の値を整数値として認識できません|警告|`GameData/ConstantData.cs`|
|指定されたファイルの読み込みに失敗しました:|警告|`Content/AppContents.cs`|
|指定されたラベル名"$|警告|`GameProc/Function/Instraction.Child.cs`|
|指定された画像ファイルが見つかりませんでした:|警告|`Content/AppContents.cs`|
|指定された画像ファイルの大きさが大きすぎます(幅及び高さを|警告|`Content/AppContents.cs`|
|指定された関数名"@|警告|`GameProc/Function/Instraction.Child.cs`|
|式中関数では#LATERは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|式中関数では#ONLYは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|式中関数では#PRIは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|式中関数では#SINGLEは機能しません|警告|`GameProc/LogicalLineParser.cs`|
|親画像の範囲外を参照しています:|警告|`Content/AppContents.cs`|
|対応する|警告|`GameProc/ErbLoader.cs`|
|対応する[IF]のない[ENDIF]です|警告|`GameProc/ErbLoader.cs`|
|対応するCATCHのないENDCATCHです|警告|`GameProc/ErbLoader.cs`|
|対応するDATALISTのないENDLISTです|警告|`GameProc/ErbLoader.cs`|
|対応するIFの無いENDIF文です|警告|`GameProc/ErbLoader.cs`|
|対応するNOSKIP系命令のない|警告|`GameProc/ErbLoader.cs`|
|対応するPRINTDATA系命令のない|警告|`GameProc/ErbLoader.cs`|
|対応するPRINTDATA系命令のないDATALISTです|警告|`GameProc/ErbLoader.cs`|
|対応するPRINTDATA系命令もしくはSTRDATAのない|警告|`GameProc/ErbLoader.cs`|
|対応するSELECTCASEの無いENDSELECT文です|警告|`GameProc/ErbLoader.cs`|
|対応するTRYCALLLIST系命令のない|警告|`GameProc/ErbLoader.cs`|
|対応するTRYC系命令がありません|警告|`GameProc/ErbLoader.cs`|
|代入演算子に"=="が使われています|警告|`GameProc/LogicalLineParser.cs`|
|第二引数に拡張子がありません:|警告|`Content/AppContents.cs`|
|同名のリソースがすでに作成されています:|警告|`Content/AppContents.cs`|
|二つ目の識別子がありません|警告|`GameData/ConstantData.cs`|
|二つ目の値を整数値として認識できません|警告|`GameData/ConstantData.cs`|
|二次元配列のサイズ指定には2つの数値が必要です|警告|`GameData/ConstantData.cs`|
|二次元配列のサイズ指定に不必要なデータは無視されます|警告|`GameData/ConstantData.cs`|
|二次元配列の要素数は最大で100万個までです|警告|`GameData/ConstantData.cs`|
|認識できないプリプロセッサです|警告|`GameProc/ErbLoader.cs`|
|配列サイズを1000000より大きくすることはできません|警告|`GameData/ConstantData.cs`|
|配列サイズを1未満にはできません|警告|`GameData/ConstantData.cs`|
|配列長に0は指定できません（変数を使用禁止にするには配列長に負の値を指定してください）|警告|`GameData/ConstantData.cs`|
|配列変数でない変数|警告|`GameData/ConstantData.cs`|
|番号|警告|`GameData/ConstantData.cs`|
|番号が定義される前に他のデータが始まりました|警告|`GameData/ConstantData.cs`|
|番号が二重に定義されました|警告|`GameData/ConstantData.cs`|
|不適切な[ELSE]です|警告|`GameProc/ErbLoader.cs`|
|不適切な[ELSEIF]です|警告|`GameProc/ErbLoader.cs`|
|変数名|警告|`GameProc/LogicalLineParser.cs`|
|予期しないエラーが発生しました|警告|`GameData/ConstantData.cs`|
|・定義が見つからなかった関数: 他のファイルで定義されている場合はこの警告は無視できます|错误|`GameProc/ErbLoader.cs`|
|','の後にRIGHT又はLEFTがありません|错误|`GameData/StrForm.cs`|
|','の後にRIGHT又はLEFT以外の単語があります|错误|`GameData/StrForm.cs`|
|'?'と'#'の数が正しく対応していません|错误|`GameData/Expression/ExpressionParser.cs`|
|'('に対応する')'が見つかりません|错误|`GameData/Expression/ExpressionParser.cs`|
|')'が閉じられていません|错误|`GameProc/HeaderFileLoader.cs`|
|'['に対応する']'が見つかりません|错误|`GameData/Expression/ExpressionParser.cs`|
|'&'と';'が連続しています|错误|`GameView/HtmlManager.cs`|
|'&'に対応する';'がみつかりません|错误|`GameView/HtmlManager.cs`|
|'<'は代入演算子として認識できません|错误|`Sub/LexicalAnalyzer.cs`|
|'='の後に式がありません|错误|`GameData/Expression/ExpressionParser.cs`|
|'='の前後で型が一致しません|错误|`GameData/Expression/ExpressionParser.cs`|
|'>'は代入演算子として認識できません|错误|`Sub/LexicalAnalyzer.cs`|
|"\'"は代入演算子として認識できません|错误|`Sub/LexicalAnalyzer.cs`|
|"が閉じられていません|错误|`Sub/LexicalAnalyzer.cs`|
|[]を使った機能はまだ実装されていません|错误|`GameData/Expression/ExpressionParser.cs`|
|{}の中に式が存在しません|错误|`GameData/StrForm.cs`|
|{}の中の式が数式ではありません|错误|`GameData/StrForm.cs`|
|@SYSTEM_TITLE以外でこの命令を使うことはできません|错误|`GameProc/Process.ScriptProc.cs`|
|@の使い方が不正です|错误|`GameData/Expression/ExpressionParser.cs`|
|＊＊＊＊＊警告＊＊＊＊＊|错误|`GameProc/ErbLoader.cs`|
|\'{\'が使われましたが対応する\'}\'が見つかりません|错误|`Sub/LexicalAnalyzer.cs`|
|\'\\@\',\'?\',\'#\'が使われましたが対応する\'\\@\'が見つかりません|错误|`Sub/LexicalAnalyzer.cs`|
|\'\\@\',\'?\'が使われましたが対応する\'#\'が見つかりません|错误|`Sub/LexicalAnalyzer.cs`|
|\'\\@\'が使われましたが対応する\'?\'が見つかりません|错误|`Sub/LexicalAnalyzer.cs`|
|\'%\'が使われましたが対応する\'%\'が見つかりません|错误|`Sub/LexicalAnalyzer.cs`|
|\'が閉じられていません|错误|`Sub/LexicalAnalyzer.cs`|
|#FUCNTION(S)が定義された関数@|错误|`GameProc/Process.CalledFunction.cs`|
|#FUNCTION(S)属性を持たない関数|错误|`GameProc/Function/Instraction.Child.cs`|
|#FUNCTIONが指定されていない関数"@|错误|`GameData/IdentifierDictionary.cs`|
|#FUNCTIONが定義されていない関数|错误|`GameData/IdentifierDictionary.cs`|
|%%の中に式が存在しません|错误|`GameData/StrForm.cs`|
|%%の中の式が文字列式ではありません|错误|`GameData/StrForm.cs`|
|</button>の前に<button>がありません|错误|`GameView/HtmlManager.cs`|
|</font>の前に<font>がありません|错误|`GameView/HtmlManager.cs`|
|</nobr>の後にテキストがあります|错误|`GameView/HtmlManager.cs`|
|</nobr>の前に<nobr>がありません|错误|`GameView/HtmlManager.cs`|
|</nonbutton>の前に<nonbutton>がありません|错误|`GameView/HtmlManager.cs`|
|</p>の後にテキストがあります|错误|`GameView/HtmlManager.cs`|
|</p>の前に<p>がありません|错误|`GameView/HtmlManager.cs`|
|<button>又は<nonbutton>が入れ子にされています|错误|`GameView/HtmlManager.cs`|
|<font>タグのpos属性の属性値が数値として解釈できません|错误|`GameView/HtmlManager.cs`|
|<nobr>が2度以上使われています|错误|`GameView/HtmlManager.cs`|
|<nobr>が行頭以外で使われています|错误|`GameView/HtmlManager.cs`|
|<nobr>が設定されていない行ではpos属性は使用できません|错误|`GameView/HtmlManager.cs`|
|<p>が2度以上使われています|错误|`GameView/HtmlManager.cs`|
|<p>が行頭以外で使われています|错误|`GameView/HtmlManager.cs`|
|<p>タグの属性名|错误|`GameView/HtmlManager.cs`|
|○一般関数:|错误|`GameProc/ErbLoader.cs`|
|○文中関数:|错误|`GameProc/ErbLoader.cs`|
|0による除算が行なわれました|错误|`GameData/Expression/OperatorMethod.cs`|
|2次元配列型変数|错误|`GameData/Variable/VariableToken.cs`|
|3次元以上のキャラ型変数を宣言することはできません|错误|`GameProc/UserDefinedVariable.cs`|
|3次元配列型変数|错误|`GameData/Variable/VariableToken.cs`|
|4次元以上の配列変数を宣言することはできません|错误|`GameProc/UserDefinedVariable.cs`|
|ALIGNMENTのキーワード"|错误|`GameProc/Process.ScriptProc.cs`|
|alignがleftでない行ではpos属性は使用できません|错误|`GameView/HtmlManager.cs`|
|ARRAYCOPY命令の２つの配列変数の型が一致していません|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYCOPY命令の２つの配列変数の次元数が一致していません|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYCOPY命令の第１引数|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYCOPY命令の第１引数"|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYCOPY命令の第２引数|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYCOPY命令の第２引数"|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYREMOVEの第２引数が負の値|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYREMOVEの第３引数が負の値|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYREMOVEは1次元配列および配列型キャラクタ変数のみに対応しています|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYRESORTは1次元配列および配列型キャラクタ変数のみに対応しています|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYSHIFTの第４引数が負の値|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYSHIFTの第５引数が負の値|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYSHIFTは1次元配列および配列型キャラクタ変数のみに対応しています|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYSORTの第３引数が負の値|错误|`GameProc/Process.ScriptProc.cs`|
|ARRAYSORTの第４引数が負の値|错误|`GameProc/Process.ScriptProc.cs`|
|ASSERT文の引数が0です|错误|`GameProc/Process.ScriptProc.cs`|
|AWAIT命令:10秒以上の待機時間|错误|`GameProc/Function/Instraction.Child.cs`|
|AWAIT命令:負の値|错误|`GameProc/Function/Instraction.Child.cs`|
|BARが長すぎます|错误|`GameData/Expression/ExpressionMediator.cs`|
|BARの最大値が正の値ではありません|错误|`GameData/Expression/ExpressionMediator.cs`|
|BARの長さが正の値ではありません|错误|`GameData/Expression/ExpressionMediator.cs`|
|BEGINのキーワード"|错误|`GameProc/Process.State.cs`|
|CALLTRAIN命令の引数の値がSELECTCOMの要素数を超えています|错误|`GameProc/Process.cs`|
|CASEの引数は省略できません|错误|`GameData/Expression/ExpressionParser.cs`|
|CBRT関数の引数に負の値が指定されました|错误|`GameData/Function/Creator.Method.cs`|
|CDFLAGの要素の取得にはCDFLAGNAME1又はCDFLAGNAME2を使用します|错误|`GameData/ConstantData.cs`|
|CDFLAGの要素数が多すぎます（CDFLAGNAME1とCDFLAGNAME2の要素数の積が100万を超えています）|错误|`GameData/ConstantData.cs`|
|CDFLAGの要素数とCDFLAGNAME1及びCDFLAGNAME2の要素数が一致していません|错误|`GameData/ConstantData.cs`|
|CMATCH関数の範囲指定がキャラクタ配列の範囲を超えています|错误|`GameData/Function/Creator.Method.cs`|
|CONSTキーワードが指定された変数を多次元配列にはできません|错误|`GameProc/UserDefinedVariable.cs`|
|CONSTキーワードが指定されていますが初期値が設定されていません|错误|`GameProc/UserDefinedVariable.cs`|
|CONVERT関数の第２引数は2, 8, 10, 16のいずれかでなければなりません|错误|`GameData/Function/Creator.Method.cs`|
|CSTRの参照可能範囲外を参照しました|错误|`GameData/Variable/VariableEvaluator.cs`|
|csv\\_Rename.csvが見つかりません|错误|`GameProc/Process.cs`|
|datフォルダーの作成に失敗しました|错误|`GameData/Variable/VariableEvaluator.cs`|
|DOTRAIN命令に0未満の値が渡されました|错误|`GameProc/Process.ScriptProc.cs`|
|DOTRAIN命令にTRAINNAMEの配列数以上の値が渡されました|错误|`GameProc/Process.ScriptProc.cs`|
|DOTRAIN命令をこの位置で実行することはできません|错误|`GameProc/Process.ScriptProc.cs`|
|DYNAMICとGLOBALキーワードは同時に指定できません|错误|`GameProc/UserDefinedVariable.cs`|
|DYNAMICとSAVEDATAキーワードは同時に指定できません|错误|`GameProc/UserDefinedVariable.cs`|
|emueraのエラー：プログラムの状態を特定できません|错误|`GameView/EmueraConsole.cs`|
|Emueraの予約語"|错误|`GameData/IdentifierDictionary.cs`|
|ENCOIDETOUNI関数の第２引数|错误|`GameData/Function/Creator.Method.cs`|
|EVENT関数の解決前にCALLEVENT命令が行われました|错误|`GameProc/Process.State.cs`|
|GETBIT関数の第２引数|错误|`GameData/Function/Creator.Method.cs`|
|GETLINESTR関数の引数が空文字列です|错误|`GameData/Function/Creator.Method.cs`|
|GETNUMBの1番目の引数("|错误|`GameData/Function/Creator.Method.cs`|
|html文字列"|错误|`GameView/HtmlManager.cs`|
|INPUTに必要な処理をNOSKIP～ENDNOSKIPで囲むか、SKIPDISP 0～SKIPDISP 1で囲ってください|错误|`GameProc/Process.ScriptProc.cs`|
|INRANGECARRAY関数の範囲指定がキャラクタ配列の範囲を超えています|错误|`GameData/Function/Creator.Method.cs`|
|ISキーワードの後に演算子がありません|错误|`GameData/Expression/ExpressionParser.cs`|
|ISキーワードの後に式がありません|错误|`GameData/Expression/ExpressionParser.cs`|
|ISキーワードの後の演算子が2項演算子ではありません|错误|`GameData/Expression/ExpressionParser.cs`|
|ISキーワードはここでは使用できません|错误|`GameData/Expression/ExpressionParser.cs`|
|LOADDATAの引数|错误|`GameProc/Process.ScriptProc.cs`|
|LOADDATAの引数に負の値|错误|`GameProc/Process.ScriptProc.cs`|
|MONEYSTR関数の第2引数の書式指定が間違っています|错误|`GameData/Function/Creator.Method.cs`|
|RANDの引数が省略されています|错误|`GameData/Variable/VariableParser.cs`|
|RANDの引数に0が与えられています|错误|`GameData/Variable/VariableParser.cs`|
|RANDの引数に0以下の値|错误|`GameData/Variable/VariableToken.cs`|
|RANDの最大値に0以下の値|错误|`GameData/Function/Creator.Method.cs`|
|RANDの最大値に最小値以下の値|错误|`GameData/Function/Creator.Method.cs`|
|REF引数は4次元以上の配列にできません|错误|`GameProc/UserDefinedFunction.cs`|
|REF引数は配列変数でなければなりません|错误|`GameProc/UserDefinedFunction.cs`|
|RIGHT又はLEFTの後に余分な文字があります|错误|`GameData/StrForm.cs`|
|SAVECHARAの第|错误|`GameProc/Function/Instraction.Child.cs`|
|SAVEDATAのセーブテキストに改行文字が与えられました（セーブデータが破損するため改行文字は使えません）|错误|`GameProc/Process.ScriptProc.cs`|
|SAVEDATAの引数|错误|`GameProc/Process.ScriptProc.cs`|
|SAVEDATAの引数に負の値|错误|`GameProc/Process.ScriptProc.cs`|
|SAVEDATA命令によるセーブ中に予期しないエラーが発生しました|错误|`GameProc/Process.ScriptProc.cs`|
|SETCOLORの引数に0未満の値が指定されました|错误|`GameProc/Process.ScriptProc.cs`|
|SETCOLORの引数に255を超える値が指定されました|错误|`GameProc/Process.ScriptProc.cs`|
|SPLITによる分割後の文字列の数が配列変数の要素数を超えています|错误|`GameProc/Process.ScriptProc.cs`|
|SPキャラ関係の機能は標準では使用できません(互換性オプション「SPキャラを使用する」をONにしてください)|错误|`GameData/Function/Creator.Method.cs`|
|SQRT関数の引数に負の値が指定されました|错误|`GameData/Function/Creator.Method.cs`|
|STATICとDYNAMICキーワードは同時に指定できません|错误|`GameProc/UserDefinedVariable.cs`|
|STATICとGLOBALキーワードは同時に指定できません|错误|`GameProc/UserDefinedVariable.cs`|
|STATICとSAVEDATAキーワードは同時に指定できません|错误|`GameProc/UserDefinedVariable.cs`|
|STRFORM関数:文字列"|错误|`GameData/Function/Creator.Method.cs`|
|SUMCARRAY関数の範囲指定がキャラクタ配列の範囲を超えています|错误|`GameData/Function/Creator.Method.cs`|
|THROW内容：|错误|`GameProc/Process.cs`|
|TOSTR関数の書式指定が間違っています|错误|`GameData/Function/Creator.Method.cs`|
|TOキーワードが2度使われています|错误|`GameData/Expression/ExpressionParser.cs`|
|TOキーワードの後に式がありません|错误|`GameData/Expression/ExpressionParser.cs`|
|TOキーワードの前後の型が一致していません|错误|`GameData/Expression/ExpressionParser.cs`|
|TOキーワードはここでは使用できません|错误|`GameData/Expression/ExpressionParser.cs`|
|UNICODE関数に範囲外の値|错误|`GameData/Function/Creator.Method.cs`|
|VARSIZEの1番目の引数("|错误|`GameData/Function/Creator.Method.cs`|
|イベント関数でない関数@|错误|`GameProc/Process.CalledFunction.cs`|
|インクリメント・デクリメントを前置・後置両方同時に使うことはできません|错误|`GameData/Expression/ExpressionParser.cs`|
|エスケープ文字\\の後に文字がありません|错误|`Sub/LexicalAnalyzer.cs`|
|エディタを開くことができませんでした|错误|`GameView/EmueraConsole.cs`|
|エラー内容：|错误|`GameProc/Process.cs`|
|オートセーブをスキップします|错误|`GameProc/Process.SystemProc.cs`|
|オートセーブ中に予期しないエラーが発生しました|错误|`GameProc/Process.SystemProc.cs`|
|かっこ"("～")"の中に式が含まれていません|错误|`GameData/Expression/ExpressionParser.cs`|
|カンマの後に有効な定数式が指定されていません|错误|`GameProc/UserDefinedVariable.cs`|
|キーワードを空には出来ません|错误|`GameData/ConstantData.cs`|
|キャラクタ二次元配列変数|错误|`GameData/Variable/VariableParser.cs`|
|キャラクタ配列変数|错误|`GameData/Variable/VariableEvaluator.cs`|
|キャラクタ変数|错误|`GameData/Variable/VariableParser.cs`|
|キャラ型変数にSAVEDATAフラグを付ける場合には「バイナリ型セーブ」オプションが必須です|错误|`GameProc/UserDefinedVariable.cs`|
|キャラ型変数には初期値を設定できません|错误|`GameProc/UserDefinedVariable.cs`|
|グローバルデータの保存中にエラーが発生しました|错误|`GameData/Variable/VariableEvaluator.cs`|
|この関数に引数変数"|错误|`GameData/Variable/VariableLocal.cs`|
|コピー元のキャラクタが存在しません|错误|`GameData/Variable/VariableEvaluator.cs`|
|コピー先のキャラクタが存在しません|错误|`GameData/Variable/VariableEvaluator.cs`|
|コメンdト終了タグ"-->"がみつかりません|错误|`GameView/HtmlManager.cs`|
|スクリプト実行中はコマンドを入力できません|错误|`GameView/EmueraConsole.cs`|
|セーブ中にエラーが発生しました|错误|`GameData/Variable/VariableEvaluator.cs`|
|セーブ中に予期しないエラーが発生しました|错误|`GameProc/Process.SystemProc.cs`|
|ソートキーが配列外を参照しています|错误|`GameData/Variable/CharacterData.cs`|
|タイマー系命令の待ち時間中はコマンドを入力できません|错误|`GameView/EmueraConsole.cs`|
|タグ終端'>'が見つかりません|错误|`GameView/HtmlManager.cs`|
|データがありません|错误|`GameProc/Process.SystemProc.cs`|
|デバッグウインドウは-Debug引数付きで起動したときのみ使えます|错误|`GameView/EmueraConsole.cs`|
|デバッグコマンドで使用できるのは代入文か命令文だけです|错误|`GameView/EmueraConsole.cs`|
|デバッグコマンドを使用できない設定になっています|错误|`GameView/EmueraConsole.cs`|
|プライベート変数|错误|`GameData/IdentifierDictionary.cs`|
|フロー制御命令は使用できません|错误|`GameView/EmueraConsole.cs`|
|ヘッダーの中に#で始まらない行があります|错误|`GameProc/HeaderFileLoader.cs`|
|マクロ|错误|`Sub/LexicalAnalyzer.cs`|
|マクロの展開数が1文あたりの上限|错误|`Sub/LexicalAnalyzer.cs`|
|マクロの展開数が1文あたりの上限を超えました(自己参照・循環参照のおそれ)|错误|`Sub/LexicalAnalyzer.cs`|
|マクロの展開数が1文あたりの上限値|错误|`Sub/LexicalAnalyzer.cs`|
|ユーザー定義変数のサイズは1以上1000000以下でなければなりません|错误|`GameProc/UserDefinedVariable.cs`|
|リソースファイルのロード中にエラーが発生しました|错误|`Content/AppContents.cs`|
|ローカル変数でない変数|错误|`GameData/IdentifierDictionary.cs`|
|ローカル変数の宣言に|错误|`GameProc/UserDefinedVariable.cs`|
|一次元配列変数|错误|`GameData/Variable/VariableParser.cs`|
|引数のない関数参照|错误|`GameData/Function/UserDefinedMethodTerm.cs`|
|引数の解析中にエラーが発生しました|错误|`GameProc/UserDefinedFunction.cs`|
|引数の解析中に予期しないトークン|错误|`GameProc/UserDefinedFunction.cs`|
|引数の値が適切な範囲外です|错误|`GameProc/Function/Instraction.Child.cs`|
|引数を0未満にできません|错误|`GameData/Function/Creator.Method.cs`|
|何も参照していない関数参照|错误|`GameData/Function/UserDefinedMethodTerm.cs`|
|解釈不能なコードです|错误|`GameView/EmueraConsole.cs`|
|括弧が閉じられていません|错误|`GameProc/UserDefinedFunction.cs`|
|関数"@|错误|`GameProc/Function/Instraction.Child.cs`|
|関数の呼び出しスタックが溢れました(無限に再帰呼び出しされていませんか？)|错误|`GameProc/Process.cs`|
|関数の終端でEmueraのエラーが発生しました:|错误|`GameProc/Process.cs`|
|関数の終端でエラーが発生しました:|错误|`GameProc/Process.cs`|
|関数の終端で予期しないエラーが発生しました:|错误|`GameProc/Process.cs`|
|関数型マクロの引数を0個にすることはできません|错误|`GameProc/HeaderFileLoader.cs`|
|関数型マクロは宣言できません|错误|`GameProc/HeaderFileLoader.cs`|
|関数形式のマクロ|错误|`Sub/LexicalAnalyzer.cs`|
|関数呼び出しスタック：|错误|`GameProc/Process.cs`|
|関数定義の引数は省略できません|错误|`GameData/Expression/ExpressionParser.cs`|
|関数名"|错误|`GameData/IdentifierDictionary.cs`|
|擬似変数|错误|`GameData/Variable/VariableToken.cs`|
|空の[[]]です|错误|`Sub/LexicalAnalyzer.cs`|
|空文字列によるDRAWLINEが行われました|错误|`GameView/EmueraConsole.Print.cs`|
|計算結果|错误|`GameData/Function/Creator.Method.cs`|
|計算値が非数値です|错误|`GameData/Function/Creator.Method.cs`|
|計算値が無限大です|错误|`GameData/Function/Creator.Method.cs`|
|現在の関数：@|错误|`GameProc/Process.cs`|
|呼び出された変数"|错误|`GameData/IdentifierDictionary.cs`|
|後置の単項演算子が複数存在しています|错误|`GameData/Expression/ExpressionParser.cs`|
|広域変数の宣言に|错误|`GameProc/UserDefinedVariable.cs`|
|構文を式として解釈できません|错误|`GameData/Expression/ExpressionParser.cs`|
|構文解釈中に予期しない記号'|错误|`GameData/Expression/ExpressionParser.cs`|
|構文解析中に予期しない')'を発見しました|错误|`GameData/Expression/ExpressionParser.cs`|
|構文解析中に予期しない']'を発見しました|错误|`GameData/Expression/ExpressionParser.cs`|
|行連結始端記号'{'が使われましたが終端記号'}'が見つかりません|错误|`Sub/EraStreamReader.cs`|
|行連結始端記号'{'の行に'{'以外の文字を含めることはできません|错误|`Sub/EraStreamReader.cs`|
|行連結終端記号'}'の行に'}'以外の文字を含めることはできません|错误|`Sub/EraStreamReader.cs`|
|三項演算子\\@の第一オペランドが異常です|错误|`GameData/StrForm.cs`|
|三項演算子の使用法が不正です|错误|`GameData/Expression/OperatorMethod.cs`|
|三次元配列|错误|`GameData/Variable/VariableToken.cs`|
|三次元配列変数|错误|`GameData/Variable/VariableParser.cs`|
|参照可能範囲外を参照しました|错误|`GameData/Variable/VariableEvaluator.cs`|
|参照型変数|错误|`GameData/Variable/VariableToken.cs`|
|参照型変数にはサイズを指定できません(サイズを省略するか0を指定してください)|错误|`GameProc/UserDefinedVariable.cs`|
|参照型変数には初期値を設定できません|错误|`GameProc/UserDefinedVariable.cs`|
|指定されたファイル"|错误|`GameData/Variable/VariableEvaluator.cs`|
|指定された色名"|错误|`GameData/Function/Creator.Method.cs`|
|字句解析中に対応する'('のない')'を発見しました|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に対応する')'のない'('を発見しました|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に対応する'['のない']'を発見しました|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に対応する']'のない'['を発見しました|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に置換(rename)できない符号|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に予期しない全角スペースを発見しました(この警告はシステムオプション「|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に予期しない文字'|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に予期しない文字'[['を発見しました|错误|`Sub/LexicalAnalyzer.cs`|
|字句解析中に予期しない文字"[["を発見しました|错误|`Sub/LexicalAnalyzer.cs`|
|式が異常です|错误|`GameData/Expression/ExpressionParser.cs`|
|式の結果が数値ではありません|错误|`GameData/Expression/ExpressionParser.cs`|
|式の結果が文字列ではありません|错误|`GameData/Expression/ExpressionParser.cs`|
|式の数が不足しています|错误|`GameData/Expression/ExpressionParser.cs`|
|式中で代入演算子'='が使われています(等価比較には'=='を使用してください)|错误|`GameData/Expression/ExpressionParser.cs`|
|式中関数|错误|`GameProc/Function/Instraction.Child.cs`|
|式中関数"@|错误|`GameProc/Function/Instraction.Child.cs`|
|識別子の後に引数定義がありません|错误|`GameProc/UserDefinedFunction.cs`|
|実行中の関数が存在しないため|错误|`GameData/IdentifierDictionary.cs`|
|終了タグ</|错误|`GameView/HtmlManager.cs`|
|初期値の数が配列のサイズを超えています|错误|`GameProc/UserDefinedVariable.cs`|
|書式が間違っています|错误|`GameProc/UserDefinedVariable.cs`|
|色を表す単語又は#RRGGBB値が必要です|错误|`GameView/HtmlManager.cs`|
|数字でない文字が含まれています|错误|`Domain/Config/ConfigItem.cs`|
|数字で始まるトークンが適切でありません|错误|`Sub/LexicalAnalyzer.cs`|
|数値として認識できる文字が必要です|错误|`Sub/LexicalAnalyzer.cs`|
|整数型でない変数|错误|`GameData/Variable/VariableEvaluator.cs`|
|整数型配列でない変数|错误|`GameData/Variable/VariableToken.cs`|
|設定によりシステム一文字数値変数の使用が禁止されています(呼び出された変数：|错误|`GameData/IdentifierDictionary.cs`|
|宣言の後に余分な文字があります|错误|`GameProc/UserDefinedFunction.cs`|
|属性値|错误|`GameView/HtmlManager.cs`|
|存在しないキーを参照しました|错误|`GameData/ConstantData.cs`|
|存在しないキャラクターを参照しようとしました|错误|`GameData/Variable/VariableEvaluator.cs`|
|存在しないデータを参照しようとしました|错误|`GameData/Variable/VariableEvaluator.cs`|
|存在しない登録キャラクタ|错误|`GameData/Variable/VariableEvaluator.cs`|
|存在しない登録キャラクタを参照しようとしました|错误|`GameData/Variable/VariableEvaluator.cs`|
|存在しない登録キャラクタを入れ替えようとしました|错误|`GameData/Variable/VariableEvaluator.cs`|
|多次元変数には初期値を設定できません|错误|`GameProc/UserDefinedVariable.cs`|
|対応する'?'のない'#'です|错误|`GameData/Expression/ExpressionParser.cs`|
|対応する')'のない'('です|错误|`GameData/Expression/ExpressionParser.cs`|
|対応する"]]"のない"[["です|错误|`Sub/LexicalAnalyzer.cs`|
|対応するENDNOSKIPのないNOSKIPです|错误|`GameProc/Process.ScriptProc.cs`|
|対応するNOSKIPのないENDNOSKIPです|错误|`GameProc/Process.ScriptProc.cs`|
|対数関数の引数に0以下の値が指定されました|错误|`GameData/Function/Creator.Method.cs`|
|対数関数の底に0以下の値が指定されました|错误|`GameData/Function/Creator.Method.cs`|
|第１引数が0から255の範囲外です|错误|`GameData/Function/Creator.Method.cs`|
|第１引数が色を表す整数の範囲外です|错误|`GameProc/Function/Instraction.Child.cs`|
|第２引数が0から255の範囲外です|错误|`GameData/Function/Creator.Method.cs`|
|第2引数がビットのレンジ(0から63)を超えています|错误|`GameProc/Function/Instraction.Child.cs`|
|第２引数が色を表す整数の範囲外です|错误|`GameProc/Function/Instraction.Child.cs`|
|第2引数が正規表現として不正です|错误|`GameData/Function/Creator.Method.cs`|
|第２引数が正規表現として不正です：|错误|`GameData/Function/Creator.Method.cs`|
|第３引数が0から255の範囲外です|错误|`GameData/Function/Creator.Method.cs`|
|値をColor指定子として認識できません|错误|`Domain/Config/ConfigItem.cs`|
|置換元の引数に同じ文字が2回以上使われています|错误|`GameProc/HeaderFileLoader.cs`|
|置換元の引数指定の書式が間違っています|错误|`GameProc/HeaderFileLoader.cs`|
|置換元の識別子がありません|错误|`GameProc/HeaderFileLoader.cs`|
|置換先の式がありません|错误|`GameProc/HeaderFileLoader.cs`|
|定義していないキャラクタを作成しようとしました|错误|`GameData/Variable/VariableEvaluator.cs`|
|定義していないキャラクタを参照しようとしました|错误|`GameData/Variable/VariableEvaluator.cs`|
|定数の初期値の数が配列のサイズと一致しません|错误|`GameProc/UserDefinedVariable.cs`|
|同一のキャラ登録番号|错误|`GameProc/Function/Instraction.Child.cs`|
|同一の登録キャラクタ番号|错误|`GameData/Variable/VariableEvaluator.cs`|
|読み取り専用の変数|错误|`GameData/Variable/VariableEvaluator.cs`|
|読込に失敗した行が実行されました。エラーの詳細は読込時の警告を参照してください。|错误|`GameProc/Process.ScriptProc.cs`|
|二次元配列|错误|`GameData/Variable/VariableParser.cs`|
|二次元配列変数|错误|`GameData/Variable/VariableParser.cs`|
|二進法表記の中で使用できない文字が使われています|错误|`Sub/LexicalAnalyzer.cs`|
|入れ替える変数の型が異なります|错误|`GameProc/Process.ScriptProc.cs`|
|配列でない変数|错误|`GameData/Variable/VariableParser.cs`|
|配列の初期値には定数のみ指定できます|错误|`GameProc/UserDefinedVariable.cs`|
|配列の初期値は省略できません|错误|`GameProc/UserDefinedVariable.cs`|
|配列型でない変数|错误|`GameData/Variable/VariableToken.cs`|
|配列型変数|错误|`GameData/Variable/VariableToken.cs`|
|配列型変数のキャラ変数|错误|`GameData/Variable/VariableToken.cs`|
|配列変数|错误|`GameData/ConstantData.cs`|
|非キャラクタ変数|错误|`GameData/Variable/VariableToken.cs`|
|非配列型のキャラ変数|错误|`GameData/Variable/VariableToken.cs`|
|表示スキップ中にデフォルト値を持たないINPUTに遭遇しました|错误|`GameProc/Process.ScriptProc.cs`|
|不正なデータをロードしようとしました|错误|`GameProc/Process.ScriptProc.cs`|
|不正な指定です|错误|`Domain/Config/ConfigItem.cs`|
|不正な文字で行が始まっています|错误|`Sub/LexicalAnalyzer.cs`|
|不明な変数型です|错误|`GameProc/Process.ScriptProc.cs`|
|文字列|错误|`GameProc/Function/Instraction.Child.cs`|
|文字列"|错误|`Sub/LexicalAnalyzer.cs`|
|文字列に10000以上の値|错误|`GameData/Expression/OperatorMethod.cs`|
|文字列に負の値|错误|`GameData/Expression/OperatorMethod.cs`|
|文字列型でない変数|错误|`GameData/Variable/VariableEvaluator.cs`|
|文字列型の多次元配列変数にSAVEDATAフラグを付ける場合には「バイナリ型セーブ」オプションが必須です|错误|`GameProc/UserDefinedVariable.cs`|
|文字列型配列でない変数|错误|`GameData/Variable/VariableToken.cs`|
|閉じられていないタグがあります|错误|`GameView/HtmlManager.cs`|
|変更できない変数をインクリメントすることはできません|错误|`GameData/Expression/OperatorMethod.cs`|
|変数|错误|`GameData/Variable/VariableTerm.cs`|
|変数の:の後に引数がありません|错误|`GameData/Expression/ExpressionParser.cs`|
|変数の引数の読み取り中に予期しない演算子を発見しました|错误|`GameData/Expression/ExpressionParser.cs`|
|変数の型と初期値の型が一致していません|错误|`GameProc/UserDefinedVariable.cs`|
|変数以外をインクリメントすることはできません|错误|`GameData/Expression/OperatorMethod.cs`|
|変数名"|错误|`GameData/IdentifierDictionary.cs`|
|未実装の機能です|错误|`GameProc/UserDefinedVariable.cs`|
|無限ループに入る可能性が高いため実行を終了します|错误|`GameProc/Process.ScriptProc.cs`|
|無限ループの疑いにより強制終了が選択されました|错误|`GameProc/Process.cs`|
|無色透明(Transparent)は色として指定できません|错误|`GameData/Function/Creator.Method.cs`|
|命令ARRAYREMOVEの第２引数|错误|`GameData/Variable/VariableEvaluator.cs`|
|命令ARRAYSHIFTの第４引数|错误|`GameData/Variable/VariableEvaluator.cs`|
|命令ARRAYSORTの第３引数|错误|`GameData/Variable/VariableEvaluator.cs`|
|命令CVARSETにキャラクタ変数でない変数|错误|`GameProc/Function/Instraction.Child.cs`|
|命令CVARSETの第４引数|错误|`GameProc/Function/Instraction.Child.cs`|
|命令CVARSETの第５引数|错误|`GameProc/Function/Instraction.Child.cs`|
|命令FORCEKANAの引数が指定可能な範囲(0～3)を超えています|错误|`GameData/Expression/ExpressionMediator.cs`|
|命令PICKUPCHARAの第|错误|`GameProc/Process.ScriptProc.cs`|
|命令名"|错误|`GameData/IdentifierDictionary.cs`|
|予期しないスクリプト終端です|错误|`GameProc/Process.SystemProc.cs`|
|予期しないマクロ名"|错误|`GameData/IdentifierDictionary.cs`|
|予期しない演算子を発見しました|错误|`GameProc/UserDefinedVariable.cs`|
|予期しない括弧です|错误|`GameProc/UserDefinedFunction.cs`|
|予期しない行連結始端記号'{'が見つかりました|错误|`Sub/EraStreamReader.cs`|
|予期しない行連結終端記号'}'が見つかりました|错误|`Sub/EraStreamReader.cs`|
|予期しない全角スペースを発見しました(この警告はシステムオプション「|错误|`Sub/LexicalAnalyzer.cs`|
|予期しない代入演算子'='を発見しました(等価比較には'=='を使用してください)|错误|`Sub/LexicalAnalyzer.cs`|
|累乗結果|错误|`GameData/Function/Creator.Method.cs`|
|累乗結果が非数値です|错误|`GameData/Function/Creator.Method.cs`|
|累乗結果が無限大です|错误|`GameData/Function/Creator.Method.cs`|
|ARRAY2DとARRAY1Dは排他|致命|`GameData/Variable/VariableIdentifier.cs`|
|ARRAY2DにはEXTENDEDフラグ必須|致命|`GameData/Variable/VariableIdentifier.cs`|
|CALCとSAVE_EXTENDEDは排他|致命|`GameData/Variable/VariableIdentifier.cs`|
|CanForbidでない変数"|致命|`GameData/IdentifierDictionary.cs`|
|CASEチェック中。引数が解析されていない。|致命|`GameProc/Function/Instraction.Child.cs`|
|ClientSize:|致命|`GameData/Function/Creator.Method.cs`|
|Emuera.exeは次に実行する行を見失いました|致命|`GameProc/Process.ScriptProc.cs`|
|GetConfigValueのCodeまたは型が不適切|致命|`Domain/Config/ConfigData.cs`|
|GLOBALにはEXTENDEDフラグ必須|致命|`GameData/Variable/VariableIdentifier.cs`|
|GraphicsState:|致命|`GameData/Function/Creator.Method.cs`|
|IFチェック中。引数が解析されていない。|致命|`GameProc/Function/Instraction.Child.cs`|
|IFに対応するENDIFが設定されていない|致命|`GameProc/Function/Instraction.Child.cs`|
|IFのIF-ELSEIFリストが適正に作成されていない|致命|`GameProc/Function/Instraction.Child.cs`|
|INTEGERとSTRINGのどちらかは必須|致命|`GameData/Variable/VariableIdentifier.cs`|
|INTEGERとSTRINGは排他|致命|`GameData/Variable/VariableIdentifier.cs`|
|LOCALにはEXTENDEDフラグ必須|致命|`GameData/Variable/VariableIdentifier.cs`|
|PRINTDATA異常|致命|`GameProc/Function/Instraction.Child.cs`|
|PRINT異常|致命|`GameProc/Function/Instraction.Child.cs`|
|READONLYでないCALC変数の代入処理が設定されていない|致命|`GameData/Variable/VariableEvaluator.cs`|
|ReturnFと#FUNCTIONのチェックがおかしい|致命|`GameProc/Process.State.cs`|
|SAVE_EXTENDEDにはEXTENDEDフラグ必須|致命|`GameData/Variable/VariableIdentifier.cs`|
|SELECTCASEに対応するENDSELECTが設定されていない|致命|`GameProc/Function/Instraction.Child.cs`|
|SELECTCASEのCASEリストが適正に作成されていない|致命|`GameProc/Function/Instraction.Child.cs`|
|SpriteAnime:最終フレームが範囲外|致命|`Content/CroppedImage.cs`|
|SpriteAnime:時間外参照|致命|`Content/CroppedImage.cs`|
|SpriteStateMethod:|致命|`GameData/Function/Creator.Method.cs`|
|STRINGかつARRAY2DのSAVE_EXTENDEDは未実装|致命|`GameData/Variable/VariableIdentifier.cs`|
|totaltime > 0なのにFrameListが空|致命|`Content/CroppedImage.cs`|
|UNCHANGEABLEとSAVE_EXTENDEDは排他|致命|`GameData/Variable/VariableIdentifier.cs`|
|イベント関数でない関数"@|致命|`GameProc/Process.SystemProc.cs`|
|エラー投げ損ねた|致命|`GameData/Expression/ExpressionParser.cs`|
|キャラクタ変数でない|致命|`GameData/Variable/CharacterData.cs`|
|ソート順序不明|致命|`GameData/Variable/VariableEvaluator.cs`|
|ファイルのロード中に予期しないエラーが発生しました|致命|`GameProc/Process.ScriptProc.cs`|
|マクロ解決失敗|致命|`GameData/Expression/ExpressionParser.cs`|
|異常なCONTINUE|致命|`GameProc/Function/Instraction.Child.cs`|
|異常な指定|致命|`GameProc/Function/ArgumentBuilder.cs`|
|異常な状態|致命|`GameProc/Process.SystemProc.cs`|
|異常な配列|致命|`GameData/Function/Creator.Method.cs`|
|異常な分岐|致命|`GameData/Function/Creator.Method.cs`|
|異常な変数宣言|致命|`GameData/Variable/VariableData.cs`|
|異常な名前|致命|`GameData/Function/Creator.Method.cs`|
|何かおかしい|致命|`GameData/StrForm.cs`|
|何か変？|致命|`GameProc/ErbLoader.cs`|
|関数が複数ある|致命|`GameProc/Process.State.cs`|
|記憶している状態があるのに再度記憶しようとした|致命|`GameProc/Process.ScriptProc.cs`|
|記憶している状態がないのに呼び戻しされた|致命|`GameProc/Process.ScriptProc.cs`|
|空のストリームを渡された|致命|`GameData/Expression/ExpressionParser.cs`|
|型が一致しない|致命|`Domain/Config/ConfigItem.cs`|
|型チェックは呼び出し元が行うこと|致命|`GameData/Expression/OperatorMethod.cs`|
|型不明なコンフィグ|致命|`Domain/Config/ConfigItem.cs`|
|項の種別が異常|致命|`GameData/Expression/Term.cs`|
|使用中のオブジェクトを別用途に再利用しようとした|致命|`Sub/EraStreamReader.cs`|
|実行中の関数が存在しません|致命|`GameProc/Process.State.cs`|
|実行中関数がない|致命|`GameProc/Process.State.cs`|
|実装されていない|致命|`GameProc/Function/Instruction.cs`|
|存在しないウィンドウにアクセスした|致命|`GameView/EmueraConsole.cs`|
|存在しないパスを呼び出した|致命|`GameData/Variable/VariableEvaluator.cs`|
|存在しない名称を取得しようとした|致命|`GameData/Variable/VariableEvaluator.cs`|
|定義されていない種類の行です|致命|`GameProc/Process.ScriptProc.cs`|
|破棄したオブジェクトを再利用しようとした|致命|`Sub/EraStreamReader.cs`|
|描画モード不明|致命|`GameView/StringMeasure.cs`|
|不正な呼び出し|致命|`GameProc/Process.SystemProc.cs`|
|不正な時期の呼び出し|致命|`GameData/Expression/ExpressionParser.cs`|
|不適当なBEGIN呼び出し|致命|`GameProc/Process.State.cs`|
|文字列分割異常|致命|`GameView/ConsoleButtonString.cs`|
|未実装 or 呼び出しミス|致命|`GameProc/Function/Instruction.cs`|
|未実装？|致命|`GameData/Function/FunctionMethod.cs`|
|未定義の関数|致命|`GameData/Variable/VariableEvaluator.cs`|
|未定義の関数です|致命|`GameProc/Process.ScriptProc.cs`|
|未定義の状態|致命|`GameProc/Process.SystemProc.cs`|
|戻り値の型が違う|致命|`GameData/StrForm.cs`|
|戻り値の型が違う or 未実装|致命|`GameData/Function/FunctionMethod.cs`|

## 相关链接 ​

- 错误与异常（教程）
- 调试模式
- 函数与预处理指令
- ERB 的变量

Pager
Previous page术语中英日对照

Next page版本特性索引

GPL-3.0+ Licensed

Copyright © 2021-Present Miswanting
