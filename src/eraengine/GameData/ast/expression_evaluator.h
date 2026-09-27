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
#ifndef EXPRESSION_EVALUATOR_H
#define EXPRESSION_EVALUATOR_H

#include <QObject>
#include <QVariant>
#include <QString>
#include <QList>
#include <functional>

#include "expression_ast.h"
#include "text_encoding.h"

class VariableStorage; // Forward declaration
class GameBaseData; // Forward declaration
class ConstantTable;  // Forward declaration（CSV 常量名表）

class ExpressionEvaluator : public QObject
{
    Q_OBJECT

public:
    explicit ExpressionEvaluator(QObject *parent = nullptr);

    // Existing entry points (parse string + evaluate)
    Q_INVOKABLE QVariant evaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    Q_INVOKABLE bool evaluateInt(const QString &expression, VariableStorage *storage, qint64 &result);
    Q_INVOKABLE bool evaluateStr(const QString &expression, VariableStorage *storage, QString &result);

    // NEW: Direct AST evaluation entry points (for cached AST)
    QVariant evaluate(const ExpressionNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    bool evaluateInt(const ExpressionNode &node, VariableStorage *storage, qint64 &result);
    bool evaluateStr(const ExpressionNode &node, VariableStorage *storage, QString &result);

    // 用户自定义函数回调：表达式 `NAME(args)` 命中用户函数时调用（由执行链提供）。
    // 返回 true 表示已处理，out 为返回值。
    using UserFunctionInvoker = std::function<bool(const QString &name,
                                                   const QList<QVariant> &args,
                                                   QVariant &out)>;
    void setUserFunctionInvoker(UserFunctionInvoker invoker) { m_userInvoker = std::move(invoker); }

    // CSV 常量名表（把变量字符串下标解析为整数下标，对齐 C# VariableStrArgTerm）
    void setConstantTable(const ConstantTable* table) { m_constantTable = table; }
    [[nodiscard]] const ConstantTable* constantTable() const { return m_constantTable; }

    // ---- 内置函数（内部命令）所需的运行期上下文 ----
    // 语言编码（对齐 C# LangManager.setEncode(Config.Encode)）：
    // STRLENS/SUBSTRING/STRFIND 的「位置/长度」按该编码的字节数计
    // （日文 Shift-JIS 下 ASCII=1 字节、假名/汉字=2 字节）。
    void setLanguageEncoding(TextEncoding enc) { m_langEncoding = enc; }
    [[nodiscard]] TextEncoding languageEncoding() const { return m_langEncoding; }

    // GETCONFIG / GETCONFIGS 的配置取值回调（返回 false = 未命中）。
    using ConfigProvider = std::function<bool(const QString& key, QString& value)>;
    void setConfigProvider(ConfigProvider provider) { m_configProvider = std::move(provider); }

    // MONEYSTR / BARSTR 的显示参数（对齐 C# Config.MoneyLabel/MoneyFirst/BarChar1/BarChar2）
    void setMoneyLabel(const QString& label, bool moneyFirst);
    void setBarChars(QChar filled, QChar empty);

    // 角色数（SUMCARRAY/CMATCH/… 的范围上限，对齐 C# VEvaluator.CHARANUM）。
    // 默认回退到 CHARANUM 系统变量；执行链可注入实际值。
    using CharaCountProvider = std::function<int()>;
    void setCharaCountProvider(CharaCountProvider provider) { m_charaNumProvider = std::move(provider); }

signals:
    void evaluationFinished(const QString &expression, const QVariant &result);
    void evaluationError(const QString &expression, const QString &errorString);

public slots:
    QVariant slotEvaluate(const QString &expression, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    
private:
    // 变量下标求值：字符串下标经 ConstantTable 映射为整数（如 CFLAG:ARG:現在位置）
    qint64 resolveIndex(const VariableNode& node, int index,
                        VariableStorage* storage, GameBaseData* gameBaseData);

    // AST evaluation methods
    QVariant evaluateNode(const ExpressionNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateLiteral(const LiteralNode &node);
    // 二元求值的两条路径（整数快路径 / 字符串慢路径）
    QVariant evaluateIntBinary(TokenType op, qint64 l, qint64 r);
    QVariant evaluateStrBinary(TokenType op, const QString& ls, const QString& rs,
                               const QVariant& left, const QVariant& right);
    QVariant evaluateStrForm(const StrFormNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateVariable(const VariableNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateIndexedVariable(const QString &varName, int index, VariableStorage *storage);
    QVariant evaluateBinaryOp(const BinaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateUnaryOp(const UnaryOpNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);
    QVariant evaluateFunction(const FunctionNode &node, VariableStorage *storage, GameBaseData *gameBaseData = nullptr);

    // ---- 内置函数（内部命令）求值 ----
    // 返回 false 表示该 opcode 尚未实现求值（交给用户函数回调 / 默认 0）。
    bool evaluateBuiltin(const FunctionNode &node, VariableStorage *storage,
                         GameBaseData *gameBaseData, QVariant &out);

    // 实参取值辅助
    qint64  argInt(const FunctionNode &node, int i, VariableStorage *storage, GameBaseData *g);
    QString argStr(const FunctionNode &node, int i, VariableStorage *storage, GameBaseData *g);
    [[nodiscard]] const VariableNode* argVar(const FunctionNode &node, int i) const;
    [[nodiscard]] static bool hasArg(const FunctionNode &node, int i);

    // 数组：读一维整型数组内容 / 取长度（对齐 C# VariableTerm 的数组访问）
    QList<qint64> readIntArray(const VariableNode &var, VariableStorage *storage,
                               GameBaseData *gameBaseData, bool charaRange) const;
    [[nodiscard]] int charaCount(VariableStorage *storage) const;

    // TOINT / ISNUMERIC 的 Emuera 语义（对齐 C# ToIntMethod / IsNumericMethod）
    [[nodiscard]] qint64 parseIntLikeEmuera(const QString& str) const;
    [[nodiscard]] bool   isNumericLikeEmuera(const QString& str) const;

    // 语言相关的字节长度（对齐 C# LangManager.GetStrlenLang / GetSubStringLang / GetUFTIndex）
    [[nodiscard]] int langByteCount(const QString& s) const;
    [[nodiscard]] int langByteCountOfChar(QChar c) const;
    [[nodiscard]] QString langSubstring(const QString& s, int startIndex, int length) const;
    [[nodiscard]] int langIndexOf(const QString& target, const QString& word, int langStart) const;

    // Helper methods for expression evaluation
    bool parseAndEvaluate(const QString &expression, VariableStorage *storage);

    UserFunctionInvoker m_userInvoker;
    ConfigProvider m_configProvider;
    CharaCountProvider m_charaNumProvider;
    const ConstantTable* m_constantTable = nullptr;
    TextEncoding m_langEncoding = TextEncoding::ShiftJis;
    QString m_moneyLabel = QStringLiteral("$");
    bool    m_moneyFirst = true;
    QChar   m_barFilled = QLatin1Char('*');
    QChar   m_barEmpty  = QLatin1Char('.');
};

#endif // EXPRESSION_EVALUATOR_H
