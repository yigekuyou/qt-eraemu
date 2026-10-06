# SAVENOS

- **类别**：式中函数（另有同名命令形态）
- **签名**：int SAVENOS()
- **文档来源**：`ecd/Expression.md`（表达式内函数签名列表）；`ecd/Command.md`「### SAVENOS `<数值变量>`」（命令形态）

## 语义

无参数式中函数，返回当前配置中「显示的存档数量」（表示するセーブデータ数 / Save data count per page）设置的值，即存档界面每页显示的存档位数量。命令文档给出的默认值为 20。

同名命令形态 `SAVENOS <数值变量>` 把该配置值赋值给指定的数值变量（变量不能省略），语义与函数形态相同，只是结果写入变量而不是作为表达式值。本仓库中它同时通过 `Runtime/Script/Statements/FunctionIdentifier.cs` 的 SP_GETINT 通道注册为命令形态（Process.ScriptProc.cs:560）。本文档以式中函数形态为主。

## 用法

### int SAVENOS()
- 无参数（`()` 必须写，以区分变量）。
- 返回值：配置项「显示的存档数量」（默认 20）。
```erb
FOR LOCAL, 0, SAVENOS()
	; 逐个处理每个存档位
NEXT
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:67`（`["SAVENOS"] = new GetSaveNosMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:2869`（`GetSaveNosMethod`）

```text
构造：返回类型 = long；参数列表 = []；CanRestructure = true（允许常量折叠）。

GetIntValue(exm, args):
    返回 Config.SaveDataNos
```

`Config.SaveDataNos` 来自配置项 `SaveDataNos`（`Runtime/Config/ConfigData.cs:91`，默认 20）；`Runtime/Config/Config.cs:204-212` 中加载后会把该值夹紧到 20～80 范围。

## 备注

- ecd/Command.md 小节描述的是命令形态（赋值给数值变量）；ecd/Expression.md 只给出签名 `int SAVENOS()`。两者读取同一配置，语义一致。
- 源码会把配置值夹紧到 20～80；文档只提到默认 20，未提夹紧范围。
- zh 套件未收录本函数。
