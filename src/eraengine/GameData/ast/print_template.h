#ifndef PRINT_TEMPLATE_COMPILER_H
#define PRINT_TEMPLATE_COMPILER_H
#include "logical_line.h"
#include <functional>

// One compiler for static string/form ASTs and runtime markup strings.
class PrintTemplateCompiler {
public:
    static QSharedPointer<PrintTemplate> compile(const QString& markup);
    static QSharedPointer<PrintTemplate> compile(const QSharedPointer<ExpressionNode>& string);
    static PrintTemplate evaluate(const PrintTemplate& source,
        const std::function<QString(const ExpressionNode&)>& evaluate);
};
#endif
