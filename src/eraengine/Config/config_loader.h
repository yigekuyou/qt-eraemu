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
#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QList>

#include "text_encoding.h"

// Config file with precedence level
struct ConfigFile {
    QString filePath;
    int precedence;  // Lower number = lower priority
    bool isFixed;    // True if this is a fixed config (e.g., _fixed.config)
    QHash<QString, QString> config;

    // ---- 编码 ----
    // 读入时嗅探到的编码（写回时若未指定则沿用「UTF-8」策略，见 ConfigLoader::saveConfigFile）
    TextEncoding detectedEncoding = TextEncoding::Auto;
    // 文件是否带 BOM（读入时）
    bool hadBom = false;
};

// ---------------------------------------------------------------------------
// ConfigLoader —— emuera.config / _default.config / _fixed.config
//
// 对齐 C# ConfigData.loadConfig：
//   · 行分隔符是 **':'**（`key:value`），不是 '='；为兼容自写文件同时接受 '='；
//   · ';' 开头为注释；空行跳过；
//   · 键与值 trim，值两侧的引号剥掉。
//
// 编码：C# 用 `Config.Encode`（默认 SHIFT-JIS，useLanguage 可换 949/936/950）。
// 本实现按文件嗅探（BOM → UTF-8 → Shift-JIS → Latin-1），因此同一游戏目录里
// 「UTF-8 的 ERB + Shift-JIS 的 emuera.config」都能正确读入；内部一律 Unicode，
// 需要字节时统一 toUtf8()。
// ---------------------------------------------------------------------------
class ConfigLoader : public QObject
{
    Q_OBJECT

public:
    explicit ConfigLoader(QObject *parent = nullptr);

    // Load configuration files
    bool loadConfigFile(const QString& filePath, int precedence = 0);
    void mergeConfig(const QString& filePath, int precedence = 0);

    // Config access
    QString getConfig(const QString& key) const;
    void setConfig(const QString& key, const QString& value);
    bool hasConfig(const QString& key) const;

    // 布尔/整数便捷读取（对齐 C# AConfigItem.TryParse 的宽松语义）
    bool getBool(const QString& key, bool defaultValue = false) const;
    int getInt(const QString& key, int defaultValue = 0) const;

    // ---- 编码 ----
    // 强制读取编码（Auto = 逐文件嗅探，默认）
    void setReadEncoding(TextEncoding enc) { m_readEncoding = enc; }
    [[nodiscard]] TextEncoding readEncoding() const { return m_readEncoding; }
    // 写回编码（默认 UTF-8+BOM：Linux/macOS/Windows 都能正确识别；
    // 要与 Emuera 原版互换时设为 ShiftJis）
    void setWriteEncoding(TextEncoding enc) { m_writeEncoding = enc; }
    [[nodiscard]] TextEncoding writeEncoding() const { return m_writeEncoding; }

    // 把某个已加载的配置文件写回（默认 writeEncoding）。
    bool saveConfigFile(const QString& filePath, TextEncoding enc = TextEncoding::Auto);

    // 只改/追加**一个**键，其余行（含注释与顺序）原样保留；文件不存在则创建。
    // 用于把「本引擎自己的设置」（如 TextEncoding）落到 CSV/_fixed.config
    // ——_fixed.config 是最高优先级，且 Emuera 会忽略不认识的键。
    bool setConfigValueInFile(const QString& filePath, const QString& key, const QString& value,
                             TextEncoding enc = TextEncoding::Auto);
    // 便捷：写回全部已加载文件（保持各自路径）
    int saveAll();

    // 清空已加载文件（供「用新的回退编码重读配置」使用）
    void clearFiles() { m_configFiles.clear(); }

    // ---- ERB 侧取值：GETCONFIG / GETCONFIGS ----
    // 对齐 C# ConfigData.GetConfigValueInERB：**只有白名单里的配置项**允许被
    // ERB 读出（白名单外 C# 抛错，这里按「未命中」处理）。返回值的形态按项类型：
    //   <bool>            -> "1"/"0"（按 YES/NO/TRUE/FALSE/ON/OFF/1/0 宽松解析）
    //   <int>/<Int64>     -> 数值文本
    //   <Color>           -> ((R*256)+G)*256+B（C# 的 Color -> Int 打包）
    //   <string>/<char>/<TextDrawingMode> -> 原样文本（GETCONFIGS 用）
    // 配置项缺省时回落到 C# 的内建默认值。
    // 命中返回 true；白名单外返回 false（调用方：GETCONFIG -> 0 / GETCONFIGS -> ""）。
    [[nodiscard]] bool configValueInErb(const QString& key, QString& out) const;

    // 该键是否属于 GETCONFIG/GETCONFIGS 白名单（用于 GETCONFIGS 的类型校验提示）
    [[nodiscard]] static bool isErbConfigKey(const QString& key);

    // 已加载文件（含嗅探到的编码）
    QList<ConfigFile> getConfigFiles() const;
    // 某个文件嗅探到的编码（未加载返回 Auto）
    [[nodiscard]] TextEncoding encodingOf(const QString& filePath) const;

signals:
    void configChanged(const QString& key, const QString& value);

private:
    QList<ConfigFile> m_configFiles;
    TextEncoding m_readEncoding = TextEncoding::Auto;
    TextEncoding m_writeEncoding = TextEncoding::Utf8Bom;

    // Helper methods
    bool parseConfigFile(const QString& filePath, QHash<QString, QString>& config, ConfigFile* meta);
    static QString serialize(const QHash<QString, QString>& config);
    static bool parseBool(const QString& value, bool defaultValue);
};

#endif // CONFIG_LOADER_H
