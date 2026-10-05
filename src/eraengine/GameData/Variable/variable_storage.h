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
#ifndef VARIABLE_STORAGE_H
#include <QVariant>
#define VARIABLE_STORAGE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QtQml/qqmlregistration.h>

#include "variable_types.h"
#include "variable_config.h"

// perf：变量名（eraTW/MUGEN 惯例）几乎都是大写，QString::toUpper() 即使无变化
// 也会 detach + 逐字符大小写转换。这里先扫一遍：无 a-z 且无非 ASCII 字符时
// toUpper() 等价于原串，直接复用零分配；否则才真正转换到 scratch。
[[nodiscard]] inline const QString& eraUpperKey(const QString& s, QString& scratch) {
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || u >= 0x80) {
            scratch = s.toUpper();
            return scratch;
        }
    }
    return s;
}
// 便利重载：返回 QString（已是「大写等价」时只做共享引用，不分配）。
[[nodiscard]] inline QString eraUpperKey(const QString& s) {
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || u >= 0x80) return s.toUpper();
    }
    return s;
}

class VariableStorage : public QObject
{
		Q_OBJECT
		QML_ELEMENT

public:
		explicit VariableStorage(QObject *parent = nullptr);

		Q_INVOKABLE void initialize(int maxCharacters, int localSize);

		// 从 CSV 目录的 VariableSize.csv 读取系统数组尺寸（对齐 C# VariableData）
		bool loadVariableSizes(const QString& csvPath);
		[[nodiscard]] const VariableConfig& variableConfig() const { return m_variableConfig; }

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
		// 按运行时下标直接存取（不做 运行时->模板 翻译）：供角色列表自身（NO）使用
		void setCharaIntRaw(const QString &name, int charaId, int index, qint64 value);
		Q_INVOKABLE void setCharaInt3D(const QString &name, int charaId, int x, int y, qint64 value);
		Q_INVOKABLE qint64 getCharaInt3D(const QString &name, int charaId, int x, int y) const;
		// 角色字符串变量（CSTR / NAME / 用户 #DIMS CHARADATA）
		Q_INVOKABLE void setCharaStr(const QString &name, int charaId, int index, const QString &value);
		Q_INVOKABLE QString getCharaStr(const QString &name, int charaId, int index) const;

		// ================= 角色数据变量（CHARADATA）识别与实参规约 =================
		// Emuera 的角色变量写作 `VAR:角色:下标…`；角色维省略时取 TARGET。
		// 内建角色变量（CHARACTER_VARIABLES）在构造时登记；用户 `#DIM(S) CHARADATA X`
		// 由 EraParseTable 登记。elementDimension 是「每个角色的元素维数」
		// （NAME 之类标量为 0，CFLAG 为 1，二维角色数组为 2）。
		void registerCharaDataVariable(const QString &name, bool isString, int elementDimension);
		[[nodiscard]] bool isCharaDataVariable(const QString &name) const;
		[[nodiscard]] bool isCharaDataString(const QString &name) const;
		[[nodiscard]] int charaDataDimension(const QString &name) const;
		// 按 C# VariableParser.ReduceVariable 的实参规约得到 (角色号, 元素下标)
		void reduceCharaArgs(const QString &name, const QList<int> &indices,
		                     int &charaId, QList<int> &elements) const;

		// ================= 角色列表（ADDCHARA / DELCHARA / CHARANUM）=================
		// C# 里「已登记角色」是运行时列表，第 i 个角色的数据来自 CSV 模板 NO:i。
		// 本移植把模板数据按**模板号**存放，故这里维护 运行时下标 -> 模板号 的映射，
		// 角色数据访问统一经 resolveCharaIndex 翻译（列表为空时恒等，模板装载期安全）。
		void addChara(int csvNo);
		bool delChara(int index);
		// ADDVOIDCHARA（C#：何の設定のないキャラを作成）—— csvNo = -1 的空角色
		void addVoidChara() { m_charaList.append(-1); }
		// ADDSPCHARA（C#：SP キャラとして追加）—— SP 角色不参与 GETCHARA 常规检索
		void addSpChara(int csvNo);
		[[nodiscard]] bool isSpChara(int index) const { return m_charaSp.contains(index); }
		// DELALLCHARA（C#：DelAllCharacter）
		// ---- 训练流程的变量重置（对齐 C# VariableEvaluator）----
		// UpdateAfterShowUsercom：UP/DOWN/LOSEBASE（全局）+ 各角色 DOWNBASE/CUP/CDOWN 归零。
		//   在 @SHOW_USERCOM 结束、以及 DOTRAIN 强制训练前调用。
		void updateAfterShowUsercom();
		// UpdateAfterInputCom：各角色 NOWEX 归零（对齐 C#「选择中以外的角色也全部重置」）。
		//   在 @EVENTCOM 之前（callEventCom）调用。
		void updateAfterInputCom();
		// UpdateInBeginTrain：BEGIN TRAIN 入口（@EVENTTRAIN 之前）的全量复位。
		//   ASSIPLAY=0 / PREVCOM=-1 / NEXTCOM=-1 / TFLAG 全 0 / TSTR 全 "" +
		//   全角色 GOTJUEL/TEQUIP/EX/STAIN/PALAM/SOURCE/TCVAR 归零。
		//   **NEXTCOM 必须复位成 -1**：系统层 endCallEventTrain 依据
		//   「NEXTCOM >= 0」判定是否自动执行下一条调教指令（eramaker 的
		//   連続実行机制）；若沿用容器初值 0，起床后会直接跳进 COM0。
		void updateInBeginTrain();

