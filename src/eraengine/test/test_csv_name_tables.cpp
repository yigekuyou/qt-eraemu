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
// ---------------------------------------------------------------------------
// test_csv_name_tables.cpp —— CSV 名表 / STR 变量语义（依据 ecd/docs 规范）
//
// 文档依据（仓库内 ecd/docs）：
//   * guide/EraBasic_Variables.html：
//       「STR —— 文本数据。string[]0~19999，**Str.csv 的数据保存在这里**」
//   * reference/CSV_File_Format.html：
//       「Str.csv 的格式：第一栏字符串编号，第二栏字符串」
//   * reference/Variable.html：
//       「STRNAME ← StrName.csv」「**Str.csv 指定的是 STR 的内容，而不是元素的名称**」
// 对齐实现（C# GameData/ConstantData.cs）：
//   * loadDataTo("STR.CSV", strIndex)      -> STR 的**值**
//   * loadDataTo("STRNAME.CSV", strnameIndex) -> STRNAME（STR 的**名表**）
//   * 逆引き辞書の構築で i == 10 を skip（注释「Strは逆引き無用」）
//   * ResolveName: case VariableCode.STR -> nameToIntDics[strnameIndex]（errPos strname.csv）
// 即：`STR:名前` 的名表是 **StrName.csv**，不是 Str.csv。
// ---------------------------------------------------------------------------
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "constant_table.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) {
        qDebug().noquote() << "  [ok ]" << what;
    } else {
        qDebug().noquote() << "  [FAIL]" << what;
        ++g_failures;
    }
}

