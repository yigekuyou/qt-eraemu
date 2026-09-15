#include "user_defined_variable_data.h"
#include <QRegularExpression>
#include <QException>

UserDefinedVariableData UserDefinedVariableData::create(QString streamContent, bool isDims, bool isPrivate, const ScriptPosition& pos) {
		UserDefinedVariableData data;
		data.typeIsStr = isDims;
		data.isPrivate = isPrivate;

		QString stream = streamContent.trimmed();

		// 1. 提取修饰关键字 (DYNAMIC, CONST, REF 等)
		QRegularExpression tokenRe("^([A-Za-z_]\\w*)");
		bool staticDefined = false;

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

				// 分割维度与初始值部分
				QStringList parts = stream.split('=');
				QString dimPart = parts.first();

				QStringList dims = dimPart.split(',');
				for (const QString& d : dims) {
						QString trimmedD = d.trimmed();
						if (trimmedD.isEmpty()) {
								sizeNum.append(0);
						} else {
								bool ok = false;
								int val = trimmedD.toInt(&ok);
								if (!ok || val <= 0) throw std::runtime_error("数组维数指定错误");
								sizeNum.append(val);
						}
				}

				// 如果包含 '='，将剩余部分作为初始值留给后续解包
				if (parts.size() > 1) {
						stream = "=" + parts.mid(1).join('=');
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
				QStringList initialValues = stream.split(',');

				for (QString val : initialValues) {
						val = val.trimmed();
						if (isDims) {
								// 清理字符串前后引号
								if (val.startsWith('"') && val.endsWith('"')) {
										val = val.mid(1, val.length() - 2);
								}
								data.defaultStr.append(val);
						} else {
								bool ok = false;
								qlonglong num = val.toLongLong(&ok);
								if (!ok) throw std::runtime_error("初始值与类型不匹配");
								data.defaultInt.append(num);
						}
				}
		} else if (data.isConst) {
				throw std::runtime_error("CONST 变量必须赋予初始值");
		}

		return data;
}