		void delAllChara() { m_charaList.clear(); m_charaSp.clear(); }
		// SWAPCHARA / COPYCHARA / ADDCOPYCHARA / PICKUPCHARA（C# 同名命令）
		void swapChara(int a, int b);
		void copyChara(int dst, int src);
		void addCopyChara(int src);
		// PICKUPCHARA：角色列表重排为给定的运行时下标序列（可少于原数量）
		void pickupChara(const QList<int>& indexes);
		// SAVECHARA / LOADCHARA：角色清单（模板号 + SP 标记）的文本序列化
		[[nodiscard]] QString dumpCharaList() const;
		void appendCharaList(const QString& text);
		[[nodiscard]] int charaNum() const { return m_charaList.size(); }
		[[nodiscard]] int charaCsvNo(int index) const { return m_charaList.value(index, -1); }
		[[nodiscard]] int resolveCharaIndex(int index) const { return m_charaList.value(index, index); }
		void markCsvExists(int csvNo) { if (csvNo >= 0) m_existCsv.insert(csvNo); }
		[[nodiscard]] bool existCsv(int csvNo) const { return m_existCsv.contains(csvNo); }
		void clearCharaList() { m_charaList.clear(); m_charaSp.clear(); }

		// RESETGLOBAL（C#：全てのグローバル変数を初期化）
		void resetGlobals();

		// RESETDATA / 新开游戏（C# VariableEvaluator.ResetData）：
		// 全部运行时变量回默认值 + 角色列表清空。NAME 表 / STR 等 CSV 装载期
		// 默认值与模板快照保留。
		void resetForNewGame();

		// ================= CSV 模板快照（CSVNAME/CSVBASE/CSVABL/… 用）=================
		// C# 里「CSV 模板」与「角色运行时数据」是两个存储：CSV* 函数读模板，
		// VAR:角色:下标 读运行时可变量。本移植把模板值直接写进了角色存储，
		// 故在装载结束时深拷贝一份作为模板快照（见 eraengine 的 loadConstantData）。
		void snapshotCharaTemplates();
		[[nodiscard]] qint64 csvCharaInt(const QString& name, int charaId, int index) const;
		[[nodiscard]] QString csvCharaStr(const QString& name, int charaId, int index) const;

		// ================= 存档序列化（SAVEDATA/LOADDATA/DELDATA/CHKDATA 支持）=================
		// 对齐 C# VariableEvaluator.SaveTo/LoadFrom 的语义：
		//   * 文件名 save{index:00}.sav（C# VariableEvaluator.cs:1746）
		//   * 内容：唯一码/版本/SAVETEXT(PUTFORM)/角色清单/全局变量/系统变量
		//   * 格式为移植版行式文本 —— C# 的 EraDataWriter 格式不兼容，
		//     C# 存档不可载入（已知限制）；移植版自身存档可完整往返。
		Q_INVOKABLE QString dumpSaveData() const;
		Q_INVOKABLE void restoreSaveData(const QString& text);

		// ================= 本地变量 =================
		Q_INVOKABLE void setLocalInt(int index, qint64 value);
		Q_INVOKABLE qint64 getLocalInt(int index) const;
		Q_INVOKABLE void setLocalStr(int index, const QString &value);
		Q_INVOKABLE QString getLocalStr(int index) const;

