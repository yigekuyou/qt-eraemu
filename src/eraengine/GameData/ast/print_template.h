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
