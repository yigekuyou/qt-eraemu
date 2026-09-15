#ifndef FUNCTION_LABEL_LINE_H
#define FUNCTION_LABEL_LINE_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <user_defined_variable_data.h>
class FunctionLabelLine {
public:
		FunctionLabelLine(int lineNo, const QString& labelName)
				: m_lineNo(lineNo), m_labelName(labelName),
					m_isEvent(false), m_isSystem(false), m_isSingle(false),
					m_isPri(false), m_isLater(false), m_isOnly(false),
					m_hasPrivDynamicVar(false),
					m_isMethod(false), m_methodType("int"),
					m_localLength(0), m_localsLength(0),
					m_argLength(0), m_argsLength(0),
					m_depth(-1), m_index(-1), m_fileIndex(0) {}
		bool addPrivateVariable(const UserDefinedVariableData& data) {
						if (m_privateVar.contains(data.name)) {
								return false; // 变量名已存在
						}
						m_privateVar.insert(data.name, data);
						if (!data.isStatic) {
								m_hasPrivDynamicVar = true; // 存在动态私有变量，标记生命周期
						}
						return true;
				}

				QMap<QString, UserDefinedVariableData> privateVariables() const {
						return m_privateVar;
				}
		// Getters & Setters
		QString labelName() const { return m_labelName; }
		int lineNo() const { return m_lineNo; }

		bool isEvent() const { return m_isEvent; }
		void setIsEvent(bool val) { m_isEvent = val; }

		bool isSystem() const { return m_isSystem; }
		void setIsSystem(bool val) { m_isSystem = val; }

		bool isSingle() const { return m_isSingle; }
		void setIsSingle(bool val) { m_isSingle = val; }

		bool isPri() const { return m_isPri; }
		void setIsPri(bool val) { m_isPri = val; }

		bool isLater() const { return m_isLater; }
		void setIsLater(bool val) { m_isLater = val; }

		bool isOnly() const { return m_isOnly; }
		void setIsOnly(bool val) { m_isOnly = val; }

		bool hasPrivDynamicVar() const { return m_hasPrivDynamicVar; }
		void setHasPrivDynamicVar(bool val) { m_hasPrivDynamicVar = val; }

		bool isMethod() const { return m_isMethod; }
		void setIsMethod(bool val) { m_isMethod = val; }

		QString methodType() const { return m_methodType; }
		void methodType(const QString& type) { m_methodType = type; }

		int localLength() const { return m_localLength; }
		void setLocalLength(int len) { m_localLength = len; }

		int localsLength() const { return m_localsLength; }
		void setLocalsLength(int len) { m_localsLength = len; }

		int argLength() const { return m_argLength; }
		void setArgLength(int len) { m_argLength = len; }

		int argsLength() const { return m_argsLength; }
		void setArgsLength(int len) { m_argsLength = len; }

		int depth() const { return m_depth; }
		void setDepth(int depth) { m_depth = depth; }

		int index() const { return m_index; }
		void setIndex(int index) { m_index = index; }

		int fileIndex() const { return m_fileIndex; }
		void setFileIndex(int fileIndex) { m_fileIndex = fileIndex; }

private:
		int m_lineNo;
		QString m_labelName;

		// 对应 C# 中的各种标签属性标志[cite: 3, 5]
		bool m_isEvent;
		bool m_isSystem;
		bool m_isSingle;
		bool m_isPri;
		bool m_isLater;
		bool m_isOnly;
		bool m_hasPrivDynamicVar; // 是否含有私有动态变量

		bool m_isMethod;       // #FUNCTION / #FUNCTIONS[cite: 3]
		QString m_methodType;  // "int" 或 "string"[cite: 3]

		int m_localLength;     // #LOCALSIZE[cite: 3]
		int m_localsLength;    // #LOCALSSIZE[cite: 3]
		int m_argLength;       // ARG 数组长度
		int m_argsLength;      // ARGS 数组长度

		int m_depth;           // 函数嵌套/系统深度判定（系统函数通常为0）[cite: 5]
		int m_index;           // 排序用索引[cite: 5]
		int m_fileIndex;       // 文件索引[cite: 5]
		QMap<QString, UserDefinedVariableData> m_privateVar;
};

#endif // FUNCTION_LABEL_LINE_H