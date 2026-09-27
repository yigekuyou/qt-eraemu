/*
 * emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
 * Copyright (C) 2026  yigekuyou
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// 表达式解析诊断：expr_probe "expr" ...
// 打印每个表达式的 tokens、AST kind、valueType，便于定位解析缺口。
#include <QCoreApplication>
#include <QDebug>
#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/expression_ast.h"
#include "ast/operand_type.h"
#include "ast/strform_parser.h"
#include "ast/variable_table.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    ExpressionLexer lexer;
    for (int i = 1; i < argc; ++i) {
        const QString expr = QString::fromLocal8Bit(argv[i]);
        const QList<ExpressionToken> tokens = lexer.tokenize(expr, 1);
        QStringList toks;
        for (const auto& t : tokens) toks << QStringLiteral("%1(%2)").arg(int(t.type())).arg(t.value());
        qDebug().noquote() << "EXPR:" << expr;
        qDebug().noquote() << "  tokens:" << toks.join(' ');
        ExpressionParser parser;
        // 与 EraParseTable::expressionAst 相同的接线
        std::function<QSharedPointer<ExpressionNode>(const QString&)> resolve =
            [&parser, &lexer](const QString& e) -> QSharedPointer<ExpressionNode> {
                return parser.parse(lexer.tokenize(e, 1));
            };
        parser.setFormProvider([&resolve](const QString& text, bool yenAt) -> QSharedPointer<ExpressionNode> {
            if (yenAt) return StrFormParser::parseYenAt(text, resolve);
            return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, resolve));
        });
        QSharedPointer<ExpressionNode> ast = parser.parse(tokens);
        if (!ast) {
            qDebug().noquote() << "  AST: <null>  (解析失败)";
            continue;
        }
        qDebug().noquote() << "  AST: kind=" << int(ast->kind())
                           << " valueType=" << operandTypeName(ast->valueType())
                           << " dump=" << ast->toString();
    }
    return 0;
}
