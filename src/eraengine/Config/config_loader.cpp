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

} // namespace

ConfigLoader::ConfigLoader(QObject *parent)
    : QObject(parent)
{
}

bool ConfigLoader::parseConfigFile(const QString& filePath, QHash<QString, QString>& config,
                                   ConfigFile* meta)
{
    // 文本文件编码：嗅探（BOM → UTF-8 → Shift-JIS → Latin-1），内部统一 Unicode
    bool ok = false;
    TextEncoding detected = TextEncoding::Auto;
    const QString text = TextCodecUtil::readFile(filePath, m_readEncoding, &detected, &ok);
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
}

QString ConfigLoader::getConfig(const QString& key) const
{
    // Search from highest precedence to lowest
    // Fixed config has highest priority, then regular configs by precedence
    // Within same precedence, later loaded files have higher priority

    // First, check fixed configs (highest priority)
    for (int i = m_configFiles.size() - 1; i >= 0; i--) {
        const ConfigFile& config = m_configFiles[i];
        if (config.isFixed && config.config.contains(key)) {
            return config.config[key];
        }
    }
    // Then non-fixed configs in reverse order
    for (int i = m_configFiles.size() - 1; i >= 0; i--) {
        const ConfigFile& config = m_configFiles[i];
        if (!config.isFixed && config.config.contains(key)) {
            return config.config[key];
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
    emit configChanged(key, value);
}

bool ConfigLoader::hasConfig(const QString& key) const
{
    for (const ConfigFile& config : m_configFiles) {
        if (config.config.contains(key)) return true;
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
    if (!hasConfig(key)) return defaultValue;
    return parseBool(getConfig(key), defaultValue);
}

int ConfigLoader::getInt(const QString& key, int defaultValue) const
{
    if (!hasConfig(key)) return defaultValue;
    bool ok = false;
    const int v = getConfig(key).trimmed().toInt(&ok);
    return ok ? v : defaultValue;
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
