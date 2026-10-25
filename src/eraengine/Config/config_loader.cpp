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
#include "config_loader.h"
#include "GameData/file_system_io.h"   // FileSystem::resolvePathCase（加载前还原真实大小写）

#include <QFile>
#include <QFileInfo>
#include <QDebug>

namespace {

// 拆一行：返回 false 表示不是配置行。
// 分隔符优先 ':'（C# Emuera 的 emuera.config 就是 key:value），其次 '='。
// 注意：值里可能含 ':'（例如 TextEditor 的 `C:\path\notepad.exe`），
// 只按**第一个**分隔符切分（对齐 C# `line.Split(':')` 后再拼回 tokens[1..]）。
bool splitConfigLine(const QString& line, QString& key, QString& value) {
    int sep = -1;
    for (int i = 0; i < line.length(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char(':')) { sep = i; break; }
        // '=' 仅在没有更靠前的 ':' 时作为分隔符
        if (c == QLatin1Char('=')) break;
    }
    if (sep < 0) {
        sep = line.indexOf(QLatin1Char('='));
    }
    if (sep <= 0) return false;

    key = line.left(sep).trimmed();
    value = line.mid(sep + 1).trimmed();
    if (key.isEmpty()) return false;
    // 剥掉值两侧的引号
    if (value.length() >= 2
        && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
        value = value.mid(1, value.length() - 2);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 汉化版配置键名归一（中文 -> 引擎规范日文键）
//
// 汉化 era 游戏会把 CSV/_default.config、CSV/_fixed.config 的**键名一并译成中文**
// （eraH 的 _fixed.config 就是：忽略大小写 / 启用_Rename.csv / 检索子目录 /
//  内部使用东亚语言 …）。而 Emuera 与移植版的内置配置项名是日文，按日文键查找
// 会**整表落空**，于是游戏声明的设置全部失效：
//   · 检索子目录:YES 读不到 -> Config.SearchSubdirectory 恒 false
//     -> 子目录里的角色 CSV 不装载 -> CHARANUM 停在 0 -> 战斗无队伍；
//   · 启用_Rename.csv:YES 读不到 -> _Rename.csv 不装载（此前由自动探测兜住）。
// 这里把已知的中文键名映射回引擎使用的日文键，查找时两者都试。
// （键名对照取自 eraH 的 _default.config / _fixed.config。）
// ---------------------------------------------------------------------------
const QHash<QString, QString>& configKeyAliases() {
    static const QHash<QString, QString> m = {
        // _fixed.config
        {QStringLiteral("忽略大小写"),        QStringLiteral("大文字小文字の違いを無視する")},
        {QStringLiteral("启用_Rename.csv"),   QStringLiteral("_Rename.csvを利用する")},
        {QStringLiteral("启用_Replace.csv"),  QStringLiteral("_Replace.csvを利用する")},
        {QStringLiteral("不对按钮行折返换行"), QStringLiteral("ver1739以前の非ボタン折り返しを再現する")},
        {QStringLiteral("检索子目录"),        QStringLiteral("サブディレクトリを検索する")},
        {QStringLiteral("以UTF-8格式保存存档"), QStringLiteral("セーブデータをUTF-8で保存する")},
        {QStringLiteral("内部使用东亚语言"),  QStringLiteral("内部で使用する東アジア言語")},
        {QStringLiteral("以二进制形式保存存档"), QStringLiteral("セーブデータをバイナリ形式で保存する")},
        // _default.config
        {QStringLiteral("窗口宽度"),          QStringLiteral("ウィンドウ幅")},
        {QStringLiteral("窗口高度"),          QStringLiteral("ウィンドウ高さ")},
        {QStringLiteral("PRINTC并列数量"),    QStringLiteral("PRINTCを並べる数")},
        {QStringLiteral("PRINTC文字数量"),    QStringLiteral("PRINTCの文字数")},
        {QStringLiteral("字体名称"),          QStringLiteral("フォント名")},
        {QStringLiteral("字体大小"),          QStringLiteral("フォントサイズ")},
        {QStringLiteral("每行高度"),          QStringLiteral("一行の高さ")},
        {QStringLiteral("文字色"),            QStringLiteral("文字色")},
        {QStringLiteral("背景色"),            QStringLiteral("背景色")},
        {QStringLiteral("選択中文字色"),      QStringLiteral("選択中文字色")},
        {QStringLiteral("履歴文字色"),        QStringLiteral("履歴文字色")},
        {QStringLiteral("绘图界面"),          QStringLiteral("描画インターフェース")},
        {QStringLiteral("在sav文件夹中创建存档"), QStringLiteral("セーブデータをsavフォルダ内に作成する")},
        {QStringLiteral("显示eramaker兼容性相关警告"), QStringLiteral("eramaker互換性に関する警告を表示する")},
        {QStringLiteral("重复定义非事件函数时显示警告"), QStringLiteral("同名の非イベント関数が複数定義されたとき警告する")},
        {QStringLiteral("最低显示警告等级"),  QStringLiteral("表示する最低警告レベル")},
    };
    return m;
}

// 反查方向（规范键 -> 中文译名）建表时算一次：以前每查一个键都把 25 条
// 中文别名线性扫一遍。
const QHash<QString, QStringList>& configAliasIndex() {
    static const QHash<QString, QStringList> m = [] {
        QHash<QString, QStringList> index;
        const auto& aliases = configKeyAliases();
        for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it)
            if (it.key() != it.value()) index[it.value()].append(it.key());
        return index;
    }();
    return m;
}

// 查找顺序：先原键，再其中文别名（汉化文件里只有中文键）
QStringList configLookupKeys(const QString& key) {
    const QHash<QString, QStringList>& index = configAliasIndex();
    const auto it = index.constFind(key);
    QStringList out{key};
    if (it != index.constEnd()) out += it.value();
    return out;
}

// 文件里的原始键 -> 引擎规范键（无别名时原样返回）
QString canonicalConfigKey(const QString& rawKey) {
    return configKeyAliases().value(rawKey, rawKey);
}

} // namespace

ConfigLoader::ConfigLoader(QObject *parent)
    : QObject(parent)
{
    rebuildValues();
}

bool ConfigLoader::parseConfigFile(const QString& filePath, QHash<QString, QString>& config,
                                   ConfigFile* meta)
{
    // 文本文件编码：嗅探（BOM → UTF-8 → Shift-JIS → Latin-1），内部统一 Unicode
    bool ok = false;
    TextEncoding detected = TextEncoding::Auto;
    // 大小写：加载前把路径逐段还原成磁盘上的真实名字（Windows→Linux 移植问题）。
    const QString text = TextCodecUtil::readFile(FileSystem::resolvePathCase(filePath),
                                                 m_readEncoding, &detected, &ok);
    if (!ok) {
        qDebug() << "[ConfigLoader] 无法读取配置文件:" << filePath;
        return false;
    }
    if (meta) {
        meta->detectedEncoding = detected;
        meta->hadBom = (detected == TextEncoding::Utf8Bom);
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    int count = 0;
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))) continue;
        QString key, value;
        if (!splitConfigLine(trimmed, key, value)) continue;
        config.insert(key, value);
        ++count;
    }
    qDebug() << "[ConfigLoader]" << QFileInfo(filePath).fileName()
             << "编码:" << TextCodecUtil::name(detected) << "配置项:" << count;
    return true;
}

