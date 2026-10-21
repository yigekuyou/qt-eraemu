// 存档映射层（erasave::Map）的验收测试：
//   1) 存出来的字节与重构前逐字节相同（fixtures/save-map/*.bin 由旧实现生成）；
//   2) 旧格式存档（不含任何映射痕迹）仍能读；
//   3) 私有变量（函数内 #DIM SAVEDATA）经 save -> load 往返值不变；
//   4) 映射表本身的内容（名字 / 身份 / 容器 / 形状）。
#include <QCoreApplication>
#include <QByteArray>
#include <QHash>
#include <QDebug>
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

// 重构前的旧实现生成的基线与本测例共用同一份声明表与初值：
// 只含广域/角色数据声明（没有函数私有 savedata），因此旧的 saveShapes 与新的
// 映射表产出的条目序列完全一致。
void buildGoldenState(VariableStorage& storage, VariableTable& table)
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

} // namespace

int main(int argc, char** argv)
{
    // 用户 SAVEDATA 声明的枚举顺序来自 VariableTable 内部的 QHash（重构前后一样），
    // 默认每个进程随机播种 -> 多用户变量的存档条目顺序逐进程不同（重构前就如此，见报告第 6 节）。
    // 这里固定种子，让 golden 字节比较有意义。
    QHashSeed::setDeterministicGlobalSeed();
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { qCritical() << "FAIL:" << label; ++failures; }
    };
    const QString fixtures = QStringLiteral(ERA_TEST_SOURCE_DIR) + QStringLiteral("/fixtures/save-map");

    // ---- 1) 字节不变 + 2) 旧格式可读（同一份旧字节）----
    {
        VariableStorage storage; storage.initialize(10, 10);
        VariableTable table;
        buildGoldenState(storage, table);
        const QByteArray bytes = encodeNormal(storage);
        const QByteArray golden = readFile(fixtures + QStringLiteral("/normal-old.bin"));
        check(!golden.isEmpty() && bytes == golden, "NORMAL 存档与重构前逐字节相同");
        check(encodeNormal(storage) == bytes, "同一状态连存两次逐字节相同");
        auto global = storage.captureBinarySave(EraBinaryData::FileType::Global);
        global.uniqueCode = 42; global.scriptVersion = 1808;
        QByteArray globalBytes;
        check(EraBinaryData::encode(global, globalBytes), "GLOBAL 编码成功");
        check(globalBytes == readFile(fixtures + QStringLiteral("/global-old.bin")),
              "GLOBAL 存档与重构前逐字节相同");

        // 旧字节读回：旧实现根本没写过私有变量，文件里只有广域/角色数据条目。
        VariableStorage restored; restored.initialize(10, 10);
        VariableTable restoredTable;
        VariableDecl saved; saved.name = "UG"; saved.isSavedata = true; saved.isGlobal = true;
        saved.isGlobalSave = true; saved.lengths = {2}; restoredTable.add(saved);
        restored.setSaveVariableTable(&restoredTable);
        EraBinaryData::Document doc;
        check(EraBinaryData::decode(globalBytes, doc), "旧 GLOBAL 字节可解码");
        restored.setGlobalInt1D("UG", 0, 0); restored.setGlobalStr1D("GLOBALS", 0, "bye");
        restored.applyBinarySave(doc);
        check(restored.getGlobalInt1D("GLOBAL", 0) == 999, "旧 GLOBAL 存档：GLOBAL 恢复");
        check(restored.getGlobalStr1D("GLOBALS", 0) == "gl", "旧 GLOBAL 存档：GLOBALS 恢复");
        check(restored.getGlobalInt1D("UG", 1) == 5, "旧 GLOBAL 存档：用户 SAVEDATA 恢复");
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

    // ---- 4) 映射表本身：名字 -> 容器/形状是数据 ----
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
    }

    if (argc > 1) QDir().mkpath(QString::fromLocal8Bit(argv[1]));
    qInfo() << "save variable map failures:" << failures;
    return failures ? 1 : 0;
}