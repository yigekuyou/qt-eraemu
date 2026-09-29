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
#include "user_defined_variable_data.h"
#include <QRegularExpression>
#include <QException>
#include "ast/expression_ast.h"

namespace {

// 顶层切分（尊重引号与 ()/[]/{} 嵌套）—— 维数表达式里会出现
// VARSIZE("X", 1) 这种带逗号的调用，不能简单 split(',')
QStringList splitTopLevel(const QString& text, QChar separator) {
		QStringList out;
		QString current;
		int depth = 0;
		QChar quote;
		for (int i = 0; i < text.length(); ++i) {
				const QChar c = text.at(i);
				if (!quote.isNull()) {
						current += c;
						if (c == quote) quote = QChar();
						continue;
				}
				if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; current += c; continue; }
				if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) { ++depth; current += c; continue; }
				if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) { if (depth > 0) --depth; current += c; continue; }
				if (depth == 0 && c == separator) { out.append(current); current.clear(); continue; }
				current += c;
		}
		out.append(current);
		return out;
}

// 第一个顶层 '=' 的位置（找不到返回 -1）
int findTopLevelEquals(const QString& text) {
		int depth = 0;
		QChar quote;
		for (int i = 0; i < text.length(); ++i) {
				const QChar c = text.at(i);
				if (!quote.isNull()) {
						if (c == quote) quote = QChar();
						continue;
				}
				if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
				if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) { ++depth; continue; }
				if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) { if (depth > 0) --depth; continue; }
				if (depth == 0 && c == QLatin1Char('=')) return i;
		}
		return -1;
}

} // namespace

UserDefinedVariableData UserDefinedVariableData::create(QString streamContent, bool isDims, bool isPrivate, const ScriptPosition&) {
		UserDefinedVariableData data;
		data.typeIsStr = isDims;
		data.isPrivate = isPrivate;

		QString stream = streamContent.trimmed();

		// 1. 提取修饰关键字 (DYNAMIC, CONST, REF 等)
		QRegularExpression tokenRe("^([^\\s,=()]+)");   // 允许非 ASCII 变量名（CJK/全角）
		while (true) {
				QRegularExpressionMatch match = tokenRe.match(stream);
				if (!match.hasMatch()) break;

				QString keyword = match.captured(1).toUpper();

				if (keyword == "CONST") {
						if (data.reference || !data.isStatic) throw std::runtime_error("CONST与REF/DYNAMIC冲突");
						data.isConst = true;
				} else if (keyword == "REF") {
						data.reference = true;
						data.isStatic = false;
				} else if (keyword == "DYNAMIC") {
						if (!isPrivate) throw std::runtime_error("全局变量不能指定 DYNAMIC");
						data.isStatic = false;
				} else if (keyword == "STATIC") {
						data.isStatic = true;
				} else if (keyword == "CHARADATA") {
						data.charaData = true;
				} else if (keyword == "SAVEDATA") {
						data.save = true;
				} else if (keyword == "GLOBAL") {
						data.global = true;
				} else {
						// 非修饰关键字，即为变量名
						data.name = match.captured(1);
						stream.remove(0, match.capturedLength(1));
						break;
				}

				stream.remove(0, match.capturedLength(1));
				stream = stream.trimmed();
		}

		if (data.name.isEmpty()) {
				throw std::runtime_error("无效的变量名");
		}

		// 2. 解析维数与长度指定 (如: , 10, 20)
		stream = stream.trimmed();
		QList<int> sizeNum;

		if (stream.startsWith(',')) {
				stream.remove(0, 1);

				// 分割维度与初始值部分（顶层 '='，避免 VARSIZE("X=Y",1) 之类误切）
				const int eq = findTopLevelEquals(stream);
				QString dimPart = (eq >= 0) ? stream.left(eq) : stream;

				const QStringList dims = splitTopLevel(dimPart, QLatin1Char(','));
				for (const QString& d : dims) {
						const QString trimmedD = d.trimmed();
						data.lengthExprs.append(trimmedD);
						if (trimmedD.isEmpty()) {
								sizeNum.append(0);
						} else {
								bool ok = false;
								int val = trimmedD.toInt(&ok);
								if (!ok || val <= 0) {
										// 维数可为常数表达式（C# 用 ReduceIntegerTerm 折叠）；此处尚不能求值，
										// 记为 0（未知尺寸）而不视为错误。
										sizeNum.append(0);
								} else {
										sizeNum.append(val);
								}
						}
				}

				// 如果包含 '='，将剩余部分作为初始值留给后续解包
				if (eq >= 0) {
						stream = stream.mid(eq);
				} else {
						stream.clear();
				}
		}

		if (sizeNum.isEmpty()) sizeNum.append(1);

		data.dimension = sizeNum.size();
		data.lengths = sizeNum;

		if (data.dimension > 3) {
				throw std::runtime_error("不支持 4 维以上的数组");
		}

		// 3. 解析默认初始值 (如: = "A", "B")
		stream = stream.trimmed();
		if (stream.startsWith('=')) {
				stream.remove(0, 1);
				const QStringList initialValues = splitTopLevel(stream, QLatin1Char(','));

				for (QString val : initialValues) {
						val = val.trimmed();
						if (isDims) {
								// 清理字符串前后引号
								if (val.startsWith('"') && val.endsWith('"') && val.length() >= 2) {
										val = val.mid(1, val.length() - 2);
								}
								data.defaultStr.append(val);
						} else {
								qint64 num = 0;
								// 宽松：无法解析的初值按 0（真实脚本存在如 "= 1p0" / "= " 之类写法，
								// 此处对齐 C# ReadInt64 支持 0x/0b/p/e）
								if (!parseIntegerLiteral(val, num)) num = 0;
								data.defaultInt.append(num);
						}
				}
		} else if (data.isConst) {
				throw std::runtime_error("CONST 变量必须赋予初始值");
		}

		return data;
}