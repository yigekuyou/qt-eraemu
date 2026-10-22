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
		Q_INVOKABLE void setGlobalInt1D(const QString &name, int x, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt1D(const QString &name, int x) const;
        // 私有初值直写（EraParseTable 的「调用计划」用）：storageKey 是装载期
        // 算好的 FUNC\x1fNAME 复合键，绕开每次调用的 storageName 作用域解析。
        // 语义与「作用域内裸名走 setGlobal{Int,Str}1D + ensureArraySize」等价：
        // 尺寸只扩不缩、初值逐项写（必要时顺带扩容）。
        void applyPrivateDefault(const QString& storageKey, bool isString, int ensureSize,
                                 const QList<qint64>& defaultInt, const QStringList& defaultStr);
		Q_INVOKABLE void setGlobalInt2D(const QString &name, int x, int y, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt2D(const QString &name, int x, int y) const;
		Q_INVOKABLE void setGlobalInt3D(const QString &name, int x, int y, int z, qint64 val);
		Q_INVOKABLE qint64 getGlobalInt3D(const QString &name, int x, int y, int z) const;

		// ================= 名字级整数访问的唯一原语 =================
		// 「名字 + 下标 -> 整数存储」只有这一份实现。语义顺序照抄旧
		// ExecutionEngine::writeLhs：
		//   hasParameter -> 角色数据变量 -> ARG/LOCAL -> ARGS/LOCALS/RESULTS
		//   （字符串槽，整型写忽略/读为 0）-> 系统变量
		//   -> 声明维数 >= 2 的 2D/3D -> 全局 1D。
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

        [[nodiscard]] FlatStringValue getStringSlot(const QString& name, const QList<int>& indices = {}) const;
        // 显式作用域版本：Dynamic 解析用（名字 -> 该函数的私有槽）。
        [[nodiscard]] FlatStringValue getStringSlot(const QString& name, const QList<int>& indices,
                                                    eravar::ScopeId scope) const;
        bool setStringSlot(const QString& name, const QList<int>& indices, const FlatStringValue& value);
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
        QString storageName(const QString& name) const {
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
        // 显式作用域版本：求值器的 Dynamic 解析链只认参数，不读当前帧。
        [[nodiscard]] QString resolvedStorageName(const QString& name, eravar::ScopeId scope) const {
            return storageName(name, scope);
        }
        [[nodiscard]] int arraySize(const QString& name) const;
        [[nodiscard]] int arraySizeRaw(const QString& storageKey) const;
        [[nodiscard]] QList<int> arrayLengths(const QString& name) const;
        void ensureArraySize(const QString& name, int size, bool stringArray);

        // ---- 多维数组（dimension >= 2）：独立容器，装载期按 lengths 乘积分配 ----
        // 单维变量仍走 m_globalInt1D / m_globalStr1D 的轻量 QList；多维不走那套
        // 「首维 + 懒扩容」，而是这里的一张扁平表 + 装载期算好的 strides。
        // 运行期寻址 = base + Σ idx*stride（strides 来自声明，不重新推导）。
        //
        // 上限依据（2026-10 五语料实测）：纯数字多维声明的乘积中位数 600/995/21，
        // p90 20000/11688/8000，最大值 32000/1000000/30000（eraTW/eraMegaten/
        // FL3.8）。1,000,000 是 eraMegaten 的三个 100x100x100 迷宫；没有任何
        // 声明 > 1e6。4,000,000（int64 约 32 MB/变量）留 4 倍余量，仍能挡住
        // 「乘积写错一个零」的声明。超过上限：不分配、qWarning 一条装载期诊断，
        // 运行期读写按越界处理（读 0 / 写忽略）。
        static constexpr qint64 kMaxMultiDimElements = 4'000'000;
        // 安装一个多维变量的形状并按需分配。重复安装（同键同形状）是 no-op；
        // 形状冲突时保留首次（与声明表 first-wins 同口径）。
        // allocate=false：只登记形状（CHARADATA 等多维变量由角色存储各自持有元素，
        // 这里登记是为了让 VARSIZE/尺寸口径与声明一致）。
        void installMultiDimArray(const QString& storageKey, const QList<int>& lengths,
                                  const QList<int>& strides, bool stringArray,
                                  bool allocate = true);
        // 声明各维长度的**唯一运行时视图**（装载期从 VariableDecl 拷入）。未登记返回空。
        [[nodiscard]] QList<int> multiDimLengths(const QString& storageKey) const;
        // 多维扁平容器实际占用的元素数 / 字节数（装载期诊断用：确认「真实分配」
        // 的量级与 VmHWM 的增量对得上）。
        [[nodiscard]] qint64 multiDimAllocatedElements() const;
        [[nodiscard]] qint64 multiDimAllocatedBytes() const;
        [[nodiscard]] bool isMultiDimArray(const QString& storageKey) const {
            return m_multiDimShapes.contains(storageKey);
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
		// 从 Item.csv 第 3 列装载。此前 9388aec 把价格写进 m_globalInt1D，而 ERB 的
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
		Q_INVOKABLE void setGlobalStr1D(const QString &name, int x, const QString &value);
		Q_INVOKABLE QString getGlobalStr1D(const QString &name, int x) const;
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

private:
		// ---- 存档映射层的存储侧：条目（erasave::Entry）-> 容器取值/写值 ----
		// 只做「容器类型不同」的分派；条目属于哪个容器由 save_variable_map 决定。
		[[nodiscard]] EraBinaryData::Variable readSaveEntry(const erasave::Entry& entry, int charaSlot) const;
		void writeSaveEntry(const erasave::Entry& entry, int charaSlot, int x, int y, int z,
		                    qint64 number, const QString& text);

		// 扩展 3D 字符串及更多维度的全局容器
		QHash<QString, QList<QList<QList<FlatStringValue>>>> m_globalStr3D;

        // ---- 多维数组（用户 #DIM/#DIMS 声明，dimension >= 2）----
        // 形状只此一份：装载期从 VariableDecl（lengths/strides/elementCount）拷入，
        // 之后运行期只读。QList 是 COW，拷贝不深拷元素。
        struct MultiDimShape {
            QList<int> lengths;
            QList<int> strides;
            qint64     elementCount = 0;   // 声明乘积（可能 > 上限）
            bool       allocated = false;  // false = 未分配（超限/维数未定稿）
        };
        QHash<QString, MultiDimShape> m_multiDimShapes;
        QHash<QString, QList<qint64>> m_globalIntN;
        QHash<QString, QList<FlatStringValue>> m_globalStrN;
        // 把一组下标换算成扁平偏移；形状不存在返回 nullopt（调用方回落到旧容器）。
        [[nodiscard]] std::optional<qint64> flattenMultiDim(const QString& storageKey,
                                                            const QList<int>& indices,
                                                            bool warnOnBound,
                                                            const char* operation) const;
        [[nodiscard]] qint64 readMultiDimInt(const QString& storageKey,
                                             const QList<int>& indices) const;
        void writeMultiDimInt(const QString& storageKey, const QList<int>& indices, qint64 value);
        [[nodiscard]] FlatStringValue readMultiDimStr(const QString& storageKey,
                                                      const QList<int>& indices) const;
        void writeMultiDimStr(const QString& storageKey, const QList<int>& indices,
                              const FlatStringValue& value);
        void clearMultiDimArrays() {
            m_multiDimShapes.clear();
            m_globalIntN.clear();
            m_globalStrN.clear();
        }

		// 角色 2D/3D 变量容器
		QHash<QString, QList<QList<QList<qint64>>>> m_charaInt2D;
		QHash<QString, QList<QList<QList<FlatStringValue>>>> m_charaStr2D;

		// 内置系统状态映射（例如 DAY, MONEY, FLAG 等）
		QHash<QString, qint64> m_systemIntVars;
		QHash<QString, FlatStringValue> m_systemStrVars;

		// 全局变量容器 (支持 1D, 2D, 3D)
		QHash<QString, QList<qint64>> m_globalInt1D;
		QHash<QString, QList<QList<qint64>>> m_globalInt2D;
		QHash<QString, QList<QList<QList<qint64>>>> m_globalInt3D;

		QHash<QString, QList<FlatStringValue>> m_globalStr1D;
		QHash<QString, QList<QList<FlatStringValue>>> m_globalStr2D;

		// 角色变量容器 (支持 1D/2D 对应 CharaId + Index，可扩展到 3D)
		QHash<QString, QList<QList<qint64>>> m_charaIntVars;
		QHash<QString, QList<QList<QList<qint64>>>> m_charaIntVars3D;
		QHash<QString, QList<QList<FlatStringValue>>> m_charaStrVars;

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
		// CSV 模板快照（按模板号存放）
		QHash<QString, QList<QList<qint64>>> m_csvIntVars;
		QHash<QString, QList<QList<QList<qint64>>>> m_csvIntVars3D;
		QHash<QString, QList<QList<FlatStringValue>>> m_csvStrVars;

		// Variable type information and identifiers
		QHash<QString, VariableTypeInfo> m_variableTypes;
		QHash<QString, VariableIdentifier> m_variableIdentifiers;
		// Variable configuration
		VariableConfig m_variableConfig;
		
		// ================= Expression Evaluation Methods =================

};

#endif // VARIABLE_STORAGE_H