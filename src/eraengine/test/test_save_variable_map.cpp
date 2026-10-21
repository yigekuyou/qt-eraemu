// 存档映射层（erasave::Map）的验收测试：
//   1) **顺序确定性**：entries() 是数据决定的全序 —— 同一状态连 encode 两次
//      逐字节相同，且 == 新 golden（normal-new.bin / global-new.bin /
//      global-new.txt，md5 见下）。本测试**不**固定 QHash 种子：种子的直接
//      证据是 ctest 里的 test_save_variable_map_hashseed（QT_HASH_SEED=42）
//      与手工的 QT_HASH_SEED=1/42 双跑；
//   2) **兼容读**：旧 golden（normal-old.bin / global-old.bin / global-old.txt，
//      旧实现 + 固定播种写出）降级为解码/回放 fixture —— 必须仍能解码 +
//      applyBinarySave 且值正确（读按名还原，天然与写出顺序无关）；
//   3) 私有变量（函数内 #DIM SAVEDATA）经 save -> load 往返值不变；
//   4) 映射表本身的内容（名字 / 身份 / 容器 / 形状）与 (slot, 大写名) 结构序。
//
// 新 golden（排序后实现生成，2026-10-21）的 md5：
//   normal-new.bin  995642c9f1ca57ab21e0c358e434ea89（23886 字节，条目与
//                    normal-old.bin 相同、顺序为排序后的全序）
//   global-new.bin  ff92a82c95cbaeab9a7da50babaac9b7（98 字节）
//   global-new.txt  dfdf86e45e3d5c44a76250f92c9a2f23（205 字节；GLOBAL 档只有
//                    一个用户条目 UG，旧顺序恰好与排序一致，字节未变）
//
// 新 golden 的再生成（维护用隐藏开关）：
//   EMUERA_SAVE_MAP_REGEN_GOLDEN=1 ctest --test-dir build -R test_save_variable_map
//   把三个新 golden 写回 fixtures/save-map/，md5 打到 stderr —— 人工核对后入库，
//   并同步更新本文件头与各断言处的注释。
#include <QCoreApplication>
#include <QByteArray>
#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include "variable_storage.h"
#include "save_variable_map.h"
#include "ast/variable_table.h"
#include "era_binary_data.h"

namespace {

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(bytes);
}

QByteArray md5(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex();
}

// golden 状态的声明表：只含广域/角色数据声明（没有函数私有 savedata），
// 新旧实现（映射表 / 旧 saveShapes）对它产出的**条目集合**完全一致。
void addGoldenDeclarations(VariableTable& table)
{
    VariableDecl d;
    d.name = "S1"; d.isSavedata = true; d.lengths = {4}; table.add(d);
    d = {}; d.name = "S2"; d.isSavedata = true; d.dimension = 2; d.lengths = {2, 3}; table.add(d);
    d = {}; d.name = "S3"; d.isSavedata = true; d.dimension = 3; d.lengths = {2, 2, 2}; table.add(d);
    d = {}; d.name = "SNAME"; d.type = OperandType::Str; d.isSavedata = true; d.lengths = {3}; table.add(d);
    d = {}; d.name = "S2NAME"; d.type = OperandType::Str; d.isSavedata = true; d.dimension = 2;
    d.lengths = {2, 2}; table.add(d);
    d = {}; d.name = "UG"; d.isSavedata = true; d.isGlobal = true; d.isGlobalSave = true;
    d.lengths = {2}; table.add(d);
    d = {}; d.name = "UC"; d.isSavedata = true; d.isCharaData = true; d.lengths = {2}; table.add(d);
    d = {}; d.name = "TEMP"; d.isSavedata = false; d.lengths = {2}; table.add(d);
}