bool ConfigLoader::loadConfigFile(const QString& filePath, int precedence)
{
    ConfigFile config;
    config.filePath = filePath;
    config.precedence = precedence;
    config.isFixed = filePath.contains(QLatin1String("_fixed.config"), Qt::CaseInsensitive);

    if (!parseConfigFile(filePath, config.config, &config)) {
        return false;
    }
    m_configFiles.append(config);
    rebuildValues();
    if (precedence >= 0) {
        for (auto it = config.config.constBegin(); it != config.config.constEnd(); ++it) {
            emit configChanged(it.key(), it.value());
        }
    }
    return true;
}

void ConfigLoader::mergeConfig(const QString& filePath, int precedence)
{
    ConfigFile config;
    config.filePath = filePath;
    config.precedence = precedence;
    config.isFixed = filePath.contains(QLatin1String("_fixed.config"), Qt::CaseInsensitive);

    if (!parseConfigFile(filePath, config.config, &config)) {
        return;
    }
    m_configFiles.append(config);
    rebuildValues();
}

QString ConfigLoader::getConfig(const QString& key) const
{
    for (const QString& k : configLookupKeys(key)) {
        // Search from highest precedence to lowest
        // Fixed config has highest priority, then regular configs by precedence
        // Within same precedence, later loaded files have higher priority

        // First, check fixed configs (highest priority)
        for (int i = m_configFiles.size() - 1; i >= 0; i--) {
            const ConfigFile& config = m_configFiles[i];
            if (config.isFixed && config.config.contains(k)) {
                return config.config[k];
            }
        }
        // Then non-fixed configs in reverse order
        for (int i = m_configFiles.size() - 1; i >= 0; i--) {
            const ConfigFile& config = m_configFiles[i];
            if (!config.isFixed && config.config.contains(k)) {
                return config.config[k];
            }
        }
    }
    return QString();
}

