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
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <QtQml/qqmlregistration.h>

#include "variable_types.h"
#include "variable_config.h"
#include "era_binary_data.h"
#include "save_variable_map.h"
#include "ast/variable_table.h"
#include "ast/print_template.h"

// perf：变量名（eraTW/MUGEN 惯例）几乎都是大写，QString::toUpper() 即使无变化
// 也会 detach + 逐字符大小写转换。这里先扫一遍：**确实含小写字母**时才真正
// 转换到 scratch。判定按「ASCII 小写直判 + 非 ASCII 走 QChar::isLower()」：
//   · 纯 CJK/假名名字（每个字符都 >= 0x80）此前**每次**都要 toUpper() 分配 +
//     逐字符转换，而结果永远不变 —— eraTW 的日文变量名每次 storageName 都付。
//   · 全角小写 ａ(U+FF41) 属 Ll（isLower()==true），仍会正确映射成 Ａ；
//     全角大写/希腊/西里尔大写不触发，返回原串（toUpper 对它们本就是恒等）。
[[nodiscard]] inline const QString& eraUpperKey(const QString& s, QString& scratch) {
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= 0x80 && c.isLower())) {
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
        if ((u >= 'a' && u <= 'z') || (u >= 0x80 && c.isLower())) return s.toUpper();
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
		// 无 scope 的形态**只**叫 *InCurrentFrame：名字里就写着「用当前帧」。
		// 生产代码一律走下面的显式 scope 重载（scope 必须给，没有默认参数）。
		Q_INVOKABLE void setGlobalInt1DInCurrentFrame(const QString &name, int x, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt1DInCurrentFrame(const QString &name, int x) const;
		Q_INVOKABLE void setGlobalInt1D(const QString& name, int x, qint64 val, eravar::ScopeId scope);
		Q_INVOKABLE qint64 getGlobalInt1D(const QString& name, int x, eravar::ScopeId scope) const;
        // 私有初值直写（EraParseTable 的「调用计划」用）：storageKey 是装载期
        // 算好的 FUNC\x1fNAME 复合键，绕开每次调用的 storageName 作用域解析。
        // 语义与「作用域内裸名走 setGlobal{Int,Str}1D + ensureArraySize」等价：
        // 尺寸只扩不缩、初值逐项写（必要时顺带扩容）。
        void applyPrivateDefault(const QString& storageKey, bool isString, int ensureSize,
                                 const QList<qint64>& defaultInt, const QStringList& defaultStr);
		Q_INVOKABLE void setGlobalInt2DInCurrentFrame(const QString &name, int x, int y, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt2DInCurrentFrame(const QString &name, int x, int y) const;
		Q_INVOKABLE void setGlobalInt3DInCurrentFrame(const QString &name, int x, int y, int z, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt3DInCurrentFrame(const QString &name, int x, int y, int z) const;
		Q_INVOKABLE void setGlobalInt2D(const QString& name, int x, int y, qint64 val, eravar::ScopeId scope);
		Q_INVOKABLE qint64 getGlobalInt2D(const QString& name, int x, int y, eravar::ScopeId scope) const;
		Q_INVOKABLE void setGlobalInt3D(const QString& name, int x, int y, int z, qint64 val,
		                                eravar::ScopeId scope);
		Q_INVOKABLE qint64 getGlobalInt3D(const QString& name, int x, int y, int z,
		                                  eravar::ScopeId scope) const;

		// ================= 名字级整数访问的唯一原语 =================
		// 「名字 + 下标 -> 整数存储」只有这一份实现；判定顺序（行序即语义）
		// 只在私有 resolveIntLocus 里定义一次，本组函数只按 IntLocusKind 施加。
		// 大小写规范化统一走 eraUpperKey（不再混用 toUpper）。
		// 读路径 const：内部访问函数本身是 const，无需 const_cast。
		// scope = 解析私有名的位置（0 = 顶层）。键自足的调用方（readIntNode）
		// 只在本原语的 Dynamic 分支才用到它。
		[[nodiscard]] qint64 readIntByName(const QString& name, const QList<int>& indices,
		                                   eravar::ScopeId scope) const;
		void writeIntByName(const QString& name, const QList<int>& indices, qint64 value,
		                    eravar::ScopeId scope);

		// 用户数组声明维数（1/2/3）：由 ExpressionEvaluator（与旧 lhsDimension
		// 同源：VariableTable::find(name, ownerFunction)）注入。
		using IntDimProvider = std::function<int(const QString&)>;
		void setIntDimProvider(IntDimProvider p) { m_intDimProvider = std::move(p); }
		[[nodiscard]] int intDimension(const QString& name) const;

		// ================= 角色整型 (支持 2D/3D) =================
		Q_INVOKABLE void setCharaInt(const QString &name, int charaId, int index, qint64 value);
		Q_INVOKABLE qint64 getCharaInt(const QString &name, int charaId, int index) const;
		// 按运行时下标直接存取（不做 运行时->模板 翻译）：供角色列表自身（NO）使用
		void setCharaIntRaw(const QString &name, int charaId, int index, qint64 value);
		void setCharaStrRaw(const QString &name, int charaId, int index, const QString &value);
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
        struct CharacterEntry { int number = 0; bool special = false; };
        using CharacterSnapshot = QList<CharacterEntry>;
        CharacterSnapshot captureCharacterSnapshot(const QList<int>& indexes = {}) const;
        static bool decodeCharacterSnapshot(const QString& text, CharacterSnapshot& snapshot);
        static QString serializeCharacterSnapshot(const CharacterSnapshot& snapshot);
        void appendCharacterSnapshot(const CharacterSnapshot& snapshot);
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
        // 重载游戏：释放全部旧变量、CSV 模板和作用域，恢复构造默认值。
        void clearForReload();

		// ================= CSV 模板快照（CSVNAME/CSVBASE/CSVABL/… 用）=================
		// C# 里「CSV 模板」与「角色运行时数据」是两个存储：CSV* 函数读模板，
		// VAR:角色:下标 读运行时可变量。本移植把模板值直接写进了角色存储，
		// 故在装载结束时深拷贝一份作为模板快照（见 eraengine 的 loadConstantData）。
		void snapshotCharaTemplates();
		[[nodiscard]] qint64 csvCharaInt(const QString& name, int charaId, int index) const;
		[[nodiscard]] QString csvCharaStr(const QString& name, int charaId, int index) const;

        // C#1808 typed snapshots used by all engine persistence entry points.
        // 只登记指针；**可用性**由 saveReadiness() 回答（见下）。
        void setSaveVariableTable(const VariableTable* table) { m_saveVariableTable = table; }
        // ---- 存档前置条件（显式，不再靠调用顺序）----
        // captureBinarySave 按 erasave::Map（由 VariableTable 声明构建）取条目：
        //   * 声明表没接上 -> 只写内建条目，用户 SAVEDATA 变量静默丢失；
        //   * 声明表未定稿（resolveDimensions 未跑）-> 形状按声明文本猜，静默错。
        // 两种都不崩，所以必须可观察。返回值是数据；checkSaveReadiness() 负责
        // qWarning 到 stderr（不进 ParseDiagnostics），并返回是否 Ready。
        enum class SaveReadiness : quint8 { Ready, NoDeclarationTable, DeclarationsNotResolved };
        [[nodiscard]] SaveReadiness saveReadiness() const;
        [[nodiscard]] bool checkSaveReadiness(const char* operation) const;
        // 把 ERB/ERH 声明的变量恢复成初值（RESETDATA 与读档共用）。
        // 批量直写（存储键每声明一次 / memset 清零 / 下标内联），不再逐元素
        // 调 setGlobal*D + coordinates()（后者每个元素分配一个 QList）。
        void restoreDeclaredDefaults();
        EraBinaryData::Document captureBinarySave(EraBinaryData::FileType type,
                                                  const QList<int>& characters = {}) const;
        void applyBinarySave(const EraBinaryData::Document& document);

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

        [[nodiscard]] FlatStringValue getStringSlotInCurrentFrame(const QString& name, const QList<int>& indices = {}) const;
        // 显式作用域版本：Dynamic 解析用（名字 -> 该函数的私有槽）。
        [[nodiscard]] FlatStringValue getStringSlot(const QString& name, const QList<int>& indices,
                                                    eravar::ScopeId scope) const;
        bool setStringSlotInCurrentFrame(const QString& name, const QList<int>& indices, const FlatStringValue& value);
        bool setStringSlot(const QString& name, const QList<int>& indices,
                           const FlatStringValue& value, eravar::ScopeId scope);

        struct LocalContext {
            QList<qint64> integers;
            QList<FlatStringValue> strings;
            QList<qint64> argIntegers;
            QList<FlatStringValue> argStrings;
            QHash<QString, int> aliases;
            QHash<QString, QVariant> parameters;
            QHash<QString, QString> references;
            // 当前调用帧的私有作用域身份 = **段身份的 intern id**（0 = 顶层）。
            // 求值器把 scope 作为显式参数传入；这里只是帧快照的一部分。
            eravar::ScopeId privateScopeId = eravar::kGlobalScope;
        };
        LocalContext localContext() const {
            return {m_localIntVars, m_localStrVars, m_argIntVars, m_argStrVars,
                    m_localAliases, m_parameters, m_references,
                    m_privateScopeId};
        }
        void setLocalContext(const LocalContext& context) {
            m_localIntVars = context.integers;
            m_localStrVars = context.strings;
            m_argIntVars = context.argIntegers;
            m_argStrVars = context.argStrings;
            m_localAliases = context.aliases;
            m_parameters = context.parameters;
            m_references = context.references;
            m_privateScopeId = context.privateScopeId;
        }
        // 当前帧作用名的 intern id（0 = 无私有作用域）。
        [[nodiscard]] eravar::ScopeId privateScopeId() const { return m_privateScopeId; }
        // 填充某段的作用域名表（名字 -> 存储键），并把「当前帧」指向它。
        // scope = **段身份 intern id**（不是函数名）。名字表按 scopeId 常驻：
        // 不同段（含同名 @F 的两段）的私有名互不覆盖，换段只改指针。
        //
        // 装载期名表已安装（installPrivateScopeTable）后，本函数**只记录当前帧**，
        // names 参数不再参与任何构建 —— 「函数里有哪些私有变量」的唯一真相是
        // 声明（VariableTable::buildPrivateNameTable），装载期就定好了。
        // 签名保留：安装前的测试路径（先手工 setPrivateScope 再求值）仍用它建表。
        void setPrivateScope(eravar::ScopeId scope, const QStringList& names) {
            m_privateScopeId = scope;
            if (scope == eravar::kGlobalScope) return;
            if (m_privateTableInstalled) return;   // 单一真相：装载期名表
            // 未安装（装载前 / 纯测试的存储对象）：按调用方给的名字临时建表。
            // 已登记过的作用域直接复用（声明表装载后不变）。
            if (m_privateNames.contains(scope)) return;
            QHash<QString, QString> next;
            next.reserve(names.size());
            const QString& scopeText = eravar::nameOf(scope);
            for (const auto& name : names)
                next.insert(name.toUpper(),
                            eravar::makePrivateStorageKey(scopeText, name));
            m_privateNames.insert(scope, next);
        }
        // 安装装载期建立的私有名表（EraParseTable 在声明定稿后调用一次）。
        // **前置条件**：声明（VariableTable 的 Local 声明）已定稿 —— 由调用方
        // 保证，并且这里用 m_privateTableInstalled 把「已安装」变成可观察状态：
        // 安装后 setPrivateScope 的建表分支彻底关闭（换函数零分配）。
        // 重复安装允许（整表替换，不做增量合并）—— 重载会先 clearForReload。
        void installPrivateScopeTable(QHash<eravar::ScopeId, QHash<QString, QString>> table) {
            m_privateNames = std::move(table);
            m_privateTableInstalled = true;
        }
        // 名表是否已由装载期安装（false = setPrivateScope 仍按调用方名字建表）。
        [[nodiscard]] bool privateScopeTableInstalled() const { return m_privateTableInstalled; }
        // Emuera 的 `大文字小文字の違いを無視する:YES`（ICVariable）语义：
        // 标识符大小写不敏感，所有存储键统一用大写，写 A 与读 a 落到同一槽位。
        // 否则 `Mark`/`MARK` 会被当成两个变量，表现为「变量似乎不可变」。
        //
        // scope 是**显式参数**：键本身自带作用域（私有名 -> FUNC\x1fNAME），
        // 这里只是在「按名解析」（Dynamic）时按 scope 选择该函数的名表。
        // scope == 0 = 顶层：只有 REF 别名与裸名。
        QString storageName(const QString& name, eravar::ScopeId scope) const {
            QString scratch;
            const QString& upper = eraUpperKey(name, scratch);
            if (scope != eravar::kGlobalScope) {
                const auto scopeIt = m_privateNames.constFind(scope);
                if (scopeIt != m_privateNames.constEnd()) {
                    const auto nameIt = scopeIt.value().constFind(upper);
                    if (nameIt != scopeIt.value().constEnd())
                        return m_references.value(upper, nameIt.value());
                }
            }
            return m_references.value(upper, upper);
        }
        // 当前帧快捷重载（仅存储层内部 / 未接线调用方使用）。
        QString storageNameInCurrentFrame(const QString& name) const {
            return storageName(name, m_privateScopeId);
        }
        void setReference(const QString& name, const QString& targetStorage) {
            m_references.insert(eraUpperKey(name), targetStorage);
        }
        [[nodiscard]] bool hasReference(const QString& name) const {
            // evaluateVariable 每次读变量都要判一次 REF：走零分配的 eraUpperKey
            return m_references.contains(eraUpperKey(name));
        }
        // REF 形参别名解析：`#DIM REF ターゲット` ← 实参 TARGET 时，m_references 把
        // 形参名映射到实参的存储键（"TARGET"）。系统变量按**名字**分派到各自数组
        // （m_target / m_flag …），并不走 m_intSlots —— 若不做这一步解析，
        // 「REF 到系统变量」的读写会落到同名用户全局槽（恒 0）。eraTW 画像表示
        // 的 `CALL PRINT_TARGET_IMAGE(TARGET)`（TARGET 是系统变量）正是此例：
        // 函数内 `SIF !ターゲット` 读成 0 提前 RETURN → 「示例立絵」整块不显示。
        // 名字未登记引用时原样返回（普通变量/私有作用域不受影响）。
        [[nodiscard]] QString systemVariableName(const QString& name) const {
            QString scratch;
            const QString& upper = eraUpperKey(name, scratch);
            return m_references.value(upper, upper);
        }
        [[nodiscard]] QString resolvedStorageNameInCurrentFrame(const QString& name) const { return storageNameInCurrentFrame(name); }
        // 显式作用域版本：求值器的 Dynamic 解析链只认参数，不读当前帧。
        [[nodiscard]] QString resolvedStorageName(const QString& name, eravar::ScopeId scope) const {
            return storageName(name, scope);
        }
        // ---- 数组「形状」的唯一查询：维数 / 每维长度 / 元素总数 ----
        // 回落顺序**只在这里**知道，且只看装载期事实：
        //   1) 脚本声明（VariableDecl::lengths / elementCount，装载期 resolveDimensions 定稿）
        //      —— 声明表是形状的所有者；键先经 storageName（REF 别名/私有键）解析，
        //      复合键再拆回 (名字, 段身份文本) 查声明；
        //   2) 无声明 -> VariableConfig（VariableSize.csv + 默认表）：**装载期**尺寸来源；
        //   3) 都没有 -> 多维容器（2D/3D）的**外接几何**（运行期才长出来的数组，
        //      如 REF 指向的未声明数组）；1D 容器槽位数不是形状（见 allocatedSlots）。
        //   4) 都没有 -> 空形状（未声明的 1D 名字没有形状）。
        // 容器当前分配量**不是**形状（见 allocatedSlots）。
        struct ArrayShape {
            QList<int> lengths;      // 每维长度（空 = 无形状）
            qint64 elementCount = 0; // 各维乘积
            [[nodiscard]] int dimension() const { return int(lengths.size()); }
            [[nodiscard]] int lengthOf(int dim) const {
                return (dim >= 0 && dim < lengths.size()) ? lengths.at(dim) : 0;
            }
            [[nodiscard]] bool isEmpty() const { return lengths.isEmpty(); }
        };
        [[nodiscard]] ArrayShape shapeOf(const QString& name, eravar::ScopeId scope) const;
        // 已解析存储键版本（调用方已经做过 storageName 解析时用）。
        [[nodiscard]] ArrayShape shapeOfKey(const QString& storageKey) const;

        // 「数组长度」：有形状 = 第 0 维长度（唯一形状入口）；没有形状的名字
        // （未声明的运行时 1D / 未定稿 DYNAMIC）才报**已分配槽位数**（容器事实，非形状）。
        // 单参重载 = **当前帧**（旧语义，等价于 explicit 的 privateScopeId）；要按
        // 某个段问私有变量就传 scope。
        [[nodiscard]] int arraySizeInCurrentFrame(const QString& name) const;
        [[nodiscard]] int arraySize(const QString& name, eravar::ScopeId scope) const;
        // 容器当前已分配的槽位数（**不是形状**：形状是装载期事实，见 shapeOf）。
        [[nodiscard]] int allocatedSlots(const QString& storageKey) const;
        [[nodiscard]] QList<int> arrayLengthsInCurrentFrame(const QString& name) const;
        [[nodiscard]] QList<int> arrayLengths(const QString& name, eravar::ScopeId scope) const;
        void ensureArraySizeInCurrentFrame(const QString& name, int size, bool stringArray);
        // 显式作用域版本（装载期按声明自己的段身份扩容私有数组）。
        void ensureArraySize(const QString& name, int size, bool stringArray, eravar::ScopeId scope);

        // ---- 多维数组（dimension >= 2）：与 1D **同一个**扁平容器，按几何分配 ----
        // 每个存储键只有一条扁平槽位序列（m_intSlots / m_strSlots）；维数不决定
        // 容器，只决定取址所用的几何：offset = Σ idx[d]·strides[d]（唯一公式
        // 在 addressOf）。1D 的 strides 是隐式的 {1}，不另立容器。
        //
        // 上限依据（2026-10 五语料实测）：纯数字多维声明的乘积中位数 600/995/21，
        // p90 20000/11688/8000，最大值 32000/1000000/30000（eraTW/eraMegaten/
        // FL3.8）。1,000,000 是 eraMegaten 的三个 100x100x100 迷宫；没有任何
        // 声明 > 1e6。4,000,000（int64 约 32 MB/变量）留 4 倍余量，仍能挡住
        // 「乘积写错一个零」的声明。超过上限：不分配、qWarning 一条装载期诊断，
        // 运行期读写按越界处理（读 0 / 写忽略）。
        static constexpr qint64 kMaxMultiDimElements = 4'000'000;
        // 安装一个多维变量的几何并按需分配扁平槽位。重复安装（同键同形状）是
        // no-op；形状冲突时保留首次（与声明表 first-wins 同口径）。
        // allocate=false：只登记几何（CHARADATA 的元素在角色容器里，每个角色
        // 一份；这里登记是为了让 VARSIZE/尺寸口径与声明一致，并给角色 stride）。
        void installMultiDimArray(const QString& storageKey, const QList<int>& lengths,
                                  const QList<int>& strides, bool stringArray,
                                  bool allocate = true);
        // 声明各维长度的**唯一运行时视图**（装载期从 VariableDecl 拷入）。未登记返回空。
        [[nodiscard]] QList<int> multiDimLengths(const QString& storageKey) const;
        // 扁平槽位序列实际占用的元素数 / 字节数（装载期诊断用：确认「真实分配」
        // 的量级与 VmHWM 的增量对得上）。
        [[nodiscard]] qint64 multiDimAllocatedElements() const;
        [[nodiscard]] qint64 multiDimAllocatedBytes() const;
        [[nodiscard]] bool isMultiDimArray(const QString& storageKey) const {
            return m_geometries.contains(storageKey);
        }
        bool hasParameter(const QString& name) const { return m_parameters.contains(eraUpperKey(name)); }
        QVariant parameter(const QString& name) const { return m_parameters.value(eraUpperKey(name)); }
        void setParameter(const QString& name, const QVariant& value) { m_parameters.insert(eraUpperKey(name), value); }

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

		// ITEMPRICE：C# 是**常量** 1D（VariableCode.ITEMPRICE | __UNCHANGEABLE__，
		// VariableData.cs:252 的 Int1DConstantToken(constant.ItemPrice)），
		// 由 ConstantData.LoadData 的 `loadDataTo("ITEM.CSV", itemIndex, ItemPrice)`
		// 从 Item.csv 第 3 列装载。此前 9388aec 把价格写进用户全局槽，而 ERB 的
		// `ITEMPRICE:i` 读取走 hasSystemVariable -> getSystemVariable（无该分支 -> 恒 0），
		// 于是「商店价格全为 $0」回归（eraTW 隙間商店）。这里补上常量容器。
		Q_INVOKABLE void setItemprice(int index, qint64 value);
		Q_INVOKABLE qint64 getItemprice(int index) const;
		// 装载期重建（C# `ItemPrice = new Int64[MaxDataList[itemIndex]]`）：按当前
		// VariableSize.csv 的尺寸清零，随后逐行填第 3 列。
		void resetItemprice();
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
		Q_INVOKABLE void setGlobalStr1DInCurrentFrame(const QString &name, int x, const QString &value);
		Q_INVOKABLE QString getGlobalStr1DInCurrentFrame(const QString &name, int x) const;
		Q_INVOKABLE void setGlobalStr1D(const QString& name, int x, const QString& value,
		                                eravar::ScopeId scope);
		Q_INVOKABLE QString getGlobalStr1D(const QString& name, int x, eravar::ScopeId scope) const;
        // 传入已经解析过作用域/REF 的内部存储键；不再次套用当前作用域。
        [[nodiscard]] QString getGlobalStr1DRaw(const QString& storageKey, int x) const;
        // 键已解析的 getStringSlot：返回值**带 markup 分片**（HTML 累积/打印模板靠它）
        [[nodiscard]] FlatStringValue getStringSlotRaw(const QString& storageKey, const QList<int>& indices) const;
        // Raw 访问族（装载期「变量分词」的运行期直取路径用）：键是收尾期算好的
        // 静态存储键（私有名 = FUNC\x1fNAME 复合键）。越界/缺槽返回 0/空串，
        // 写入侧按需扩容 —— 与按名访问的 get/setGlobal*D 语义一致，只是省掉
        // 每次访问的 storageName 解析（eraUpperKey 扫描 + 两次哈希）。
        [[nodiscard]] qint64 getGlobalInt1DRaw(const QString& storageKey, int x) const;
        [[nodiscard]] qint64 getGlobalInt2DRaw(const QString& storageKey, int x, int y) const;
        [[nodiscard]] qint64 getGlobalInt3DRaw(const QString& storageKey, int x, int y, int z) const;
        [[nodiscard]] QString getGlobalStr2DRaw(const QString& storageKey, int x, int y) const;
        [[nodiscard]] QString getGlobalStr3DRaw(const QString& storageKey, int x, int y, int z) const;
        void setGlobalInt1DRaw(const QString& storageKey, int x, qint64 value);
        void setGlobalInt2DRaw(const QString& storageKey, int x, int y, qint64 value);
        void setGlobalInt3DRaw(const QString& storageKey, int x, int y, int z, qint64 value);

        // ---- VariableKey 入口（运行期「变量分词」直取路径）----
        // id -> 存储层字符串键的解析**只有 storageKeyFor 一处**：内部按
        // (scope<<32)|name 记忆化，热路径摊到一次 quint64 哈希，绝不重新拼串。
        // 返回的引用指向 std::deque 里的 QString（元素地址在 push_back 之后不变）。
        [[nodiscard]] const QString& storageKeyFor(const eravar::VariableKey& key) const;
        [[nodiscard]] qint64 getGlobalInt1DRaw(const eravar::VariableKey& k, int x) const
        { return getGlobalInt1DRaw(storageKeyFor(k), x); }
        [[nodiscard]] qint64 getGlobalInt2DRaw(const eravar::VariableKey& k, int x, int y) const
        { return getGlobalInt2DRaw(storageKeyFor(k), x, y); }
        [[nodiscard]] qint64 getGlobalInt3DRaw(const eravar::VariableKey& k, int x, int y, int z) const
        { return getGlobalInt3DRaw(storageKeyFor(k), x, y, z); }
        [[nodiscard]] FlatStringValue getStringSlotRaw(const eravar::VariableKey& k,
                                                      const QList<int>& indices) const
        { return getStringSlotRaw(storageKeyFor(k), indices); }
        void setGlobalInt1DRaw(const eravar::VariableKey& k, int x, qint64 value)
        { setGlobalInt1DRaw(storageKeyFor(k), x, value); }
        void setGlobalInt2DRaw(const eravar::VariableKey& k, int x, int y, qint64 value)
        { setGlobalInt2DRaw(storageKeyFor(k), x, y, value); }
        void setGlobalInt3DRaw(const eravar::VariableKey& k, int x, int y, int z, qint64 value)
        { setGlobalInt3DRaw(storageKeyFor(k), x, y, z, value); }
		Q_INVOKABLE void setGlobalStr2DInCurrentFrame(const QString &name, int x, int y, const QString &value);
		Q_INVOKABLE QString getGlobalStr2DInCurrentFrame(const QString &name, int x, int y) const;
		Q_INVOKABLE void setGlobalStr2D(const QString& name, int x, int y, const QString& value,
		                                eravar::ScopeId scope);
		Q_INVOKABLE QString getGlobalStr2D(const QString& name, int x, int y,
		                                   eravar::ScopeId scope) const;

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

private:
		// ---- 「名字 + 下标 -> 整数存储」的解析结果 ----
		// 判定顺序**只在 resolveIntLocus 的 if 链里定义一次**（行序即语义）；
		// readIntByName / writeIntByName 只负责按 kind 施加，不再各自复述顺序。
		enum class IntLocusKind : quint8 {
			Parameter,      // m_parameters 值绑定（按名，不看 scope）
			CharaInt1D,     // 角色数据，元素维 <= 1
			CharaInt2D,     // 角色数据，元素维 >= 2
			CharaStrAsInt,  // 角色数据但是字符串变量：读 0 / 写忽略
			Arg,            // ARG:n
			Local,          // LOCAL:n
			StringSlot,     // ARGS / LOCALS / RESULTS（整型读落到 1D 槽 / 写忽略）
			System,         // 系统变量（FLAG/DA/TA/...）
			Global3D, Global2D, Global1D
		};
		struct IntLocus {
			IntLocusKind kind = IntLocusKind::Global1D;
			QString key;   // storageName(name, scope) 的结果；只在需要的 kind 上填写
			int charaId = 0;
			int e0 = 0, e1 = 0, e2 = 0;
		};
		[[nodiscard]] IntLocus resolveIntLocus(const QString& name, const QList<int>& indices,
		                                       eravar::ScopeId scope) const;

		// ---- 「名字 -> 字符串存储」的解析结果 ----
		// 判定顺序**只在 resolveStrLocus 的 if 链里定义一次**（行序即语义）；
		// getStringSlot / getStringSlotRaw / setStringSlot 只按 kind 施加，
		// 读一个 switch、写一个 switch（与 IntLocus 同形），漏 case 编译失败。
		enum class StrLocusKind : quint8 {
			NegativeIndex,  // 任一维负下标：读写都不发生（读空 / 写 false）
			LocalStr,       // LOCALS:x
			ArgStr,         // ARGS:x
			SavedataText,   // SAVEDATA_TEXT：单槽（任意下标同一格）
			CharaStr,       // 角色字符串变量：角色块 + 元素偏移
			GlobalStr       // 普通扁平槽（1D/2D/3D 同一容器）
		};
		struct StrLocus {
			StrLocusKind kind = StrLocusKind::GlobalStr;
			QString key;          // storageName(name, scope) 的结果（或已解析的键）
			int x = 0;            // 第 0 个下标（LOCALS / ARGS 用）
			int charaSlot = 0;    // 已翻译的角色槽（resolveCharaIndex 之后）
			QList<int> elements;  // 角色元素下标（reduceCharaArgs 的结果）
		};
		// 入参是「名字」还是「已经解析过的存储键」。Raw 入口传 ResolvedKey：
		// 同一个 if 链，只是第 1 步不再套 storageName（键不重新按 scope 解析；
		// 此时 scope 参数不参与任何查询，传 kGlobalScope 即可）。
		enum class StrKeyState : quint8 { Name, ResolvedKey };
		[[nodiscard]] StrLocus resolveStrLocus(const QString& nameOrKey, const QList<int>& indices,
		                                       eravar::ScopeId scope, StrKeyState keyState) const;
		// 读侧唯一的施加点（getStringSlot 与 getStringSlotRaw 共用）：
		// 返回 FlatStringValue（含 markup 分片），不在这里做任何解析。
		[[nodiscard]] FlatStringValue readStrLocus(const StrLocus& locus,
		                                           const QList<int>& indices) const;

		// ---- 存档映射层的存储侧：条目（erasave::Entry）-> 容器取值/写值 ----
		// 只做「容器类型不同」的分派；条目属于哪个容器由 save_variable_map 决定。
		[[nodiscard]] EraBinaryData::Variable readSaveEntry(const erasave::Entry& entry, int charaSlot) const;
		void writeSaveEntry(const erasave::Entry& entry, int charaSlot, int x, int y, int z,
		                    qint64 number, const QString& text);

		// ---- 扁平槽位序列：一个存储键 = 一条序列，形态只有这一种 ----
		// 全局/私有（1D 与多维同一容器）：
		QHash<QString, QList<qint64>>         m_intSlots;
		QHash<QString, QList<FlatStringValue>> m_strSlots;
		// CHARADATA：**碎片化角色号 → 连续 arena**。
		// 每个存储键有自己的 arena（连续槽位）与基址表（角色号 → 该角色块的起址）。
		// 只为**写过的**角色号追加一块（块长 = 该变量每角色元素数 = 几何 elementCount）；
		// 未出现在基址表里的角色号 = 未分配 —— 读一律 0 / 空串，且**读路径不分配**
		// （读必须是纯的：同一状态读多少次，容器都一模一样）。
		// 旧布局是「角色号 × 每角色元素数」直接当 stride，模板号稀疏时（eraMegaten
		// 模板号到 9002、每角色 7505 个元素，有用叶 4.33M）会撑到 65M+ 槽。
		template <typename Cell>
		struct CharaArena {
			QList<Cell> cells;          // 连续槽位（各角色的块首尾相接）
			QHash<int, qint64> bases;   // 角色号 -> 块起址；缺失 = 未分配
			// **唯一**的角色取址公式：块基址 + 元素偏移（未分配 -> -1）。
			[[nodiscard]] qint64 slotOf(int charaSlot, qint64 elementOffset) const {
				const auto it = bases.constFind(charaSlot);
				if (it == bases.constEnd()) return -1;
				return it.value() + elementOffset;
			}
			// 写路径唯一的分配点：未分配时在 arena 尾部追加一块并登记基址。
			// 已分配 -> 返回既有基址（块长只由几何决定，不在这里改）。
			qint64 ensureBlock(int charaSlot, qint64 elementCount) {
				if (charaSlot < 0 || elementCount <= 0) return -1;
				if (const auto it = bases.constFind(charaSlot); it != bases.constEnd())
					return it.value();
				const qint64 base = cells.size();
				bases.insert(charaSlot, base);
				cells.resize(qsizetype(base + elementCount));
				return base;
			}
		};
		QHash<QString, CharaArena<qint64>>         m_charaIntSlots;
		QHash<QString, CharaArena<FlatStringValue>> m_charaStrSlots;
		// CSV 模板快照（与运行时**同一布局**：装载期模板号就是角色号，快照连基址表
		// 一起复制；此后两者各自独立生长）。
		QHash<QString, CharaArena<qint64>>         m_csvIntSlots;
		QHash<QString, CharaArena<FlatStringValue>> m_csvStrSlots;

		// ---- 取址几何：唯一缓存，唯一写入点 installGeometry ----
		// 为什么缓存而不是每次调 shapeOf：shapeOf 要拆私有键 + 查声明表/CSV 表
		// （每元素 2~3 次哈希 + storageName 解析），且**不提供 strides**；扁平
		// 容器的每次读写都要 strides。缓存的是 shapeOf 的同一份事实（声明乘积），
		// 不是第二份形状来源：写入点只有 installGeometry，形状仍只由 shapeOf 回答。
		struct SlotGeometry {
			QList<int> lengths;
			QList<int> strides;          // 行主序，最后一维连续
			qint64 elementCount = 1;     // 每键（每角色）的元素数；标量 = 1
			bool   allocated = false;    // 装载期已按 elementCount 分配扁平槽位
			bool   authoritative = true; // true = 声明/CSV 定形（越界写忽略）；
			                             // false = 运行期外接几何（写时扩边界）
		};
		// mutable：**读**路径也可按需从 shapeOf 登记几何（首读不再依赖装载期
		// installMultiDimArray 跑过；写入点仍只有 installGeometry 一个）。
		mutable QHash<QString, SlotGeometry> m_geometries;      // 全局/私有键
		QHash<QString, SlotGeometry> m_charaGeometries; // CHARADATA 每角色元素几何
		// **唯一**的几何写入点（两个 map 都只经这里插入/扩边界）。
		static void installGeometry(QHash<QString, SlotGeometry>& map, const QString& storageKey,
		                            const QList<int>& lengths, const QList<int>& strides,
		                            qint64 elementCount, bool allocated, bool authoritative);
		// 几何按需登记：几何缺失时问 declaredShapeOfKey（shapeOf 的前两步 —— 声明 ->
		// VariableConfig），维数 >= 2 才登记。读/写前各调一次（幂等、一次哈希）。
		void ensureGeometry(const QString& storageKey) const;
		// **唯一**的取址公式：offset = Σ idx[d]·strides[d]。
		// geometry == nullptr = 无形状的 1D（offset = idx[0]，容器按需长）。
		// 返回 -1 = 无法取址（越界 / 下标个数超过维数 / 无几何的多维坐标）。
		[[nodiscard]] static qint64 addressOf(const SlotGeometry* geometry,
		                                      const QList<int>& indices);
		// **唯一**的角色块取址公式已收进 CharaArena::slotOf（基址表 + 元素偏移）。
		// 基址表只在两处写：CharaArena::ensureBlock（分配新块，写路径）与
		// restrideSlots（几何变化后整块重建）；读路径只查表，永不写。
		// 无几何（或运行期外接几何）的多维写：装/扩几何并按新 strides 重排既有
		// 数据（运行期才长出来的数组）。权威几何（声明/CSV）返回 -1（越界写忽略）。
		template <typename SlotList>
		qint64 extendExtents(QHash<QString, SlotGeometry>& map, const QString& storageKey,
		                     const QList<int>& indices, SlotList& slots);
		// 几何变化后把既有数据搬到新布局（extendExtents 用）：
		//   · 扁平序列：整条重排（行主序，最后一个元素也不丢）；
		//   · 角色 arena：逐块重排（每块 base..base+elementCount），基址按原角色号顺序重算。
		// 两种形态各一份实现，按容器类型重载 —— 不是按维数/按存储分派。
		template <typename Cell>
		static void restrideSlots(QList<Cell>& cells, const SlotGeometry& before,
		                          const SlotGeometry& after);
		template <typename Cell>
		static void restrideSlots(CharaArena<Cell>& arena, const SlotGeometry& before,
		                          const SlotGeometry& after);
		// CHARADATA：全部角色块归零（块与基址表都不动，只填 0 / 空串）。
		template <typename Cell>
		static void zeroCharaSlots(QHash<QString, CharaArena<Cell>>& map, const QString& key);
		// CHARADATA 写路径的唯一规则：取块（未分配则追加一块）-> 基址 + 元素偏移。
		// 返回 -1 = 拒绝（每角色元素数超过上限 / 角色号非法）。
		template <typename Cell>
		qint64 charaWriteSlot(QHash<QString, CharaArena<Cell>>& map, const QString& storageKey,
		                      int charaSlot, qint64 elementOffset, const QList<int>& elements);
		// 唯一的写规则：取址 -> 按需把容器长到 offset+1 -> 写。
		// 有权威几何时越界忽略（qWarning 一条）；无几何时按需增长（唯一的懒扩容）。
		bool writeIntSlot(const QString& storageKey, const QList<int>& indices, qint64 value,
		                  const char* operation);
		bool writeStrSlot(const QString& storageKey, const QList<int>& indices,
		                  const FlatStringValue& value, const char* operation);
		[[nodiscard]] qint64 readIntSlot(const QString& storageKey, const QList<int>& indices) const;
		[[nodiscard]] FlatStringValue readStrSlot(const QString& storageKey,
		                                          const QList<int>& indices) const;
		// CHARADATA：每角色一块，块基址在 CharaArena::bases（未分配 = 缺键）。
		[[nodiscard]] const SlotGeometry& charaGeometry(const QString& storageKey) const;
		void ensureCharaGeometry(const QString& storageKey);
		[[nodiscard]] qint64 readCharaIntSlot(const QString& storageKey, int charaSlot,
		                                      const QList<int>& elements) const;
		[[nodiscard]] FlatStringValue readCharaStrSlot(const QString& storageKey, int charaSlot,
		                                              const QList<int>& elements) const;
		void writeCharaIntSlot(const QString& storageKey, int charaSlot,
		                       const QList<int>& elements, qint64 value);
		void writeCharaStrSlot(const QString& storageKey, int charaSlot,
		                       const QList<int>& elements, const FlatStringValue& value);
		// 形状查询的**前两步**（声明 -> VariableConfig）。shapeOf 用它，CHARADATA
		// 的几何安装也用它 —— 同一个函数，没有第二份形状来源。
		[[nodiscard]] ArrayShape declaredShapeOfKey(const QString& storageKey) const;

		// 内置系统状态映射（例如 DAY, MONEY, FLAG 等）
		QHash<QString, qint64> m_systemIntVars;
		QHash<QString, FlatStringValue> m_systemStrVars;

		// 本地变量
		const VariableTable* m_saveVariableTable = nullptr;
		IntDimProvider m_intDimProvider;
		QHash<QString, QVariant> m_parameters;
        // 私有作用域名表：ScopeId（**段身份** intern id） -> name(upper) -> storage key。
        // 每段一张，互不覆盖；scope == 0 不在表中（顶层只有裸名）。
        // 装载期由声明一次建好（VariableTable::buildPrivateNameTable ->
        // installPrivateScopeTable），运行期只查不建。
        QHash<eravar::ScopeId, QHash<QString, QString>> m_privateNames;
        // 名表是否已由装载期安装：true = setPrivateScope 不再建表（零分配换帧）。
        // clearForReload / initialize（脚本换了一批）把它复位成 false，
        // 下一次装载收尾再装。
        bool m_privateTableInstalled = false;
        QHash<QString, QString> m_references;
        eravar::ScopeId m_privateScopeId = eravar::kGlobalScope; // 当前帧的段身份
        // VariableKey -> 存储键串 的记忆化：(scope<<32)|name -> 串在 m_storageKeyStrings 的下标。
        // 串放 std::deque（push_back 不搬迁既有元素）—— storageKeyFor 返回的
        // const QString& 在任何后续登记之后仍然有效，不依赖 QHash 节点实现细节。
        // mutable：只读访问也要登记（运行期单线程求值，与其余存储容器同口径）。
        mutable QHash<quint64, quint32> m_storageKeyCache;
        mutable std::deque<QString> m_storageKeyStrings;
		QList<qint64> m_localIntVars;
		QList<qint64> m_argIntVars;   // ARG（与 LOCAL 分离）
		QList<FlatStringValue> m_localStrVars;
		QList<FlatStringValue> m_argStrVars;  // ARGS
		QHash<QString, int> m_localAliases;   // 形参名 -> LOCAL 槽位

		// System variable containers (1D arrays)
		QList<qint64> m_day;
		QList<qint64> m_money;
		QList<qint64> m_time;
		QList<qint64> m_item;
		QList<qint64> m_itemsales;
		QList<qint64> m_itemprice;   // 常量（C# constant.ItemPrice），不参与 RESETDATA
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

		// Variable type information and identifiers
		QHash<QString, VariableTypeInfo> m_variableTypes;
		QHash<QString, VariableIdentifier> m_variableIdentifiers;
		// Variable configuration
		VariableConfig m_variableConfig;
		
		// ================= Expression Evaluation Methods =================

};

#endif // VARIABLE_STORAGE_H