void buildGoldenState(VariableStorage& storage, VariableTable& table)
{
    addGoldenDeclarations(table);
    storage.setSaveVariableTable(&table);
    storage.registerCharaDataVariable("UC", false, 1);
    storage.setSystemVariable("DAY", 0, 7);
    storage.setGlobalInt1D("S1", 0, 11); storage.setGlobalInt1D("S1", 3, 14);
    storage.setGlobalInt2D("S2", 1, 2, 1234567890123LL);
    storage.setGlobalInt3D("S3", 1, 1, 1, -42);
    storage.setGlobalStr1D("SNAME", 0, QString::fromUtf8("héllo界😀\t"));
    storage.setGlobalStr1D("SNAME", 2, "x");
    storage.setGlobalStr2D("S2NAME", 1, 1, "two");
    storage.setGlobalInt1D("UG", 0, 4); storage.setGlobalInt1D("UG", 1, 5);
    storage.setGlobalInt1D("GLOBAL", 0, 999); storage.setGlobalStr1D("GLOBALS", 0, "gl");
    storage.appendCharacterSnapshot({{1, false}, {0, false}});
    storage.setCharaStrRaw("NAME", 0, 0, "zero"); storage.setCharaStrRaw("NAME", 1, 0, "one");
    storage.setCharaIntRaw("ABL", 0, 0, 101); storage.setCharaIntRaw("ABL", 1, 0, 202);
    storage.setCharaInt("UC", 0, 1, 55);
    storage.setGlobalInt1D("TEMP", 0, 77);
}

QByteArray encodeNormal(VariableStorage& storage)
{
    auto doc = storage.captureBinarySave(EraBinaryData::FileType::Normal);
    doc.description = "golden"; doc.uniqueCode = 42; doc.scriptVersion = 1808;
    QByteArray bytes;
    if (!EraBinaryData::encode(doc, bytes)) return {};
    return bytes;
}