void ConfigLoader::setConfig(const QString& key, const QString& value)
{
    // 写到最高优先级的文件（没有文件时建一个「内存文件」）
    if (m_configFiles.isEmpty()) {
        ConfigFile mem;
        mem.filePath.clear();
        mem.precedence = 0;
        mem.isFixed = false;
        m_configFiles.append(mem);
    }
    m_configFiles.last().config.insert(key, value);
    rebuildValues();
    emit configChanged(key, value);
}

bool ConfigLoader::hasConfig(const QString& key) const
{
    for (const QString& k : configLookupKeys(key)) {
        for (const ConfigFile& config : m_configFiles) {
            if (config.config.contains(k)) return true;
        }
    }
    return false;
}

bool ConfigLoader::parseBool(const QString& value, bool defaultValue)
{
    const QString v = value.trimmed().toUpper();
    if (v == QLatin1String("YES") || v == QLatin1String("TRUE") || v == QLatin1String("ON")
        || v == QLatin1String("1")) {
        return true;
    }
    if (v == QLatin1String("NO") || v == QLatin1String("FALSE") || v == QLatin1String("OFF")
        || v == QLatin1String("0")) {
        return false;
    }
    return defaultValue;
}

bool ConfigLoader::getBool(const QString& key, bool defaultValue) const
{
    // 同 getInt：没有该键时 .value 就返回 defaultValue，守卫是多余的第二跳
    return m_boolValues.value(key, defaultValue);
}

int ConfigLoader::getInt(const QString& key, int defaultValue) const
{
    // 摊平表 m_intValues 只由「文件里真实存在的键」建出来，没有该键时
    // .value(key, ...) 本来就返回 defaultValue —— 这里不再先跳一次 hasConfig。
    return m_intValues.value(key, defaultValue);
}

