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
// ---------------------------------------------------------------------------
// qdbug_probe —— 字符串赋值右值的求值诊断（保留的 qdbug 桩）
//
// 背景（eraTW 实测差距）：
//   MOBGIRL_GENERATOR.ERB:145  ARGS = %\@ ARGS == "販売員" ? 因幡 # %ARGS% \@%
//   MOBGIRL_GENERATOR.ERB:146  CSTR:ARG:路人子種族 = %ARGS%
//   移植版把 145 行求值成字面 "ARGS" 并写进 ARGS:0，146 行再读出，
//   于是路人子素質栏显示「種族：[…][ARGS]」。
//
// 本探针按 applyStringAssignments → handleStringAssignment 的真实链路复现：
//   StrFormParser::parse(rhs, resolve)  →  ExpressionEvaluator::evaluate
// ARGS:0 预置为 "测试值"（模拟 CALL MAKE_YUKIZURI 传入的实参）。
//
// 用法：qdbug_probe "%ARGS%" "%\@ ARGS == \"販売員\" ? 因幡 # %ARGS% \@%" ...
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <functional>

#include "ast/expression_lexer.h"
#include "ast/expression_parser.h"
#include "ast/expression_ast.h"
#include "ast/expression_evaluator.h"
#include "ast/strform_parser.h"
#include "ast/operand_type.h"
#include "Variable/variable_storage.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    VariableStorage storage;
    storage.setArgStr(0, QStringLiteral("测试值"));   // 模拟 ARGS:0 的实参值

    ExpressionLexer lexer;
    ExpressionParser parser;
    std::function<QSharedPointer<ExpressionNode>(const QString&)> resolve =
        [&parser, &lexer](const QString& e) -> QSharedPointer<ExpressionNode> {
            return parser.parse(lexer.tokenize(e, 1));
        };
    // 与 EraParseTable::expressionAst 相同的接线（\@ 三元 / @"..." 格式化串）
    parser.setFormProvider([&resolve](const QString& text, bool yenAt)
                               -> QSharedPointer<ExpressionNode> {
        if (yenAt) return StrFormParser::parseYenAt(text, resolve);
        return QSharedPointer<ExpressionNode>(StrFormParser::parse(text, resolve));
    });

    ExpressionEvaluator evaluator;
    for (int i = 1; i < argc; ++i) {
        const QString rhs = QString::fromLocal8Bit(argv[i]);
        // 装载期：applyStringAssignments 对 `=` 的字符串右值建 StrForm 节点
        const QSharedPointer<ExpressionNode> ast = StrFormParser::parse(rhs, resolve);
        // 运行期：handleStringAssignment 求值
        const QString value = ast
            ? evaluator.evaluate(*ast.staticCast<ExpressionNode>(), &storage, nullptr).toString()
            : QStringLiteral("<null AST>");
        qDebug().noquote() << "RHS:" << rhs;
        qDebug().noquote() << "  => value:" << value;
        // parts 转储：看 StrForm 拆出了什么（文本/表达式/宽度）
        if (ast) {
            const auto* form = ast.staticCast<StrFormNode>().data();
            int pi = 0;
            for (const StrFormPart& p : form->parts()) {
                const QString exprDump = p.expression ? p.expression->toString() : QString();
                qDebug().noquote() << QStringLiteral("  part[%1] type=%2 text=\"%3\" expr={%4} width=%5")
                    .arg(pi).arg(int(p.type)).arg(p.text, exprDump)
                    .arg(p.width ? p.width->toString() : QString());
                ++pi;
            }
        }
    }
    return 0;
}
