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
#ifndef GAME_BASE_DATA_H
#define GAME_BASE_DATA_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariantMap>
#include <QQmlEngine>

class GameBaseData : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY dataChanged)
    Q_PROPERTY(QString title READ title NOTIFY dataChanged)
    Q_PROPERTY(QString author READ author NOTIFY dataChanged)
    Q_PROPERTY(QString version READ version NOTIFY dataChanged)
    Q_PROPERTY(QString releaseYear READ releaseYear NOTIFY dataChanged)
    Q_PROPERTY(QString additionalInfo READ additionalInfo NOTIFY dataChanged)

public:
    explicit GameBaseData(QObject *parent = nullptr);
    
    // Property getters
    QString windowTitle() const;
    QString title() const;
    QString author() const;
    QString version() const;
    QString releaseYear() const;
    QString additionalInfo() const;
    
    // Data manipulation
    void set(const QString& key, const QString& value);
    QString get(const QString& key) const;
    QVariantMap toMap() const;

signals:
    void dataChanged();

private:
    QString m_windowTitle;
    QString m_title;
    QString m_author;
    QString m_version;
    QString m_releaseYear;
    QString m_additionalInfo;
    // GameBase.csv 其余键的原样留存。C# 的 GameBase.cs 用 switch 把它们逐个解析成
    // 具名字段（コード / バージョン / バージョン違い認める / 最初からいるキャラ /
    // アイテムなし / …），本移植只建模了上面 6 个显示用键，其余此前**直接丢弃**：
    // 于是 `get("最初からいるキャラ")` 恒空 —— 开局默认角色与 ADDDEFCHARA 都因此失效
    // （新开游戏 CHARANUM 停在 0，脚本惯例的 `DELCHARA 0` 直接越界报错）。
    // 未建模的键保存在这里，保证 get() 对这些键与 C# 的字段读取等价。
    QMap<QString, QString> m_extra;
};

#endif // GAME_BASE_DATA_H