		// ---- 函数实参 ARG / ARGS ----
		// Emuera 里 ARG / ARGS 与 LOCAL / LOCALS 是**两套独立的数组**：
		// ARG 保存实参，LOCAL 是函数的局部变量。二者混用会让 `LOCAL:2 = …`
		// 覆盖掉 `ARG:2`（eraTW 的 PRINT_COLORBAR 正是「先读 ARG:2 当循环上界，
		// 再写 LOCAL:2 保存颜色」，别名实现下上界被改写成颜色值 → 死循环）。
		void setArgInt(int index, qint64 value);
		[[nodiscard]] qint64 getArgInt(int index) const;
		void setArgStr(int index, const QString& value);
		[[nodiscard]] QString getArgStr(int index) const;

		// #LOCALSIZE：把当前函数的 LOCAL/LOCALS 扩到 n（只扩不缩，保留静态值）
		void ensureLocalSize(int n);
		[[nodiscard]] int localIntSize() const { return m_localIntVars.size(); }
		[[nodiscard]] int localStrSize() const { return m_localStrVars.size(); }

		// 局部变量名别名：用户函数形参(@F(A,B)) -> LOCAL 槽位，供表达式解析 A/B
		void setLocalAlias(const QString &name, int index);
		int  localAliasIndex(const QString &name) const;
		QHash<QString, int> localAliases() const { return m_localAliases; }
		void setLocalAliases(const QHash<QString, int> &aliases) { m_localAliases = aliases; }
		void clearLocalAliases() { m_localAliases.clear(); }

        struct LocalContext {
            QList<qint64> integers;
            QList<QString> strings;
            QList<qint64> argIntegers;
            QList<QString> argStrings;
            QHash<QString, int> aliases;
            QHash<QString, QVariant> parameters;
            QHash<QString, QString> privateNames;
            QHash<QString, QString> references;
        };
        LocalContext localContext() const {
            return {m_localIntVars, m_localStrVars, m_argIntVars, m_argStrVars,
                    m_localAliases, m_parameters, m_privateNames, m_references};
        }
        void setLocalContext(const LocalContext& context) {
            m_localIntVars = context.integers;
            m_localStrVars = context.strings;
            m_argIntVars = context.argIntegers;
            m_argStrVars = context.argStrings;
            m_localAliases = context.aliases;
            m_parameters = context.parameters;
            m_privateNames = context.privateNames;
            m_references = context.references;
        }
        void setPrivateScope(const QString& function, const QStringList& names) {
            // perf：作用域只随函数名变化（声明表装载后不变）——按函数缓存构建结果，
            // 免去每次 CALL/RETURN 都对每个名字做 toUpper + 拼接 + 哈希插入。
            // 仍与 m_privateNames 比较（setLocalContext 恢复上下文后需重新落地）。
            const QString fkey = function.toUpper();
            const auto it = m_privateScopeCache.constFind(fkey);
            if (it == m_privateScopeCache.constEnd()) {
                QHash<QString, QString> next;
                for (const auto& name : names)
                    next.insert(name.toUpper(), fkey + QChar(0x1f) + name.toUpper());
                const auto ins = m_privateScopeCache.insert(fkey, next);
                if (ins.value() != m_privateNames) m_privateNames = ins.value();
                return;
            }
            if (it.value() != m_privateNames) m_privateNames = it.value();
        }
        // Emuera 的 `大文字小文字の違いを無視する:YES`（ICVariable）语义：
        // 标识符大小写不敏感，所有存储键统一用大写，写 A 与读 a 落到同一槽位。
        // 否则 `Mark`/`MARK` 会被当成两个变量，表现为「变量似乎不可变」。
        QString storageName(const QString& name) const {
            QString scratch;
            const QString& upper = eraUpperKey(name, scratch);
            return m_references.value(upper, m_privateNames.value(upper, upper));
        }
        void setReference(const QString& name, const QString& targetStorage) {
            m_references.insert(name.toUpper(), targetStorage);
        }
        // REF 形参别名解析：`#DIM REF ターゲット` ← 实参 TARGET 时，m_references 把
        // 形参名映射到实参的存储键（"TARGET"）。系统变量按**名字**分派到各自数组
        // （m_target / m_flag …），并不走 m_globalInt1D —— 若不做这一步解析，
        // 「REF 到系统变量」的读写会落到同名用户全局槽（恒 0）。eraTW 画像表示
        // 的 `CALL PRINT_TARGET_IMAGE(TARGET)`（TARGET 是系统变量）正是此例：
        // 函数内 `SIF !ターゲット` 读成 0 提前 RETURN → 「示例立絵」整块不显示。
        // 名字未登记引用时原样返回（普通变量/私有作用域不受影响）。
        [[nodiscard]] QString systemVariableName(const QString& name) const {
            QString scratch;
            const QString& upper = eraUpperKey(name, scratch);
            return m_references.value(upper, upper);
        }
        [[nodiscard]] QString resolvedStorageName(const QString& name) const { return storageName(name); }
        [[nodiscard]] int arraySize(const QString& name) const;
        void ensureArraySize(const QString& name, int size, bool stringArray);
        bool hasParameter(const QString& name) const { return m_parameters.contains(name.toUpper()); }
        QVariant parameter(const QString& name) const { return m_parameters.value(name.toUpper()); }
        void setParameter(const QString& name, const QVariant& value) { m_parameters.insert(name.toUpper(), value); }

