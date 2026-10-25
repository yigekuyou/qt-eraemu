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
#include "csv_text_table.h"
#include "text_encoding.h"

QString CsvTextTable::get(const QString& path, bool* ok)
{
    const auto it = m_entries.constFind(path);
    if (it != m_entries.constEnd()) {
        ++m_hits;
        if (ok) *ok = it.value().ok;
        return it.value().text;   // 隐式共享：不复制字节
    }

    // 编码按文件嗅探（CSV 在日文游戏里常为 Shift-JIS，汉化版为 UTF-8）——
    // 与原 ConstantTable / CsvLoader / VariableConfig 各自的读法一致。
    // 路径来自 FileSystem::collectTree 的 entryInfoList，已是真实大小写，
    // 不再做 resolvePathCase（那是给脚本/配置里的猜大小写路径用的）。
    bool readOk = false;
    Entry entry;
    entry.text = TextCodecUtil::readFile(path, TextEncoding::Auto, nullptr, &readOk);
    entry.ok = readOk;
    if (!readOk) entry.text.clear();   // 失败不留半份文本
    m_chars += entry.text.size();
    m_entries.insert(path, entry);
    if (ok) *ok = entry.ok;
    return entry.text;
}

void CsvTextTable::clear()
{
    m_entries.clear();
    m_chars = 0;
    m_hits = 0;
}