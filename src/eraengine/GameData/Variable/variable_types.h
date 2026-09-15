#ifndef VARIABLE_TYPES_H
#define VARIABLE_TYPES_H

#include <QObject>
#include <QtGlobal>

class VariableTypes : public QObject
{
		Q_OBJECT

public:
		enum class Type : quint32 {
				Integer       = 0x00020000,
				String        = 0x00040000,
				Array1D       = 0x00080000,
				Array2D       = 0x08000000,
				Array3D       = 0x20000000,
				CharacterData = 0x00100000,
				Local         = 0x02000000,
				Global        = 0x04000000,
				Unchangeable  = 0x00400000,
				Calc          = 0x00800000,
				Constant      = 0x40000000
		};
		Q_ENUM(Type)

		enum class Scope : quint32 {
				Local         = 0x02000000,
				Global        = 0x04000000,
				CharacterData = 0x00100000
		};
		Q_ENUM(Scope)

		enum class Dimension : quint32 {
				None   = 0x00000000,
				OneD   = 0x00080000,
				TwoD   = 0x08000000,
				ThreeD = 0x20000000
		};
		Q_ENUM(Dimension)

		enum class Flag : quint32 {
				None         = 0x00000000,
				CanForbid    = 0x00010000,
				Unchangeable = 0x00400000,
				Calc         = 0x00800000,
				Constant     = 0x40000000
		};
		Q_ENUM(Flag)
};

// 变量类型元信息结构体
struct VariableTypeInfo {
		VariableTypes::Type type = VariableTypes::Type::Integer;
		VariableTypes::Scope scope = VariableTypes::Scope::Global;
		VariableTypes::Dimension dimension = VariableTypes::Dimension::None;
		VariableTypes::Flag flags = VariableTypes::Flag::None;

		int size1D = 0;
		int size2D = 0;
		int size3D = 0;

		bool isCharacterData = false;
		bool isString = false;
		bool isInteger = true;

		VariableTypeInfo() = default;

		VariableTypeInfo(VariableTypes::Type t,
										 VariableTypes::Scope s,
										 VariableTypes::Dimension d,
										 VariableTypes::Flag f,
										 int s1, int s2, int s3,
										 bool charaData, bool str, bool num)
				: type(t), scope(s), dimension(d), flags(f)
				, size1D(s1), size2D(s2), size3D(s3)
				, isCharacterData(charaData), isString(str), isInteger(num) {}
};

#endif // VARIABLE_TYPES_H