// ---------------------------------------------------------------------------
// **全工程唯一的一张配置键表**：键 → {默认值, 项类型, 英文（历史）别名}
//
// 这张表同时是四件事的同一个来源，加/改一个配置项只改这里一处：
//   1. GETCONFIG / GETCONFIGS 的白名单与内建默认值（对齐 C# ConfigData.
//      GetConfigValueInERB：白名单外 C# 抛错，这里按「未命中」处理）；
//   2. 类型：决定 ERB 的返回形态（<bool> -> "1"/"0"、<Color> -> RGB 打包）
//      以及 ConfigLoader::configInt/configBool 的解析方式；
//   3. 界面项（GuiManager）：字体/颜色/帧率/窗口宽高…… 的默认值与英文别名
//      —— GuiManager 只按名问这张表，自己不再存键名或别名（此前两份表已经
//      漂移：WindowMaximixed 的正确拼写被第二次查找用错的键名吞掉、
//      ウィンドウ高さ/フレーム毎秒 等 6 项不在 ERB 白名单里 -> GETCONFIG 得 0）；
//   4. **舞台尺寸的唯一事实**：ウィンドウ幅/高さ 的默认值（760x480）。
//      WindowController::stageWidth/stageHeight、targetWindowSize、GuiManager
//      的窗口初值全部从这里取；QML 只负责把舞台等比缩放呈现，不自带默认宽高。
//
// 别名按「历史顺序」排列，命中第一个存在的名字；值为命中的那一个，**只解析一次**
// （以前 GuiManager 先 lookup 别名、再用别名列表里的第一个名字重新 hasConfig/getBool，
// 于是写正确拼法 WindowMaximized 时值被静默丢弃）。
//
// 中文译名（汉化版 CSV/_default.config 的键名）不在这张表里：那是「文件里的键名
// 归一」（configKeyAliases），覆盖的键比这张表多、且没有默认值/类型的语义。
// getConfig/hasConfig 已经会同时试中文译名，所以按规范键读表项即可。
// ---------------------------------------------------------------------------
namespace {
enum class ConfigType { Int, Int64, Bool, Color, Text };

struct ConfigItem {
    QString     key;           // 规范键（C# ConfigData 的日文键；saveToConfig 写回用）
    QString     defaultValue;  // 内建默认值（文本形态，与 C# 一致）
    ConfigType  type;
    QStringList aliases;       // 英文/历史别名（规范键之外能命中的名字）
};

const QList<ConfigItem>& configItemTable() {
    static const QList<ConfigItem> kItems = {
        // ---- 窗口 / 舞台：**舞台尺寸的唯一事实就是这两行的默认值** ----
        // （窗口下限常量在 WindowController，且只作用于窗口，不作用于舞台）
        {QStringLiteral("ウィンドウ幅"),                QStringLiteral("760"),            ConfigType::Int,   {QStringLiteral("WindowX")}},
        {QStringLiteral("ウィンドウ高さ"),              QStringLiteral("480"),            ConfigType::Int,   {QStringLiteral("WindowY")}},
        // ---- <int> ----
        {QStringLiteral("PRINTCを並べる数"),             QStringLiteral("3"),              ConfigType::Int,   {}},
        {QStringLiteral("PRINTCの文字数"),               QStringLiteral("25"),             ConfigType::Int,   {}},
        {QStringLiteral("フォントサイズ"),               QStringLiteral("18"),             ConfigType::Int,   {QStringLiteral("FontSize")}},
        {QStringLiteral("一行の高さ"),                   QStringLiteral("19"),             ConfigType::Int,   {QStringLiteral("LineHeight")}},
        {QStringLiteral("表示するセーブデータ数"),       QStringLiteral("20"),             ConfigType::Int,   {}},
        {QStringLiteral("販売アイテム数"),               QStringLiteral("100"),            ConfigType::Int,   {}},
        {QStringLiteral("COM_ABLE初期値"),               QStringLiteral("1"),              ConfigType::Int,   {}},
        {QStringLiteral("フレーム毎秒"),                 QStringLiteral("5"),              ConfigType::Int,   {QStringLiteral("FPS")}},
        {QStringLiteral("履歴ログの行数"),               QStringLiteral("5000"),           ConfigType::Int,   {QStringLiteral("MaxLog")}},
        {QStringLiteral("スクロール行数"),               QStringLiteral("1"),              ConfigType::Int,   {QStringLiteral("ScrollHeight")}},
        // ---- <Int64> ----
        {QStringLiteral("PBANDの初期値"),                QStringLiteral("4"),              ConfigType::Int64, {}},
        {QStringLiteral("RELATIONの初期値"),             QStringLiteral("0"),              ConfigType::Int64, {}},
        // ---- <bool>（C# 以 1/0 返回）----
        {QStringLiteral("オートセーブを行なう"),         QStringLiteral("1"),              ConfigType::Bool,  {}},
        {QStringLiteral("単位の位置"),                   QStringLiteral("1"),              ConfigType::Bool,  {}},
        {QStringLiteral("ウィンドウの高さを可変にする"), QStringLiteral("1"),              ConfigType::Bool,  {QStringLiteral("SizableWindow")}},
        // WindowMaximixed 是历史拼写（老配置文件里可能有），正确拼写也必须命中
        {QStringLiteral("起動時にウィンドウを最大化する"), QStringLiteral("0"),            ConfigType::Bool,  {QStringLiteral("WindowMaximixed"), QStringLiteral("WindowMaximized")}},
        // ---- <Color>（C# 打包为 ((R*256)+G)*256+B）----
        {QStringLiteral("文字色"),                       QStringLiteral("192,192,192"),    ConfigType::Color, {QStringLiteral("ForeColor")}},
        {QStringLiteral("背景色"),                       QStringLiteral("0,0,0"),          ConfigType::Color, {QStringLiteral("BackColor")}},
        {QStringLiteral("選択中文字色"),                 QStringLiteral("255,255,0"),      ConfigType::Color, {QStringLiteral("FocusColor")}},
        {QStringLiteral("履歴文字色"),                    QStringLiteral("192,192,192"),   ConfigType::Color, {QStringLiteral("LogColor")}},
        // ---- <string> ----
        {QStringLiteral("フォント名"),                   QStringLiteral("ＭＳ ゴシック"),  ConfigType::Text,  {QStringLiteral("FontName")}},
        {QStringLiteral("お金の単位"),                   QStringLiteral("$"),              ConfigType::Text,  {}},
        {QStringLiteral("起動時簡略表示"),               QStringLiteral("Now Loading..."), ConfigType::Text,  {}},
        {QStringLiteral("DRAWLINE文字"),                 QStringLiteral("-"),              ConfigType::Text,  {}},
        {QStringLiteral("システムメニュー0"),            QStringLiteral("最初からはじめる"), ConfigType::Text, {}},
        {QStringLiteral("システムメニュー1"),            QStringLiteral("ロードしてはじめる"), ConfigType::Text, {}},
        {QStringLiteral("時間切れ表示"),                 QStringLiteral("時間切れ"),        ConfigType::Text,  {}},
        // ---- <char> ----
        {QStringLiteral("BAR文字1"),                     QStringLiteral("*"),              ConfigType::Text,  {}},
        {QStringLiteral("BAR文字2"),                     QStringLiteral("."),              ConfigType::Text,  {}},
        // ---- <TextDrawingMode> ----
        {QStringLiteral("描画インターフェース"),         QStringLiteral("TEXTRENDERER"),   ConfigType::Text,  {}},
    };
    return kItems;
}

// 规范键或英文别名 -> 表项（名字/别名索引只建一次）
const ConfigItem* findConfigItem(const QString& key) {
    static const QHash<QString, const ConfigItem*> index = [] {
        QHash<QString, const ConfigItem*> m;
        for (const ConfigItem& item : configItemTable()) {
            m.insert(item.key, &item);
            for (const QString& alias : item.aliases) m.insert(alias, &item);
        }
        return m;
    }();
    const auto it = index.constFind(key);
    return it == index.constEnd() ? nullptr : it.value();
}

// ERB 白名单只认规范键（英文别名不是 ERB 键，C# 也只认日文键）
const ConfigItem* findErbItem(const QString& key) {
    const ConfigItem* item = findConfigItem(key);
    return (item && item->key == key) ? item : nullptr;
}
} // namespace