		// ================= System variables (1D) =================
		Q_INVOKABLE void setDay(int index, qint64 value);
		Q_INVOKABLE qint64 getDay(int index) const;
		Q_INVOKABLE void setMoney(int index, qint64 value);
		Q_INVOKABLE qint64 getMoney(int index) const;
		Q_INVOKABLE void setTime(int index, qint64 value);
		Q_INVOKABLE qint64 getTime(int index) const;
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

		// ================= 全局字符串变量 (1D/2D) =================
		// Emuera 的用户字符串变量（#DIMS/#GLOBALS 声明）；此前完全缺失。
		Q_INVOKABLE void setGlobalStr1D(const QString &name, int x, const QString &value);
		Q_INVOKABLE QString getGlobalStr1D(const QString &name, int x) const;
		Q_INVOKABLE void setGlobalStr2D(const QString &name, int x, int y, const QString &value);
		Q_INVOKABLE QString getGlobalStr2D(const QString &name, int x, int y) const;

		// ================= 系统字符串变量 =================
		// 如 RESULTS / SAVEDATA_TEXT；按 (名称, 下标) 存取
		Q_INVOKABLE void setSystemStr(const QString &name, int index, const QString &value);
		Q_INVOKABLE QString getSystemStr(const QString &name, int index) const;

		// ================= System Variable Access by Name =================
		// Helper methods to access system variables by name for expression evaluation
		Q_INVOKABLE qint64 getSystemVariable(const QString &name, int index) const;
		// 该名字是否有真正的系统变量存储槽（A–Z/DA–DE 等无槽）
		[[nodiscard]] bool hasSystemVariable(const QString &name) const;
		Q_INVOKABLE void setSystemVariable(const QString &name, int index, qint64 value);

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
		QHash<QString, QVariant> m_parameters;
        QHash<QString, QString> m_privateNames;
        QHash<QString, QString> m_references;
        // setPrivateScope 的按函数缓存（function(upper) -> name(upper) -> storage key）
        QHash<QString, QHash<QString, QString>> m_privateScopeCache;
		QList<qint64> m_localIntVars;
		QList<qint64> m_argIntVars;   // ARG（与 LOCAL 分离）
		QList<QString> m_localStrVars;
		QList<QString> m_argStrVars;  // ARGS
		QHash<QString, int> m_localAliases;   // 形参名 -> LOCAL 槽位

		// System variable containers (1D arrays)
		QList<qint64> m_day;
		QList<qint64> m_money;
		QList<qint64> m_time;
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

		// 角色数据变量元信息：名字(大写) -> {是否字符串, 元素维数}
		struct CharaDataInfo { bool isString = false; int dimension = 1; };
		QHash<QString, CharaDataInfo> m_charaDataVars;

		// 已登记角色：运行时下标 -> CSV 模板号；以及存在模板的番号集合（EXISTCSV）
		QList<int> m_charaList;
		QSet<int>  m_charaSp;   // SP 角色的运行时下标（ADDSPCHARA）
		QSet<int>  m_existCsv;
		// CSV 模板快照（按模板号存放）
		QHash<QString, QList<QList<qint64>>> m_csvIntVars;
		QHash<QString, QList<QList<QList<qint64>>>> m_csvIntVars3D;
		QHash<QString, QList<QList<QString>>> m_csvStrVars;

		// Variable type information and identifiers
		QHash<QString, VariableTypeInfo> m_variableTypes;
		QHash<QString, VariableIdentifier> m_variableIdentifiers;
		// Variable configuration
		VariableConfig m_variableConfig;
		
		// ================= Expression Evaluation Methods =================

};

#endif // VARIABLE_STORAGE_H