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
    explicit FlatStringValue(QString value) : text(std::move(value)) {
        PrintTemplatePart part; part.text = text; markup.parts.append(std::move(part));
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
    return toVariant(out);
}

#endif
