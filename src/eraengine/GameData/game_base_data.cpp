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
#include "game_base_data.h"
#include <QDebug>

GameBaseData::GameBaseData(QObject *parent)
    : QObject(parent)
{
}

QString GameBaseData::windowTitle() const { return m_windowTitle; }
QString GameBaseData::title() const { return m_title; }
QString GameBaseData::author() const { return m_author; }
QString GameBaseData::version() const { return m_version; }
QString GameBaseData::releaseYear() const { return m_releaseYear; }
QString GameBaseData::additionalInfo() const { return m_additionalInfo; }

void GameBaseData::set(const QString& key, const QString& value)
{
		bool updated = false;
		if (key == "ウィンドウタイトル") { m_windowTitle = value; updated = true; }
		else if (key == "タイトル") { m_title = value; updated = true; }
		else if (key == "作者") { m_author = value; updated = true; }
		else if (key == "バージョン") { m_version = value; updated = true; }
		else if (key == "製作年") { m_releaseYear = value; updated = true; }
		else if (key == "追加情報") { m_additionalInfo = value; updated = true; }

		if (updated) {
				qDebug() << "[load] GameBase" << key << "=" << value;
				emit dataChanged(); // 关键：通知 QML 属性已变更
		}
}

QString GameBaseData::get(const QString& key) const
{
    // Map English keys to Japanese keys
    QString mappedKey = key;
    if (key == "TITLE") mappedKey = "タイトル";
    else if (key == "VERSION") mappedKey = "バージョン";
    else if (key == "AUTHOR") mappedKey = "作者";
    else if (key == "YEAR") mappedKey = "製作年";
    else if (key == "INFO") mappedKey = "追加情報";
    else if (key == "WINDOW_TITLE") mappedKey = "ウィンドウタイトル";
    else if (key == "GAMEBASE_TITLE") mappedKey = "タイトル";
    else if (key == "GAMEBASE_VERSION") mappedKey = "バージョン";
    else if (key == "GAMEBASE_AUTHOR") mappedKey = "作者";
    else if (key == "GAMEBASE_YEAR") mappedKey = "製作年";
    else if (key == "GAMEBASE_INFO") mappedKey = "追加情報";
    else if (key == "GAMEBASE_WINDOW_TITLE") mappedKey = "ウィンドウタイトル";
    
    if (mappedKey == "ウィンドウタイトル") return m_windowTitle;
    if (mappedKey == "タイトル") return m_title;
    if (mappedKey == "作者") return m_author;
    if (mappedKey == "バージョン") return m_version;
    if (mappedKey == "製作年") return m_releaseYear;
    if (mappedKey == "追加情報") return m_additionalInfo;
    return QString();
}

QVariantMap GameBaseData::toMap() const
{

    QVariantMap map;
    map["ウィンドウタイトル"] = m_windowTitle;
    map["タイトル"] = m_title;
    map["作者"] = m_author;
    map["バージョン"] = m_version;
    map["製作年"] = m_releaseYear;
    map["追加情報"] = m_additionalInfo;
    return map;
}
