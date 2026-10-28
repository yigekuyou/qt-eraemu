#ifndef PRINT_TEMPLATE_COMPILER_H
#define PRINT_TEMPLATE_COMPILER_H
#include "logical_line.h"
#include <functional>

// Load-time compiler for static string/FORM ASTs. Evaluated values remain text.
class PrintTemplateCompiler {
public:
    static void prepare(const QSharedPointer<ExpressionNode>& node);
    static QSharedPointer<CompiledPrintTemplate> compile(const QSharedPointer<ExpressionNode>& string);
    static PrintTemplate evaluateOccurrences(const CompiledPrintTemplate& source,
        const std::function<QVariant(const ExpressionNode&, int)>& evaluate);

    // ---- 打印期标签解析（对齐 C# EmueraConsole.PrintHtml → HtmlManager.Html2DisplayLine）----
    // C# 是在**打印时**从最终字符串解析 HTML 的；构建期编译只覆盖「静态可见」的标签。
    // 运行期拼装出来的标签（`+=` 把 <img …> 拼成两段、字符串函数产出的标签）
    // 只能在此刻解析，否则会原样打成文本。
    //
    // coversText：值的模板分词是否**已经解释**了文本里的标签。返回 false = 文本里
    // 还有装载期没消化掉的标签材料（未被解释的 '<'）。
    [[nodiscard]] static bool coversText(const PrintTemplate& markup, const QString& text);
    // 打印期**补解析**：对齐 C# PrintHtml —— HtmlManager.Html2DisplayLine 在打印时
    // 从**最终字符串**解析标签。装载期编译只覆盖「静态可见」的标签；字符串函数 /
    // 运行期拼接产出的标签（`PORTRAIT` 变量里的 <img …>）只能在此刻解析。
    // 文本已完全求值（无插值）—— 全部按字面量走 compileMarkup 的同一套词法。
    [[nodiscard]] static PrintTemplate parseRuntimeMarkup(const QString& text);
};

// Raw string APIs use text; only HTML output consumes the evaluated structure.
// A fully evaluated string. It contains only text and flattened print data;
// it never owns an ExpressionNode and cannot be lazily evaluated.
struct FlatStringValue {
    QString text;
    PrintTemplate markup;
    FlatStringValue() = default;
    // 普通字符串（不含 IR）：text 没有经过 compileMarkup，markup 只是「一整段纯文本」。
    // 因此 verified=false —— 若这段文字里带着标签（string 函数返回值、读档恢复的
    // HTML、`+=` 拼进来的变量），打印期必须对最终字符串重新分词。
    explicit FlatStringValue(QString value) : text(std::move(value)) {
        PrintTemplatePart part; part.text = text; markup.parts.append(std::move(part));
        markup.verified = false;
    }
    explicit FlatStringValue(QString value, PrintTemplate output) : text(std::move(value)), markup(std::move(output)) {}
};

Q_DECLARE_METATYPE(FlatStringValue)

inline bool isFlatStringValue(const QVariant& value) {
    return value.metaType() == QMetaType::fromType<FlatStringValue>();
}

// Explicit conversions. There is intentionally no QMetaType converter.
inline QString stringText(const QVariant& value) {
    return isFlatStringValue(value) ? value.value<FlatStringValue>().text : value.toString();
}
inline FlatStringValue flatString(const QVariant& value) {
    if (isFlatStringValue(value)) return value.value<FlatStringValue>();
    return FlatStringValue(stringText(value));
}
inline bool isStringValue(const QVariant& value) {
    return value.typeId() == QMetaType::QString || isFlatStringValue(value);
}
inline QVariant toVariant(const FlatStringValue& value) {
    return QVariant::fromValue(value);
}
inline PrintTemplate valueMarkup(const QVariant& value) {
    return flatString(value).markup;
}
inline QVariant concatenateStrings(const QVariant& left, const QVariant& right) {
    auto out = flatString(left);
    const auto rhs = flatString(right);
    out.text += rhs.text;
    out.markup.parts.append(rhs.markup.parts);
    // 拼接是唯一会把两个**独立分词结果**接在一起的入口。标签可以跨片段
    //（`+= @"<font …>"` 与 `+= @"</font>"` 是两条语句），而每个片段单独看是
    // 平衡的或空栈的 —— 拼出来的 IR 无法证明配对，因此一律作废：
    // 打印期按 C# PrintHtml 对最终字符串重新分词一次（同一个 compileMarkup）。
    // 装载期能证明的只有**整条写链**（EraParseTable 反向追踪把 += 拼成一棵
    // PLUS 树后由 compile() 一次分词），那条路径不经过这里。
    out.markup.verified = false;
    return toVariant(out);
}

#endif
