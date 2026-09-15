#ifndef VARIABLE_STORAGE_H
#include <QVariant>
#define VARIABLE_STORAGE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QtQml/qqmlregistration.h>

#include "variable_types.h"
#include "variable_identifier.h"
#include "system_variables.h"
#include "variable_config.h"

class VariableStorage : public QObject
{
		Q_OBJECT
		QML_ELEMENT

public:
		explicit VariableStorage(QObject *parent = nullptr);

		Q_INVOKABLE void initialize(int maxCharacters, int localSize);

		// ================= 全局整型 (支持 1D/2D/3D) =================
		Q_INVOKABLE void setGlobalInt1D(const QString &name, int x, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt1D(const QString &name, int x) const;
		Q_INVOKABLE void setGlobalInt2D(const QString &name, int x, int y, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt2D(const QString &name, int x, int y) const;
		Q_INVOKABLE void setGlobalInt3D(const QString &name, int x, int y, int z, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt3D(const QString &name, int x, int y, int z) const;

		// ================= 角色整型 (支持 2D/3D) =================
		Q_INVOKABLE void setCharaInt(const QString &name, int charaId, int index, qint64 value);
		Q_INVOKABLE qint64 getCharaInt(const QString &name, int charaId, int index) const;
		Q_INVOKABLE void setCharaInt3D(const QString &name, int charaId, int x, int y, qint64 value);
		Q_INVOKABLE qint64 getCharaInt3D(const QString &name, int charaId, int x, int y) const;

		// ================= 本地变量 =================
		Q_INVOKABLE void setLocalInt(int index, qint64 value);
		Q_INVOKABLE qint64 getLocalInt(int index) const;
		Q_INVOKABLE void setLocalStr(int index, const QString &value);
		Q_INVOKABLE QString getLocalStr(int index) const;

		// ================= System variables (1D) =================
		Q_INVOKABLE void setDay(int index, qint64 value);
		Q_INVOKABLE qint64 getDay(int index) const;
		Q_INVOKABLE void setMoney(int index, qint64 value);
		Q_INVOKABLE qint64 getMoney(int index) const;
		Q_INVOKABLE void setItem(int index, qint64 value);
		Q_INVOKABLE qint64 getItem(int index) const;
		Q_INVOKABLE void setItemsales(int index, qint64 value);
		Q_INVOKABLE qint64 getItemsales(int index) const;
		Q_INVOKABLE void setNoitem(int index, qint64 value);
		Q_INVOKABLE qint64 getNoitem(int index) const;
		Q_INVOKABLE void setBought(int index, qint64 value);
		Q_INVOKABLE qint64 getBought(int index) const;
		Q_INVOKABLE void setPband(int index, qint64 value);
		Q_INVOKABLE qint64 getPband(int index) const;
		Q_INVOKABLE void setFlag(int index, qint64 value);
		Q_INVOKABLE qint64 getFlag(int index) const;
		Q_INVOKABLE void setTflag(int index, qint64 value);
		Q_INVOKABLE qint64 getTflag(int index) const;
		Q_INVOKABLE void setTarget(int index, qint64 value);
		Q_INVOKABLE qint64 getTarget(int index) const;
		Q_INVOKABLE void setMaster(int index, qint64 value);
		Q_INVOKABLE qint64 getMaster(int index) const;
		Q_INVOKABLE void setPlayer(int index, qint64 value);
		Q_INVOKABLE qint64 getPlayer(int index) const;
		Q_INVOKABLE void setAssi(int index, qint64 value);
		Q_INVOKABLE qint64 getAssi(int index) const;
		Q_INVOKABLE void setAssiplay(int index, qint64 value);
		Q_INVOKABLE qint64 getAssiplay(int index) const;
		Q_INVOKABLE void setUp(int index, qint64 value);
		Q_INVOKABLE qint64 getUp(int index) const;
		Q_INVOKABLE void setDown(int index, qint64 value);
		Q_INVOKABLE qint64 getDown(int index) const;
		Q_INVOKABLE void setLosebase(int index, qint64 value);
		Q_INVOKABLE qint64 getLosebase(int index) const;
		Q_INVOKABLE void setPalamlv(int index, qint64 value);
		Q_INVOKABLE qint64 getPalamlv(int index) const;
		Q_INVOKABLE void setExplv(int index, qint64 value);
		Q_INVOKABLE qint64 getExplv(int index) const;
		Q_INVOKABLE void setEjac(int index, qint64 value);
		Q_INVOKABLE qint64 getEjac(int index) const;
		Q_INVOKABLE void setPrevcom(int index, qint64 value);
		Q_INVOKABLE qint64 getPrevcom(int index) const;
		Q_INVOKABLE void setSelectcom(int index, qint64 value);
		Q_INVOKABLE qint64 getSelectcom(int index) const;
		Q_INVOKABLE void setNextcom(int index, qint64 value);
		Q_INVOKABLE qint64 getNextcom(int index) const;
		Q_INVOKABLE void setResult(int index, qint64 value);
		Q_INVOKABLE qint64 getResult(int index) const;
		Q_INVOKABLE void setCount(int index, qint64 value);
		Q_INVOKABLE qint64 getCount(int index) const;
		Q_INVOKABLE void setA(int index, qint64 value);
		Q_INVOKABLE qint64 getA(int index) const;
		Q_INVOKABLE void setB(int index, qint64 value);
		Q_INVOKABLE qint64 getB(int index) const;
		Q_INVOKABLE void setC(int index, qint64 value);
		Q_INVOKABLE qint64 getC(int index) const;

		// ================= Variable type checking methods =================
		Q_INVOKABLE bool isVariableInteger(const QString &name) const;
		Q_INVOKABLE bool isVariableString(const QString &name) const;
		Q_INVOKABLE bool isVariableLocal(const QString &name) const;
		Q_INVOKABLE bool isVariableGlobal(const QString &name) const;
		Q_INVOKABLE bool isVariableCharacterData(const QString &name) const;

		// ================= Variable dimension checking methods =================
		Q_INVOKABLE bool isVariable1D(const QString &name) const;
		Q_INVOKABLE bool isVariable2D(const QString &name) const;
		Q_INVOKABLE bool isVariable3D(const QString &name) const;

		// ================= Character variable type checking methods =================
		Q_INVOKABLE bool isCharaVariableInteger(const QString &name) const;
		Q_INVOKABLE bool isCharaVariableString(const QString &name) const;
		Q_INVOKABLE bool isCharaVariable1D(const QString &name) const;

		// ================= Save/Load methods =================
		Q_INVOKABLE bool saveVariables(const QString &filePath) const;
		Q_INVOKABLE bool loadVariables(const QString &filePath);
		
		// ================= Expression Evaluation =================
		Q_INVOKABLE QVariant evaluateExpression(const QString &expression);

private:
		// 扩展 3D 字符串及更多维度的全局容器
		QHash<QString, QList<QList<QList<QString>>>> m_globalStr3D;

		// 角色 2D/3D 变量容器
		QHash<QString, QList<QList<QList<qint64>>>> m_charaInt2D;
		QHash<QString, QList<QList<QList<QString>>>> m_charaStr2D;

		// 内置系统状态映射（例如 DAY, MONEY, FLAG 等）
		QHash<QString, qint64> m_systemIntVars;
		QHash<QString, QString> m_systemStrVars;

		// 全局变量容器 (支持 1D, 2D, 3D)
		QHash<QString, QList<qint64>> m_globalInt1D;
		QHash<QString, QList<QList<qint64>>> m_globalInt2D;
		QHash<QString, QList<QList<QList<qint64>>>> m_globalInt3D;

		QHash<QString, QList<QString>> m_globalStr1D;
		QHash<QString, QList<QList<QString>>> m_globalStr2D;

		// 角色变量容器 (支持 1D/2D 对应 CharaId + Index，可扩展到 3D)
		QHash<QString, QList<QList<qint64>>> m_charaIntVars;
		QHash<QString, QList<QList<QList<qint64>>>> m_charaIntVars3D;
		QHash<QString, QList<QList<QString>>> m_charaStrVars;

		// 本地变量
		QList<qint64> m_localIntVars;
		QList<QString> m_localStrVars;

		// System variable containers (1D arrays)
		QList<qint64> m_day;
		QList<qint64> m_money;
		QList<qint64> m_item;
		QList<qint64> m_itemsales;
		QList<qint64> m_noitem;
		QList<qint64> m_bought;
		QList<qint64> m_pband;
		QList<qint64> m_flag;
		QList<qint64> m_tflag;
		QList<qint64> m_target;
		QList<qint64> m_master;
		QList<qint64> m_player;
		QList<qint64> m_assi;
		QList<qint64> m_assiplay;
		QList<qint64> m_up;
		QList<qint64> m_down;
		QList<qint64> m_losebase;
		QList<qint64> m_palamlv;
		QList<qint64> m_explv;
		QList<qint64> m_ejac;
		QList<qint64> m_prevcom;
		QList<qint64> m_selectcom;
		QList<qint64> m_nextcom;
		QList<qint64> m_result;
		QList<qint64> m_count;
		QList<qint64> m_a;
		QList<qint64> m_b;
		QList<qint64> m_c;

		// Variable type information and identifiers
		QHash<QString, VariableTypeInfo> m_variableTypes;
		QHash<QString, VariableIdentifier> m_variableIdentifiers;
		// Variable configuration
		VariableConfig m_variableConfig;
		
		// ================= Expression Evaluation Methods =================

};

#endif // VARIABLE_STORAGE_H