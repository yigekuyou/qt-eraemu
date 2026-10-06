# UPDATECHECK

- **类别**：EE 扩展命令
- **签名**：
  - `UPDATECHECK`
- **文档来源**：`eraTW/README集/EmueraEE Readme/EmueraEE_readme.txt`（・UPDATECHECK，约 248-258 行，含完整使用说明与返回值表）；`EmueraEE_readme (English).txt:125-131`；`EmueraEE_changelog.txt:150,156`。ecd 文档未收录；zh 套件未收录。

## 语义

检查游戏是否有新版本。使用前提（由 era 侧配置，非脚本参数）：

1. 在 `GameBase.csv` 中添加「バージョン名」（当前版本名，字符串）和「バージョン情報URL」（版本信息 URL）两项；
2. 在该 URL 上放置一个文本文件：第 1 行为最新版本名，第 2 行为最新版链接，第 3 行起为自由注释；
3. 执行 `UPDATECHECK` 时访问该 URL，把最新版本名与 `GameBase.csv` 中的版本名比较：相同则 `RESULT = 0` 后结束；不同则弹出对话框询问玩家是否打开服务器侧链接。

返回值（写入 `RESULT`）：

| 值 | 含义 |
|---|---|
| 0 | 已是最新版，什么都不做 |
| 1 | 玩家在对话框选「否」，不做任何事 |
| 2 | 玩家选「是」，已用浏览器打开链接 |
| 3 | 各种失败（URL 未配置、读取不到版本名/链接、打开失败等） |
| 4 | 配置「UPDATECHECKを許可しない」为开，未执行检查 |
| 5 | 无网络连接（11fix 追加） |

## 用法

### `UPDATECHECK`
- 无参数；结果写入 `RESULT`。
```erb
UPDATECHECK
SELECTCASE RESULT
    CASE 0
        PRINTL 已是最新版本
    CASE 2
        PRINTL 已打开下载页
    CASEELSE
        PRINTL 检查失败或被跳过（RESULT={RESULT}）
ENDSELECT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/BuiltInFunctionCode.cs:369`（enum）；`Runtime/Script/Statements/FunctionIdentifier.cs:416`（`addFunction(FunctionCode.UPDATECHECK, new UPDATECHECK_Instruction())`）
- 实现：`Runtime/Script/Statements/Instraction.Child.cs:2833`（`UPDATECHECK_Instruction`）；相关配置与数据：`Runtime/Config/ConfigData.cs:122`（`ForbidUpdateCheck`，配置项「UPDATECHECKを許可しない」/「Disallow UPDATECHECK」）、`Runtime/Script/Data/GameBase.cs:25-28`（`UpdateCheckURL`/`VersionName` 字段）、`Runtime/Script/Data/GameBase.cs:163-167`（解析 GameBase.csv 的「バージョン情報URL」「バージョン名」键）

```text
构造:
    ArgBuilder = VOID（无参数）
    flag = METHOD_SAFE | EXTENDED

DoInstruction(exm, func, state):
    若 Config.ForbidUpdateCheck == true:        # 配置禁止
        RESULT = 4; 返回
    若 NetworkInterface.GetIsNetworkAvailable() == false:
        RESULT = 5; 返回                        # 无网络（11fix）
    url = GlobalStatic.GameBaseData.UpdateCheckURL
    若 url 为 null 或 "": RESULT = 3; 返回
    try:
        st = WebClient.OpenRead(url); sr = StreamReader(st)
        try:
            version = sr.ReadLine()             # 第 1 行：最新版本名
            link    = sr.ReadLine()             # 第 2 行：最新版链接
            若 version 为空 → RESULT = 3; 返回
            若 link    为空 → RESULT = 3; 返回
            若 version != GameBaseData.VersionName:      # 有新版
                弹 MessageBox（YesNo，默认按钮 = Button2「否」）
                若选 Yes:  RESULT = 2; Process.Start(链接)（UseShellExecute）; 关流; 返回
                否则:      RESULT = 1; 关流; 返回
            否则:                                # 已是最新
                RESULT = 0; 关流; 返回
        catch: RESULT = 3; 关流; 返回
    catch: RESULT = 3; 返回
```

## 备注

- readme 说「最新版の場合はRESULTに0が入って終了する」等，与源码各返回值一一对应；文档未列的细节：对话框默认焦点在「否」（`MessageBoxDefaultButton.Button2`）。
- 源码里 URL 访问和后续处理各包一层 try/catch，任何 IO 异常都归并为 `RESULT = 3`，与 readme「何らかの理由で失敗した場合は3」一致。
- GameBase.csv 的键名为 Shift-JIS 全角「バージョン名」「バージョン情報URL」；本地 emuera.config 中可见 `_default.config` 有「UPDATECHECKを許可しない:NO」默认项。
- 该命令依赖网络与外部资源，且 `WebClient` 为同步访问——调用时会阻塞游戏直到请求完成。