// ---------------------------------------------------------------------------
// 按键表读取（唯一一张表见上）：GuiManager 等使用者只按名问，不自己存别名/默认值
// ---------------------------------------------------------------------------

QString ConfigLoader::configText(const QString& key) const
{
    if (const ConfigItem* item = findConfigItem(key)) {
        // 规范键优先（getConfig 内部已含汉化版的中文译名），再试英文/历史别名；
        // 都没配过就是表内默认值 —— 默认值只写在表里一份。
        if (hasConfig(item->key)) return getConfig(item->key);
        for (const QString& alias : item->aliases)
            if (hasConfig(alias)) return getConfig(alias);
        return item->defaultValue;
    }
    return getConfig(key);   // 未登记的键：按老语义直查
}

int ConfigLoader::configInt(const QString& key) const
{
    bool ok = false;
    const int value = configText(key).trimmed().toInt(&ok);
    return ok ? value : configIntDefault(key);
}

bool ConfigLoader::configBool(const QString& key) const
{
    const ConfigItem* item = findConfigItem(key);
    if (!item) return getBool(key, false);
    // 整型/文本项被当 bool 问时按宽松语义解析（YES/NO/TRUE/FALSE/ON/OFF/1/0）
    return parseBool(configText(key), parseBool(item->defaultValue, false));
}

