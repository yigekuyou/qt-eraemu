# C# Emuera 默认配置

> 本文记录参考实现 `emuera.em/Emuera/Runtime/Config/ConfigData.cs` 的默认配置，作为 Qt/C++ 移植的语义基线。键名保持 C# 日文键名；移植实现可以采用不同的命令行约定，但配置语义不能擅自改名。

## 主配置默认值

| 配置键 | 类型 | C# 默认值 |
|---|---|---|
| 大文字小文字の違いを無視する | bool | YES |
| _Rename.csvを利用する | bool | NO |
| _Replace.csvを利用する | bool | YES |
| マウスを使用する | bool | YES |
| メニューを使用する | bool | YES |
| デバッグコマンドを使用する | bool | NO |
| 多重起動を許可する | bool | YES |
| オートセーブを行なう | bool | YES |
| キーボードマクロを使用する | bool | YES |
| ウィンドウの高さを可変にする | bool | YES |
| 描画インターフェース | enum | TEXTRENDERER |
| ウィンドウ幅 | int | 760 |
| ウィンドウ高さ | int | 480 |
| ウィンドウ位置X | int | 0 |
| ウィンドウ位置Y | int | 0 |
| 起動時のウィンドウ位置を指定する | bool | NO |
| 起動時にウィンドウを最大化する | bool | NO |
| 履歴ログの行数 | int | 5000 |
| PRINTCを並べる数 | int | 3 |
| PRINTCの文字数 | int | 25 |
| フォント名 | string | ＭＳ ゴシック |
| フォントサイズ | int | 18 |
| 一行の高さ | int | 19 |
| 文字色 | Color | (192,192,192) |
| 背景色 | Color | (0,0,0) |
| 選択中文字色 | Color | (255,255,0) |
| 履歴文字色 | Color | (192,192,192) |
| フレーム毎秒 | int | 5 |
| 最大スキップフレーム数 | int | 3 |
| スクロール行数 | int | 1 |
| 無限ループ警告までのミリ秒数 | int | 5000 |
| 表示する最低警告レベル | int | 1 |
| ロード時にレポートを表示する | bool | NO |
| ロード時に引数を解析する | enum | NO |
| 呼び出されなかった関数を無視する | bool | YES |
| 関数が見つからない警告の扱い | enum | IGNORE |
| 関数が呼び出されなかった警告の扱い | enum | IGNORE |
| デバッグコマンドを使用した時にMASTERの名前を変更する | bool | YES |
| ボタンの途中で行を折りかえさない | bool | NO |
| サブディレクトリを検索する | bool | NO |
| 読み込み順をファイル名順にソートする | bool | NO |
| 最終更新コード | long | 0 |
| 表示するセーブデータ数 | int | 20 |
| eramaker互換性に関する警告を表示する | bool | YES |
| システム関数の上書きを許可する | bool | YES |
| システム関数が上書きされたとき警告を表示する | bool | YES |
| 関連づけるテキストエディタ | string | notepad |
| テキストエディタコマンドライン指定 | enum | USER_SETTING |
| エディタに渡す行指定引数 | string | 空文字列 |
| 同名の非イベント関数が複数定義されたとき警告する | bool | NO |
| 解釈不可能な行があっても実行する | bool | NO |
| CALLNAMEが空文字列の時にNAMEを代入する | bool | NO |
| セーブデータをsavフォルダ内に作成する | bool | NO |
| 擬似変数RANDの仕様をeramakerに合わせる | bool | NO |
| DRAWLINEを常に新しい行で行う | bool | NO |
| 関数・属性については大文字小文字を無視しない | bool | NO |
| 全角スペースをホワイトスペースに含める | bool | YES |
| ver1739以前の非ボタン折り返しを再現する | bool | NO |
| 内部で使用する東アジア言語 | enum | JAPANESE |
| ONEINPUT系命令でマウスによる2文字以上の入力を許可する | bool | NO |
| イベント関数のCALLを許可する | bool | NO |
| SPキャラを使用する | bool | NO |
| セーブデータをバイナリ形式で保存する | bool | NO |
| ユーザー関数の全ての引数の省略を許可する | bool | NO |
| ユーザー関数の引数に自動的にTOSTRを補完する | bool | NO |
| FORM中の三連記号を展開しない | bool | NO |
| TIMESの計算をeramakerにあわせる | bool | NO |
| キャラクタ変数の引数を補完しない | bool | NO |
| 文字列変数の代入に文字列式を強制する | bool | NO |
| UPDATECHECKを許可しない | bool | NO |
| ERD機能を利用する | bool | YES |
| VARSIZEの次元指定をERD機能に合わせる | bool | NO |
| ERDで定義した識別子とローカル変数の重複を確認する | bool | NO |
| 行連結の改行コードの置換文字列 | string | " " |
| 外部プラグインが有効時に警告を表示する | bool | YES |
| セーブデータを圧縮して保存する | bool | NO |
| CONFIGファイルの内容を英語で保存する | bool | NO |
| Emueraの表示言語 | string | 空文字列 |
| Emueraのアイコンのパス | string | 空文字列 |
| 表示したテキストをクリップボードにコピーする | bool | NO |
| テキスト中の<>タグを無視する | bool | NO |
| <>を次の文で置き換える | string | . |
| 新しい行のみコピーする | bool | YES |
| 画面のリフレッシュ時にクリップボードとバッファを消去する | bool | NO |
| 左クリックをトリガーにする | bool | YES |
| ホイールクリックをトリガーにする | bool | NO |
| ダブルクリックをトリガーにする | bool | NO |
| WAITをトリガーにする | bool | NO |
| INPUTをトリガーにする | bool | YES |
| クリップボードに貼り付ける行数 | int | 25 |
| 総バッファサイズ | int | 300 |
| スクロールの行数 | int | 5 |
| クリップボードの更新間隔(ミリ秒) | int | 800 |
| Rikaichanを使用する | bool | NO |
| Rikaichanのファイルパス | string | Emuera-Rikai-edict.txt-eucjp |
| ポップアップの背景色 | Color | (0,0,139) |
| ポップアップの文字色 | Color | (255,255,255) |
| 翻訳中の語句を強調表示する | bool | YES |
| Ctrl-Zで元に戻す機能を有効にする | bool | NO |

## 调试配置默认值

`ConfigData.cs` 的 `debugArray` 默认值如下。它们属于调试配置文件，不等于主配置数组：

| 配置键 | 类型 | C# 默认值 |
|---|---|---|
| 起動時にデバッグウインドウを表示する | bool | YES |
| デバッグウインドウを最前面に表示する | bool | YES |
| デバッグウィンドウ幅 | int | 400 |
| デバッグウィンドウ高さ | int | 300 |
| デバッグウィンドウ位置を指定する | bool | NO |
| デバッグウィンドウ位置X | int | 0 |
| デバッグウィンドウ位置Y | int | 0 |

## 命令行差异

C# 参考实现的 `Program.cs` 使用 `-Debug` 及其别名。Qt/C++ Linux 移植统一采用 `--debug`，详见 [`test/change/language/命令行选项.md`](../../change/language/命令行选项.md)。