// 结构序断言的核心：一段存档字节解出来的条目，(slot, 大写名) 必须非降
//（每个名字查回它在映射表里的行；findByName 命不中 = 集合都不对，直接失败）。
bool orderNonDecreasing(const QList<EraBinaryData::Variable>& vars, const erasave::Map& map)
{
    int lastSlot = -1;
    QString lastName;
    for (const auto& v : vars) {
        const erasave::Entry* e = map.findByName(v.name);
        if (!e) return false;
        const int slot = int(e->slot);
        const QString upper = v.name.toUpper();
        if (slot < lastSlot) return false;
        if (slot == lastSlot && QString::compare(upper, lastName) < 0) return false;
        lastSlot = slot;
        lastName = upper;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    // 注意：**不再** QHashSeed::setDeterministicGlobalSeed()。条目顺序由
    // erasave::Map::finalize() 排序保证，本进程内的哈希种子爱是什么是什么；
    // 种子无关的跨进程证据 = test_save_variable_map_hashseed（QT_HASH_SEED=42）
    // 与手工 QT_HASH_SEED=1/42 双跑（见任务报告）。
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { qCritical() << "FAIL:" << label; ++failures; }
    };
    const QString fixtures = QStringLiteral(ERA_TEST_SOURCE_DIR) + QStringLiteral("/fixtures/save-map");

    // ---- 1) 旧字节的兼容读（旧 golden 降级：解码 + applyBinarySave + 值核对）----
    {
        VariableStorage restored; restored.initialize(10, 10);
        VariableTable table;
        addGoldenDeclarations(table);
        restored.setSaveVariableTable(&table);
        restored.registerCharaDataVariable("UC", false, 1);
        EraBinaryData::Document doc;
        const QByteArray oldBytes = readFile(fixtures + QStringLiteral("/normal-old.bin"));
        check(!oldBytes.isEmpty(), "旧 NORMAL golden 文件在");
        check(EraBinaryData::decode(oldBytes, doc), "旧 NORMAL 存档（旧顺序字节）可解码");
        restored.applyBinarySave(doc);
        check(restored.getSystemVariable("DAY", 0) == 7, "旧 NORMAL 存档：系统 DAY 恢复");
        check(restored.getGlobalInt1D("S1", 0) == 11 && restored.getGlobalInt1D("S1", 3) == 14,
              "旧 NORMAL 存档：1D 用户 SAVEDATA 恢复");
        check(restored.getGlobalInt2D("S2", 1, 2) == 1234567890123LL,
              "旧 NORMAL 存档：2D 用户 SAVEDATA 恢复");
        check(restored.getGlobalInt3D("S3", 1, 1, 1) == -42,
              "旧 NORMAL 存档：3D 用户 SAVEDATA 恢复");
        check(restored.getGlobalStr1D("SNAME", 0) == QString::fromUtf8("héllo界😀\t")
              && restored.getGlobalStr1D("SNAME", 2) == "x",
              "旧 NORMAL 存档：1D 字符串数组恢复（含 UTF-16 增补面与制表符）");
        check(restored.getGlobalStr2D("S2NAME", 1, 1) == "two",
              "旧 NORMAL 存档：2D 字符串数组恢复");
        check(restored.getGlobalInt1D("TEMP", 0) == 0,
              "旧 NORMAL 存档：非 SAVEDATA 变量不随档恢复");
        check(restored.charaNum() == 2, "旧 NORMAL 存档：两个角色");
        // 旧实现按 m_charaList=[1,0] 从模板槽取数写档：file 角色 0 = "one"/202/55，
        // 角色 1 = "zero"/101；apply 后运行时序复原成同样的映射。
        check(restored.getCharaStr("NAME", 0, 0) == "one" && restored.getCharaStr("NAME", 1, 0) == "zero",
              "旧 NORMAL 存档：角色 NAME 恢复");
        check(restored.getCharaInt("ABL", 0, 0) == 202 && restored.getCharaInt("ABL", 1, 0) == 101,
              "旧 NORMAL 存档：角色 ABL 恢复");
        check(restored.getCharaInt("UC", 0, 1) == 55 && restored.getCharaInt("UC", 1, 1) == 0,
              "旧 NORMAL 存档：用户 CHARADATA 恢复");

        VariableStorage globalRestored; globalRestored.initialize(10, 10);
        VariableTable globalTable;
        addGoldenDeclarations(globalTable);
        globalRestored.setSaveVariableTable(&globalTable);
        EraBinaryData::Document globalDoc;
        const QByteArray oldGlobalBytes = readFile(fixtures + QStringLiteral("/global-old.bin"));
        check(!oldGlobalBytes.isEmpty() && EraBinaryData::decode(oldGlobalBytes, globalDoc),
              "旧 GLOBAL 存档可解码");
        globalRestored.setGlobalInt1D("UG", 0, 0); globalRestored.setGlobalStr1D("GLOBALS", 0, "bye");
        globalRestored.applyBinarySave(globalDoc);
        check(globalRestored.getGlobalInt1D("GLOBAL", 0) == 999, "旧 GLOBAL 存档：GLOBAL 恢复");
        check(globalRestored.getGlobalStr1D("GLOBALS", 0) == "gl", "旧 GLOBAL 存档：GLOBALS 恢复");
        check(globalRestored.getGlobalInt1D("UG", 1) == 5, "旧 GLOBAL 存档：用户 SAVEDATA 恢复");

        // 旧 GLOBAL 文本档（C# EraDataWriter 兼容行）：
        EraBinaryData::Document textSchema; textSchema.type = EraBinaryData::FileType::Global;
        EraBinaryData::Document textDoc;
        const QByteArray oldTextBytes = readFile(fixtures + QStringLiteral("/global-old.txt"));
        check(!oldTextBytes.isEmpty() && EraBinaryData::decodeText(oldTextBytes, textSchema, textDoc),
              "旧 GLOBAL 文本档可解码");
        VariableStorage textRestored; textRestored.initialize(10, 10);
        VariableTable textTable;
        addGoldenDeclarations(textTable);
        textRestored.setSaveVariableTable(&textTable);
        textRestored.applyBinarySave(textDoc);
        check(textRestored.getGlobalInt1D("GLOBAL", 0) == 999, "旧 GLOBAL 文本档：GLOBAL 恢复");
        check(textRestored.getGlobalStr1D("GLOBALS", 0) == "gl", "旧 GLOBAL 文本档：GLOBALS 恢复");
        check(textRestored.getGlobalInt1D("UG", 1) == 5, "旧 GLOBAL 文本档：用户 SAVEDATA 恢复");
    }

    // ---- 2) 顺序确定：encode 与种子无关，== 新 golden ----
    {
        VariableStorage storage; storage.initialize(10, 10);
        VariableTable table;
        buildGoldenState(storage, table);
        const QByteArray bytes = encodeNormal(storage);
        check(!bytes.isEmpty(), "NORMAL 存档编码成功");
        check(encodeNormal(storage) == bytes, "同一状态连存两次逐字节相同");
        // 新 golden（排序后实现生成；旧 normal-old.bin 是「固定播种 + 旧 QHash
        // 顺序」的产物，不再逐字节可比 —— 见上方兼容读）。
        //   normal-new.bin md5 995642c9f1ca57ab21e0c358e434ea89
        check(bytes == readFile(fixtures + QStringLiteral("/normal-new.bin")),
              "NORMAL 存档 == 新 golden");
        auto global = storage.captureBinarySave(EraBinaryData::FileType::Global);
        global.uniqueCode = 42; global.scriptVersion = 1808;
        QByteArray globalBytes;
        check(EraBinaryData::encode(global, globalBytes), "GLOBAL 编码成功");
        //   global-new.bin md5 ff92a82c95cbaeab9a7da50babaac9b7
        check(globalBytes == readFile(fixtures + QStringLiteral("/global-new.bin")),
              "GLOBAL 存档 == 新 golden");
        QByteArray globalText;
        check(EraBinaryData::encodeText(global, globalText), "GLOBAL 文本编码成功");
        //   global-new.txt md5 dfdf86e45e3d5c44a76250f92c9a2f23（与旧 golden 相同：
        //   GLOBAL 档仅一个用户条目 UG，旧顺序恰好等于排序后的全序）
        check(globalText == readFile(fixtures + QStringLiteral("/global-new.txt")),
              "GLOBAL 文本档 == 新 golden");

        // 结构序：解码自己刚 encode 的字节，条目 序非降。
        EraBinaryData::Document doc;
        check(EraBinaryData::decode(bytes, doc), "新 NORMAL 存档可解码");
        const erasave::Map map = erasave::Map::build(
            storage.variableConfig(), &table, EraBinaryData::FileType::Normal);
        check(orderNonDecreasing(doc.variables, map), "解码字节：variables (slot, 大写名) 序非降");
        check(doc.characters.size() == 2, "解码字节：两个角色");
        if (doc.characters.size() == 2) {
            for (const auto& c : doc.characters) {
                const erasave::Map charaMap = erasave::Map::build(
                    storage.variableConfig(), &table, EraBinaryData::FileType::Character);
                check(orderNonDecreasing(c.builtins, charaMap), "解码字节：角色内建行序非降");
                check(orderNonDecreasing(c.userDefined, charaMap), "解码字节：角色用户行序非降");
            }
        }
        EraBinaryData::Document globalDoc;
        check(EraBinaryData::decode(globalBytes, globalDoc), "新 GLOBAL 存档可解码");
        const erasave::Map globalMap = erasave::Map::build(
            storage.variableConfig(), &table, EraBinaryData::FileType::Global);
        check(orderNonDecreasing(globalDoc.variables, globalMap), "解码字节：GLOBAL 行序非降");

        // 维护用隐藏开关：把新 golden 写回 fixtures 并打印 md5（人工核对后入库）。
        if (qEnvironmentVariableIsSet("EMUERA_SAVE_MAP_REGEN_GOLDEN")) {
            writeFile(fixtures + QStringLiteral("/normal-new.bin"), bytes);
            writeFile(fixtures + QStringLiteral("/global-new.bin"), globalBytes);
            writeFile(fixtures + QStringLiteral("/global-new.txt"), globalText);
            qWarning().noquote() << "GOLDEN normal-new.bin" << md5(bytes).constData()
                                 << globalBytes.size();
            qWarning().noquote() << "GOLDEN global-new.bin" << md5(globalBytes).constData();
            qWarning().noquote() << "GOLDEN global-new.txt" << md5(globalText).constData();
        }
    }

    // ---- 3) 私有变量 save -> load 往返 ----
    {
        VariableStorage storage; storage.initialize(10, 10);
        VariableTable table;
        VariableDecl priv;
        priv.name = "PRIV"; priv.scope = VarScope::Local; priv.function = "FUNC";
        priv.isPrivate = true; priv.isSavedata = true; priv.lengths = {3};
        priv.defaultInt = {1, 2, 3};
        table.add(priv);
        VariableDecl transient;
        transient.name = "STATIC"; transient.scope = VarScope::Local; transient.function = "FUNC";
        transient.isPrivate = true; transient.isSavedata = false; transient.lengths = {2};
        table.add(transient);
        storage.setSaveVariableTable(&table);
        storage.setPrivateScope("FUNC", {QStringLiteral("PRIV"), QStringLiteral("STATIC")});
        storage.setGlobalInt1D("PRIV", 1, 4242);
        storage.setGlobalInt1D("STATIC", 0, 99);
        storage.setLocalContext({});

        const erasave::Map map = erasave::Map::build(
            storage.variableConfig(), &table, EraBinaryData::FileType::Normal);
        const auto* entry = map.findByKey(
            eravar::VariableKey{eravar::internName("FUNC"), eravar::internName("PRIV")});
        check(entry != nullptr && entry->name == "PRIV", "映射表：私有 savedata 行带作用域身份");
        check(map.findByKey(eravar::VariableKey{eravar::internName("STATIC"),
                                                eravar::internName("STATIC")}) == nullptr,
              "映射表：非 savedata 私有变量不入表");

        auto doc = storage.captureBinarySave(EraBinaryData::FileType::Normal);
        QByteArray bytes;
        check(EraBinaryData::encode(doc, bytes), "含私有条目的存档编码成功");
        EraBinaryData::Document decoded;
        check(EraBinaryData::decode(bytes, decoded), "含私有条目的存档解码成功");
        bool wrotePrivate = false;
        for (const auto& v : decoded.variables) wrotePrivate |= v.name == "PRIV";
        check(wrotePrivate, "私有 savedata 变量写进了存档");

        VariableStorage restored; restored.initialize(10, 10);
        VariableTable restoredTable;
        restoredTable.add(priv);
        restored.setSaveVariableTable(&restoredTable);
        restored.applyBinarySave(decoded);
        restored.setPrivateScope("FUNC", {QStringLiteral("PRIV")});
        check(restored.getGlobalInt1D("PRIV", 1) == 4242, "私有变量经 save -> load 值不变");
        check(restored.getGlobalInt1D("PRIV", 0) == 0 && restored.getGlobalInt1D("PRIV", 2) == 0,
              "私有变量的其余元素按存档值恢复");
    }

    // ---- 4) 映射表本身：名字 -> 容器/形状是数据；条目结构序 ----
    {
        VariableTable table;
        VariableDecl d;
        d.name = "S2"; d.isSavedata = true; d.dimension = 2; d.lengths = {2, 3}; table.add(d);
        d = {}; d.name = "SNAME"; d.type = OperandType::Str; d.isSavedata = true;
        d.lengths = {3}; table.add(d);
        d = {}; d.name = "UC"; d.isSavedata = true; d.isCharaData = true; d.lengths = {2}; table.add(d);
        VariableConfig config;
        const erasave::Map normal = erasave::Map::build(config, &table, EraBinaryData::FileType::Normal);
        const auto* s2 = normal.findByName("S2");
        check(s2 && s2->slot == erasave::Slot::GlobalInt2D && s2->dimensions == QList<int>({2, 3}) && !s2->string,
              "映射表：2D 整型声明 -> GlobalInt2D");
        const auto* sname = normal.findByName("SNAME");
        check(sname && sname->slot == erasave::Slot::GlobalStr1D && sname->string,
              "映射表：1D 字符串声明 -> GlobalStr1D");
        const auto* day = normal.findByName("DAY");
        check(day && day->slot == erasave::Slot::SystemInt1D, "映射表：系统 1D 整型 -> SystemInt1D");
        const auto* random = normal.findByName("RANDDATA");
        check(random && random->slot == erasave::Slot::GlobalInt1D,
              "映射表：RANDDATA 无系统槽 -> GlobalInt1D");
        check(normal.findByName("GLOBAL") == nullptr, "映射表：普通存档不含 GLOBAL 条目");
        const erasave::Map globals = erasave::Map::build(config, &table, EraBinaryData::FileType::Global);
        const auto* g = globals.findByName("GLOBAL");
        check(g && g->slot == erasave::Slot::GlobalInt1D, "映射表：GLOBAL 条目在 GLOBAL 档");
        const erasave::Map chars = erasave::Map::build(config, &table, EraBinaryData::FileType::Character);
        const auto* name = chars.findByName("NAME");
        check(name && name->slot == erasave::Slot::CharaStr1D && name->dimensions.isEmpty(),
              "映射表：角色 NAME 是 rank-0 字符串");
        const auto* cflag = chars.findByName("CDFLAG");
        check(cflag && cflag->slot == erasave::Slot::CharaInt2D, "映射表：角色 CFLAG 是 2D 整型");
        const auto* uc = chars.findByName("UC");
        check(uc && uc->slot == erasave::Slot::CharaInt1D && uc->user, "映射表：用户 CHARADATA 进角色段");
        const auto* temp = normal.findByName("TEMP");
        check(temp == nullptr, "映射表：非 SAVEDATA 声明不进表");
        const erasave::Map var = erasave::Map::build(config, &table, EraBinaryData::FileType::Var);
        check(var.findByName("GLOBAL") && var.findByName("S2"), "映射表：SAVEVAR 档 = 普通 + GLOBAL");
        // shapeCount 与旧实现的越界保护一致
        check(erasave::shapeCount({2, 3}) == 6 && erasave::shapeCount({}) == 1
              && erasave::shapeCount({-1}) == 0 && erasave::shapeCount({20000000, 2}) == 0,
              "shapeCount：尺寸与越界");

        // 排序契约（正向）：entries() 的 (slot, 大写名) 序非降。
        const auto orderOk = [](const erasave::Map& m) {
            int lastSlot = -1; QString lastName;
            for (const auto& e : m.entries()) {
                const QString upper = e.name.toUpper();
                if (int(e.slot) < lastSlot) return false;
                if (int(e.slot) == lastSlot && QString::compare(upper, lastName) < 0) return false;
                lastSlot = int(e.slot); lastName = upper;
            }
            return true;
        };
        check(orderOk(normal), "映射表：NORMAL 条目 序非降");
        check(orderOk(globals) && orderOk(chars) && orderOk(var),
              "映射表：GLOBAL/CHARACTER/VAR 条目 序非降");
        // 排序契约（反向）：同一组声明换个插入序 add（m_globals 的 QHash 内部
        // 布局随之不同），排出的全序必须一致 —— 顺序不可能来自任何遍历序。
        {
            VariableTable shuffled;
            VariableDecl d2;
            d2.name = "UC"; d2.isSavedata = true; d2.isCharaData = true; d2.lengths = {2};
            shuffled.add(d2);
            d2 = {}; d2.name = "SNAME"; d2.type = OperandType::Str; d2.isSavedata = true;
            d2.lengths = {3}; shuffled.add(d2);
            d2 = {}; d2.name = "S2"; d2.isSavedata = true; d2.dimension = 2;
            d2.lengths = {2, 3}; shuffled.add(d2);
            const erasave::Map normalShuffled =
                erasave::Map::build(config, &shuffled, EraBinaryData::FileType::Normal);
            bool same = normalShuffled.entries().size() == normal.entries().size();
            for (int i = 0; same && i < normal.entries().size(); ++i)
                same = normal.entries().at(i).name == normalShuffled.entries().at(i).name
                    && normal.entries().at(i).slot == normalShuffled.entries().at(i).slot;
            check(same, "映射表：声明插入序不同，entries() 全序相同");
        }

        // 索引胜负契约：用户声明重打内建名（SAVESTR 声明成 1D 整型）时，
        // findByName 的胜者 = 排序前的插入序胜者（非私有行后来者覆盖 → 用户行）。
        // 两行 slot 不同（用户 GlobalInt1D < 内建 GlobalStr1D），排序不重新裁决。
        {
            VariableTable clash;
            VariableDecl c; c.name = "SAVESTR"; c.isSavedata = true; c.lengths = {3};
            clash.add(c);
            const erasave::Map clashMap = erasave::Map::build(
                config, &clash, EraBinaryData::FileType::Normal);
            bool hasBuiltinRow = false;
            for (const auto& e : clashMap.entries())
                if (e.name == QStringLiteral("SAVESTR") && !e.user) { hasBuiltinRow = true; break; }
            check(hasBuiltinRow, "映射表：内建 SAVESTR 行仍在（与用户行同名并存）");
            const erasave::Entry* hit = clashMap.findByName(QStringLiteral("SAVESTR"));
            check(hit != nullptr, "映射表：重打内建名后 findByName 命中");
            check(hit && hit->user && hit->slot == erasave::Slot::GlobalInt1D,
                  "映射表：findByName 胜者 = 用户行（插入序胜负，排序不重新裁决）");
        }
    }

    qInfo() << "save variable map failures:" << failures;
    return failures ? 1 : 0;
}
