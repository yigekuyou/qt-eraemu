# STRFORM

- **类别**：式中函数
- **签名**：str STRFORM(form)
- **文档来源**：`ecd/Expression.md` 未收录；`ecd/Command.md` 未收录（仅有 `ecd/Error_Index.md` 记录其报错文案）。语义依据源码与官方 wiki（exmeth「式中で使用できる関数」）补写

## 语义

把参数字符串当作 `PRINTFORM` 系的 FORM 格式文本当场展开并返回结果字符串。即在字符串表达式中，对含有 `{数值表达式}`、`{字符表达式,LEFT/RIGHT,宽度}`、`%%表达式%%`、`\@条件 ? A # B\@` 等格式语法的字符串求值，展开时使用当前执行上下文中的变量。效果上相当于把 `PRINTFORM` 的输出捕获为字符串（但不会打印、不影响控制台）。

参数只有一个；格式语法本身解析或其中的表达式求值出错时，抛出运行时错误（`CodeEE`）。

也可以写成独立命令形式 `STRFORM <字符串表达式>`，结果存入 `RESULTS:0`。

## 用法

### str STRFORM(form)

- `form`：含 FORM 格式语法的字符串表达式。

```erb
A = 5
STR = STRFORM("A = {A}, 双倍 = {A * 2}")      ; STR = "A = 5, 双倍 = 10"
LOCALS = STRFORM(@"\@ A >= 5 ? 不少于五 # 不足五 \@")   ; 条件展开
PRINTFORML %STRFORM("{A,3,RIGHT}")%           ; 与 PRINTFORM 相同的宽度/对齐语法
```

## 源码实现（emuera.em/Emuera）

- 注册：`Runtime/Script/Statements/Function/Creator.cs:136`（`["STRFORM"] = new StrFormMethod()`）
- 实现：`Runtime/Script/Statements/Function/Creator.Method.cs:4887`（`public sealed class StrFormMethod`）；格式解析依赖 `Runtime/Script/Data/StrForm.cs`（`LexicalAnalyzer.AnalyseFormattedString` / `StrForm`）

```text
函数 STRFORM(form):
    try:
        wt   = AnalyseFormattedString(CharStream(form), FormStrEndWith.EoL, false)
        strForm = StrForm.FromWordToken(wt)
        返回 strForm.GetString(exm)          ; 在当前上下文中展开 {} %% \@...\@
    catch CodeEE e:
        throw CodeEE("STRFORM関数:文字列\"form\"の展開エラー:" + e.Message)
    catch:
        throw CodeEE("STRFORM関数:文字列\"form\"の展開処理中にエラーが発生しました")

常量折叠（UniqueRestructure）:
    对参数做 Restructure
    若参数不是确值(SingleTerm)或 const 字符串变量 → 不可折叠
    若解析出的 StrForm 不是常量(IsConst == false，如引用普通变量) → 不可折叠
    否则可在编译期折叠为单一字符串
```

## 备注

- 本批中 `ecd` 与 `zh` 两套文档均未收录此函数的正文（`ecd/Expression.md` 签名表也没有它），只有 `Error_Index.md` 收录了 `STRFORM関数:文字列"..."` 的报错条目（对应源码 `trerror.InvalidFormString`）；语义按源码 + 官方 wiki 补写。
- 源码报错文案已从硬编码字符串改为 `trerror` 资源化，内容与 `Error_Index.md` 所记一致。
- 与 `PRINTFORM` 的区别：不产生打印副作用，仅返回展开结果；与 `@"..."` 插值字符串（字符串表达式内直接写 FORM）能力等价。
