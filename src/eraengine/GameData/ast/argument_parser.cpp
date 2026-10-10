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
#include "argument_parser.h"
#include <QCoreApplication>
#include <cmath>
#include <QDoubleValidator>
#include <QLocale>

namespace {

inline bool isSeparator(const Operand& o) {
    return o.raw == QLatin1String(",") || o.raw == QLatin1String(":");
}

// 把操作数中的「分隔符」剔除（逗号/冒号）
QList<Operand> nonSeparators(const QList<Operand>& args) {
    QList<Operand> out;
    out.reserve(args.size());
    for (const Operand& a : args) {
        if (!isSeparator(a)) out.append(a);
    }
    return out;
}

void collectExprs(const QList<Operand>& ops, QList<QSharedPointer<ExpressionNode>>& out) {
    for (const Operand& o : ops) {
        if (o.ast) out.append(o.ast);
    }
}

} // namespace

void ArgumentParser::build(LogicalLine& line) {
    TypedArgument& arg = line.argument;

    // 幂等：本函数会在「变量类型回填前后」各调用一次，必须先复位状态
    arg.typeOk = true;
    arg.typeError.clear();
    arg.operands.clear();
    arg.params.clear();
    arg.exprs.clear();
    arg.cases.clear();

    // 标签行 / 非指令：无参数
    if (line.kind != LineKind::Instruction) {
        arg.kind = ArgKind::Void;
        return;
    }

    const QString upper = line.functionName;
    int mn = 0, mx = -1;
    arg.kind = classifyInstructionKind(upper.toStdString(), mn, mx);
    arg.minArgs = mn;
    arg.maxArgs = mx;

    // TIMES 的实数倍率直接存入命令 AST；后续类型回填重建参数时复用数值。
    if (arg.kind == ArgKind::Times) {
        int index = 0;
        for (Operand& operand : line.arguments) {
            if (isSeparator(operand)) continue;
            if (index++ != 1 || operand.isString || operand.realValue) continue;
            QString text = operand.raw.trimmed();
            QLocale locale = QLocale::c();
            locale.setNumberOptions(locale.numberOptions() | QLocale::RejectGroupSeparator);
            QDoubleValidator validator;
            validator.setLocale(locale);
            validator.setNotation(QDoubleValidator::ScientificNotation);
            validator.setDecimals(-1);
            int pos = 0;
            if (validator.validate(text, pos) != QValidator::Acceptable) continue;
            bool valid = false;
            const double value = locale.toDouble(text, &valid);
            if (valid && std::isfinite(value)) operand.realValue = value;
        }
    }
    const QList<Operand> ops = nonSeparators(line.arguments);
    arg.operands = ops;

    // ---- 归约（按参数族）----
    switch (arg.kind) {
    case ArgKind::Void:
        break;

    case ArgKind::IntExpression:
    case ArgKind::StrExpression:
    case ArgKind::Expression:
    case ArgKind::FormStr:
        if (line.condition) arg.exprs.append(line.condition);
        else collectExprs(ops, arg.exprs);
        break;

    case ArgKind::Call:      // CALL name(args...)
    case ArgKind::CallForm:
    case ArgKind::CallF:
    case ArgKind::ForNext:   // FOR var, start, end[, step]
        arg.params = ops;
        for (int i = 1; i < ops.size(); ++i) {
            if (ops.at(i).ast) arg.exprs.append(ops.at(i).ast);
        }
        break;

    case ArgKind::Case:
        arg.cases = ops;
        collectExprs(ops, arg.exprs);
        break;

    default:
        // Var/VarSet/Swap/Power/Bit/GetInt/VarStr/Expressions/PrintV/Input/Button/
        // SaveData/Split/Color/Times/Bar/ArrayControl/SortChara/HtmlSplit/PrintData/Raw…
        arg.params = ops;
        collectExprs(ops, arg.exprs);
        break;
    }

    // ---- 个数校验 ----
    int n = 0;
    switch (arg.kind) {
    case ArgKind::Void:
        n = line.arguments.isEmpty() ? 0 : 1;
        break;
    case ArgKind::IntExpression:
    case ArgKind::StrExpression:
    case ArgKind::Expression:
    case ArgKind::FormStr:
        n = line.condition ? 1 : arg.operands.size();
        break;
    case ArgKind::Case:
        n = arg.cases.size();
        break;
    case ArgKind::Call:
    case ArgKind::CallForm:
    case ArgKind::CallF:
    case ArgKind::ForNext:
    case ArgKind::Var:
    case ArgKind::VarSet:
    case ArgKind::Swap:
    case ArgKind::Power:
    case ArgKind::Bit:
    case ArgKind::GetInt:
    case ArgKind::VarStr:
        n = arg.params.size();
        break;
    default:
        n = arg.operands.size();
        break;
    }

    if (arg.minArgs > 0 && n < arg.minArgs) {
        arg.typeOk = false;
        arg.typeError = QCoreApplication::translate("ParseDiagnostics", "%1 参数过少（需要至少 %2 个，实得 %3）")
                            .arg(upper).arg(arg.minArgs).arg(n);
    } else if (arg.maxArgs >= 0 && n > arg.maxArgs) {
        arg.typeOk = false;
        arg.typeError = QCoreApplication::translate("ParseDiagnostics", "%1 参数过多（最多 %2 个，实得 %3）")
                            .arg(upper).arg(arg.maxArgs).arg(n);
    }

    if (arg.typeOk && arg.kind == ArgKind::Times) {
        if (!ops.at(1).realValue) {
            arg.typeOk = false;
            arg.typeError = QCoreApplication::translate("ParseDiagnostics", "TIMES 的倍率需要有限实数常量：%1").arg(ops.at(1).raw);
        } else if (!ops.first().ast || ops.first().ast->valueType() == OperandType::Str) {
            arg.typeOk = false;
            arg.typeError = QCoreApplication::translate("ParseDiagnostics", "TIMES 的第一个参数需要数值变量");
        }
    }

    // ---- 表达式可解析性校验（单表达式族）----
    if (arg.typeOk
        && (arg.kind == ArgKind::IntExpression || arg.kind == ArgKind::StrExpression
            || arg.kind == ArgKind::Expression)) {
        for (const Operand& o : arg.operands) {
            if (o.raw.trimmed().isEmpty() || o.isString || o.isVariable) continue;
            if (!o.ast) {
                arg.typeOk = false;
                arg.typeError = QCoreApplication::translate("ParseDiagnostics", "%1 的操作数无法解析为表达式：%2").arg(upper, o.raw);
                break;
            }
        }
    }

    // ---- 首参必须是变量的软校验（SP_SET/SP_VAR/BIT_ARG/SP_POWER…）----
    if (arg.typeOk && !arg.params.isEmpty()) {
        const ArgKind k = arg.kind;
        const bool needsVar = (k == ArgKind::Var || k == ArgKind::VarSet || k == ArgKind::Bit
                               || k == ArgKind::Power || k == ArgKind::GetInt || k == ArgKind::VarStr
                               || k == ArgKind::Swap || k == ArgKind::Times || k == ArgKind::SortChara);
        if (needsVar) {
            const Operand& first = arg.params.first();
            if (first.ast && first.ast->kind() != NodeKind::Variable) {
                arg.typeOk = false;
                arg.typeError = QCoreApplication::translate("ParseDiagnostics", "%1 的第一个参数需要变量，实得表达式").arg(upper);
            }
        }
    }

    // ---- 类型校验（强类型 AST）----
    if (arg.typeOk && !arg.exprs.isEmpty()) {
        const auto requireType = [&](OperandType want, const char* what) {
            for (const auto& e : arg.exprs) {
                if (!e) continue;
                const OperandType t = e->valueType();
                if (isKnown(t) && t != want) {
                    arg.typeOk = false;
                    arg.typeError = QCoreApplication::translate("ParseDiagnostics", "%1 需要%2表达式，实得 %3")
                                        .arg(upper, QString::fromUtf8(what),
                                             QString::fromUtf8(operandTypeName(t)));
                    return;
                }
            }
        };
        if (arg.kind == ArgKind::IntExpression) requireType(OperandType::Int, "整型");
        else if (arg.kind == ArgKind::StrExpression) requireType(OperandType::Str, "字符串");
    }
}