int ConfigLoader::configIntDefault(const QString& key)
{
    const ConfigItem* item = findConfigItem(key);
    return item ? item->defaultValue.toInt() : 0;
}

bool ConfigLoader::isErbConfigKey(const QString& key)
{
    return findErbItem(key) != nullptr;
}

bool ConfigLoader::convertErbValue(const QString& key, QString& out) const
{
    const ConfigItem* item = findErbItem(key);
    if (!item) return false;
    const QString def = item->defaultValue;
    const QString raw = hasConfig(key) ? getConfig(key) : def;
    switch (item->type) {
    case ConfigType::Bool:
        out = QString::number(parseBool(raw, def == QLatin1String("1")) ? 1 : 0);
        return true;
    case ConfigType::Color: {
        const QStringList rgb = raw.split(QLatin1Char(','));
        const int r = rgb.size() > 0 ? rgb.at(0).trimmed().toInt() : 0;
        const int g = rgb.size() > 1 ? rgb.at(1).trimmed().toInt() : 0;
        const int b = rgb.size() > 2 ? rgb.at(2).trimmed().toInt() : 0;
        out = QString::number(((r * 256) + g) * 256 + b);
        return true;
    }
    case ConfigType::Int:
    case ConfigType::Int64:
    case ConfigType::Text:
    default:
        out = raw;
        return true;
    }
}

void ConfigLoader::rebuildValues()
{
    m_erbValues.clear();
    m_erbIntegerValues.clear();
    m_intValues.clear();
    m_boolValues.clear();
    m_colors.clear();
    for (const ConfigItem& item : configItemTable()) {
        QString value;
        convertErbValue(item.key, value);
        m_erbValues.insert(item.key, value);
        if (item.type != ConfigType::Text)
            m_erbIntegerValues.insert(item.key, value.toLongLong());
    }
    for (const auto& file : m_configFiles) {
        for (auto it = file.config.constBegin(); it != file.config.constEnd(); ++it) {
            const QString raw = getConfig(it.key()).trimmed();
            bool ok = false;
            const int value = raw.toInt(&ok);
            // 汉化配置文件的键名是中文，引擎按日文键查找；这里同时按
            // 「原始键」与「规范键」两个名字登记转换结果，getBool/getInt/
            // getColor 用任一名字都能命中。
            const QString canon = canonicalConfigKey(it.key());
            const QStringList names = canon == it.key()
                ? QStringList{it.key()} : QStringList{it.key(), canon};
            const auto put = [&names](auto& map, const auto& v) {
                for (const QString& n : names) map.insert(n, v);
            };
            if (ok) put(m_intValues, value);
            const auto rgb = raw.split(QLatin1Char(','));
            if (rgb.size() >= 3) put(m_colors,
                qint64((rgb.at(0).toInt() << 16) | (rgb.at(1).toInt() << 8) | rgb.at(2).toInt()) & 0xFFFFFF);
            const QString upper = raw.toUpper();
            if (upper == "YES" || upper == "TRUE" || upper == "ON" || upper == "1"
                || upper == "NO" || upper == "FALSE" || upper == "OFF" || upper == "0")
                put(m_boolValues, parseBool(raw, false));
        }
    }
}

bool ConfigLoader::integerConfigValueInErb(const QString& key, qint64& out) const
{
    const auto found = m_erbIntegerValues.constFind(key);
    if (found == m_erbIntegerValues.constEnd()) return false;
    out = found.value();
    return true;
}

bool ConfigLoader::configValueInErb(const QString& key, QString& out) const
{
    const auto it = m_erbValues.constFind(key);
    if (it == m_erbValues.constEnd()) return false;
    out = it.value();
    return true;
}

