#include "game_base_data.h"

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
				emit dataChanged(); // 关键：通知 QML 属性已变更
		}
}

QString GameBaseData::get(const QString& key) const
{
    if (key == "ウィンドウタイトル") return m_windowTitle;
    if (key == "タイトル") return m_title;
    if (key == "作者") return m_author;
    if (key == "バージョン") return m_version;
    if (key == "製作年") return m_releaseYear;
    if (key == "追加情報") return m_additionalInfo;
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