static bool writeFile(const QString& path, const QString& content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(content.toUtf8());
    return true;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "CSV 名表与 STR 变量测试（依据 ecd/docs 规范）";
    qDebug() << "==============================================";

    // ---- 1) 变量 -> 名表 CSV 的映射 ----------------------------------------
    // 对齐 C# ConstantData.ResolveName / 文档 Variable.html 的「CSV 文件」列。
    qDebug() << "\n1) csvForVariable 映射（变量:名字 用哪张名表）";
    check(ConstantTable::csvForVariable(QStringLiteral("STR"))
              == QStringLiteral("STRNAME.CSV"),
          "STR 的名表是 StrName.csv（不是 Str.csv）");
    check(ConstantTable::csvForVariable(QStringLiteral("TSTR"))
              == QStringLiteral("TSTR.CSV"),
          "TSTR 的名表是 TSTR.CSV");
    check(ConstantTable::csvForVariable(QStringLiteral("SAVESTR"))
              == QStringLiteral("SAVESTR.CSV"),
          "SAVESTR 的名表是 SAVESTR.CSV");
    check(ConstantTable::csvForVariable(QStringLiteral("CFLAG"))
              == QStringLiteral("CFLAG.CSV"),
          "CFLAG 的名表是 CFLAG.CSV");
    check(ConstantTable::csvForVariable(QStringLiteral("TALENT"))
              == QStringLiteral("TALENT.CSV"),
          "TALENT 的名表是 TALENT.CSV");

    // ---- 2) 装载临时 CSV 目录：值表与名表刻意不同 --------------------------
    qDebug() << "\n2) Str.csv（值）与 StrName.csv（名）分离";
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        qDebug() << "  [FAIL] 无法创建临时目录";
        return 1;
    }
    const QString csvDir = tmp.path();
    const bool okWrite =
        writeFile(QDir(csvDir).filePath(QStringLiteral("Str.csv")),
                  QStringLiteral("6001,\u503c6001\n6002,\u503c6002\n"))
        && writeFile(QDir(csvDir).filePath(QStringLiteral("StrName.csv")),
                     QStringLiteral("6001,\u540d6001\n6002,\u540d6002\n"));
    check(okWrite, "写入 Str.csv / StrName.csv");

    ConstantTable ct;
    const int loaded = ct.loadCsvDirectory(csvDir, false);
    check(loaded >= 2, "装载至少 2 张表");

    // 值：来自 Str.csv（文档「Str.csv 的数据保存在这里」）
    check(ct.nameAt(QStringLiteral("STR.CSV"), 6001) == QStringLiteral("\u503c6001"),
          "Str.csv 提供 STR 的值：STR[6001] == 值6001");
    check(ct.nameAt(QStringLiteral("STR.CSV"), 6002) == QStringLiteral("\u503c6002"),
          "Str.csv 提供 STR 的值：STR[6002] == 值6002");

    // 名：来自 StrName.csv（文档「Str.csv 指定的是 STR 的内容，而不是元素的名称」）
    check(ct.nameAt(QStringLiteral("STRNAME.CSV"), 6001) == QStringLiteral("\u540d6001"),
          "StrName.csv 提供 STRNAME 的名");
    check(ct.indexForVariable(QStringLiteral("STR"), QStringLiteral("\u540d6001")) == 6001,
          "STR:名6001 —— 名字取自 StrName.csv -> 下标 6001");
    check(ct.indexForVariable(QStringLiteral("STR"), QStringLiteral("\u503c6001")) == -1,
          "STR:值6001 —— Str.csv 的值文本**不能**当名字（逆引き無用）");

    // ---- 3) 缺 StrName.csv 时：STR 的名字解析应失败（返回 -1），值仍可用 ----
    qDebug() << "\n3) 只有 Str.csv（eraTW 的情形：没有 StrName.csv）";
    QTemporaryDir tmp2;
    if (!tmp2.isValid()) {
        qDebug() << "  [FAIL] 无法创建临时目录";
        return 1;
    }
    const QString csvDir2 = tmp2.path();
    writeFile(QDir(csvDir2).filePath(QStringLiteral("Str.csv")),
              QStringLiteral("6001,\u503c6001\n"));
    ConstantTable ct2;
    ct2.loadCsvDirectory(csvDir2, false);
    check(ct2.nameAt(QStringLiteral("STR.CSV"), 6001) == QStringLiteral("\u503c6001"),
          "无 StrName.csv 时 STR 的值仍来自 Str.csv");
    check(ct2.indexForVariable(QStringLiteral("STR"), QStringLiteral("\u540d6001")) == -1,
          "无 StrName.csv 时 STR:名 不可解析（返回 -1）");

    // ---- 4) EE 用户变量 CSV 查表层及保留名表边界 -----------------------------
    // EmueraEM+EE 文档把“ERH 中声明的一维变量 + 变量名.csv”定义为完整
    // 功能；本测试只覆盖 ConstantTable 的最后一级查表层，因为它不持有
    // VariableTable，不能单独证明声明来源或维数限制。核心/扩展显式映射
    // 必须优先，保留的 _*.csv 不进入名表。
    qDebug() << "\n4) EE 用户变量同名 CSV 兜底";
    QTemporaryDir tmp3;
    if (!tmp3.isValid()) {
        qDebug() << "  [FAIL] 无法创建临时目录";
        return 1;
    }
    const QString csvDir3 = tmp3.path();
    const bool okWrite3 =
        writeFile(QDir(csvDir3).filePath(QStringLiteral("FLCONFIG.csv")),
                  QStringLiteral("101,BGM音量\n102,SE音量\n"))
        && writeFile(QDir(csvDir3).filePath(QStringLiteral("_Rename.csv")),
                     QStringLiteral("1,不要作为名表\n"))
        && writeFile(QDir(csvDir3).filePath(QStringLiteral("CFLAG.csv")),
                     QStringLiteral("0,核心零\n1,核心一\n"));
    check(okWrite3, "写入用户变量、保留文件和核心同名 CSV");

    ConstantTable ct3;
    ct3.loadCsvDirectory(csvDir3, false);
    check(ct3.indexForVariable(QStringLiteral("FLCONFIG"), QStringLiteral("BGM音量")) == 101,
          "EE 用户变量名 == CSV 文件名时可解析字符串下标");
    check(ct3.indexForVariable(QStringLiteral("FLCONFIG"), QStringLiteral("SE音量")) == 102,
          "EE 用户变量第二个名称保持声明行号");
    check(ct3.indexForVariable(QStringLiteral("FLCONFIG"), QStringLiteral("不要作为名表")) == -1,
          "_Rename.csv 等保留文件不参与用户变量名表");
    check(ct3.indexForVariable(QStringLiteral("CFLAG"), QStringLiteral("核心一")) == 1,
          "核心变量映射优先于同名用户 CSV 兜底");
    check(ct3.indexForVariable(QStringLiteral("NO_SUCH_VARIABLE"), QStringLiteral("BGM音量")) == -1,
          "无同名 CSV 的普通变量不会借用其他表");

    qDebug() << "\n==============================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] CSV 名表 / STR 测试通过";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