QString ConfigLoader::serialize(const QHash<QString, QString>& config)
{
    QStringList keys = config.keys();
    keys.sort(Qt::CaseInsensitive);
    QString out;
    for (const QString& k : keys) {
        out += k;
        out += QLatin1Char(':');
        out += config.value(k);
        out += QLatin1Char('\n');
    }
    return out;
}

bool ConfigLoader::saveConfigFile(const QString& filePath, TextEncoding enc)
{
    const TextEncoding writeEnc = (enc == TextEncoding::Auto) ? m_writeEncoding : enc;
    QString target = filePath;
    if (target.isEmpty()) {
        // 未指定路径：找已加载文件中同名的
        return false;
    }

    QHash<QString, QString> merged;
    for (const ConfigFile& f : m_configFiles) {
        if (f.filePath == target) merged = f.config;
    }
    if (merged.isEmpty()) {
        // 找不到就写合并视图（所有文件的高优先级视图）
        for (const ConfigFile& f : m_configFiles) {
            for (auto it = f.config.constBegin(); it != f.config.constEnd(); ++it) {
                merged.insert(it.key(), it.value());
            }
        }
    }
    if (!TextCodecUtil::writeFile(target, serialize(merged), writeEnc)) {
        qWarning() << "[ConfigLoader] 写回失败:" << target;
        return false;
    }
    qDebug() << "[ConfigLoader] 写回" << QFileInfo(target).fileName()
             << "编码:" << TextCodecUtil::name(writeEnc);
    return true;
}

bool ConfigLoader::setConfigValueInFile(const QString& filePath, const QString& key,
                                        const QString& value, TextEncoding enc)
{
    if (filePath.isEmpty() || key.isEmpty()) return false;
    const TextEncoding writeEnc = (enc == TextEncoding::Auto) ? m_writeEncoding : enc;

    // 原样读入所有行（保留注释/空行/顺序）
    bool ok = false;
    TextEncoding detected = TextEncoding::Auto;
    const QString existing = TextCodecUtil::readFile(filePath, m_readEncoding, &detected, &ok);

    QStringList lines;
    if (ok && !existing.isEmpty()) {
        lines = existing.split(QLatin1Char('\n'));
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    }

    const QString newLine = key + QLatin1Char(':') + value;
    bool replaced = false;
    for (QString& line : lines) {
        QString l = line;
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
        const QString trimmed = l.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))) continue;
        QString k, v;
        if (!splitConfigLine(trimmed, k, v)) continue;
        if (k.compare(key, Qt::CaseInsensitive) == 0) {
            line = newLine;
            replaced = true;
            break;
        }
    }
    if (!replaced) lines << newLine;

    QString out = lines.join(QLatin1Char('\n'));
    if (!out.isEmpty()) out += QLatin1Char('\n');
    if (!TextCodecUtil::writeFile(filePath, out, writeEnc)) {
        qWarning() << "[ConfigLoader] 无法写入配置项:" << filePath << key;
        return false;
    }
    // 同步内存视图（若该文件已加载）
    for (ConfigFile& f : m_configFiles) {
        if (f.filePath == filePath) f.config.insert(key, value);
    }
    rebuildValues();
    qDebug() << "[ConfigLoader]" << (replaced ? "更新" : "追加") << "配置项"
             << key << "->" << value << "于" << QFileInfo(filePath).fileName()
             << "编码:" << TextCodecUtil::name(writeEnc);
    return true;
}

int ConfigLoader::saveAll()
{
    int n = 0;
    for (const ConfigFile& f : m_configFiles) {
        if (f.filePath.isEmpty()) continue;
        if (saveConfigFile(f.filePath, m_writeEncoding)) ++n;
    }
    return n;
}

QList<ConfigFile> ConfigLoader::getConfigFiles() const
{
    return m_configFiles;
}

TextEncoding ConfigLoader::encodingOf(const QString& filePath) const
{
    for (const ConfigFile& f : m_configFiles) {
        if (f.filePath == filePath) return f.detectedEncoding;
    }
    return TextEncoding::Auto;
}
