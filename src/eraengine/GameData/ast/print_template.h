#ifndef PRINT_TEMPLATE_COMPILER_H
#define PRINT_TEMPLATE_COMPILER_H
#include "logical_line.h"
#include <functional>

// Load-time compiler for static string/FORM ASTs. Evaluated values remain text.
class PrintTemplateCompiler {
public:
    static void prepare(const QSharedPointer<ExpressionNode>& node);
    static QSharedPointer<CompiledPrintTemplate> compile(const QSharedPointer<ExpressionNode>& string);
    static PrintTemplate evaluate(const CompiledPrintTemplate& source,
        const std::function<QString(const ExpressionNode&)>& evaluate);
};

// Raw string APIs use text; only HTML output consumes the evaluated structure.
// A fully evaluated string. It contains only text and flattened print data;
// it never owns an ExpressionNode and cannot be lazily evaluated.
struct FlatStringValue {
    QString text;
    PrintTemplate markup;
};

// Kept as a source-compatible name for existing storage tests while migration
// proceeds. It is deliberately not registered as a QString converter.
using StructuredString = FlatStringValue;
Q_DECLARE_METATYPE(FlatStringValue)

inline bool isStructuredString(const QVariant& value) {
    return value.metaType() == QMetaType::fromType<FlatStringValue>();
}

// Explicit conversions. There is intentionally no QMetaType converter.
inline QString stringText(const QVariant& value) {
    return isStructuredString(value) ? value.value<FlatStringValue>().text : value.toString();
}
inline FlatStringValue flatString(const QVariant& value) {
    if (isStructuredString(value)) return value.value<FlatStringValue>();
    FlatStringValue out;
    out.text = value.toString();
    PrintTemplatePart part;
    part.kind = PrintTemplatePart::Kind::Text;
    part.text = out.text;
    out.markup.parts.append(std::move(part));
    return out;
}
inline bool isStringValue(const QVariant& value) {
    return value.typeId() == QMetaType::QString || isStructuredString(value);
}
inline QVariant toVariant(const FlatStringValue& value) {
    return QVariant::fromValue(value);
}
inline FlatStringValue fromVariant(const QVariant& value) {
    return flatString(value);
}
inline QVariant structuredString(const QString& text, const PrintTemplate& markup) {
    return toVariant(FlatStringValue{text, markup});
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

// Explicit text conversion at API boundaries. Callers that need structure
// must use flatString()/valueMarkup() instead of QVariant's implicit converter.
inline QString stringValue(const QVariant& value) { return stringText(value); }

#endif
