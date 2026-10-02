#include "extension_registry.h"

#include <QFile>
#include <QSet>
#include <QSharedPointer>
#include <QTextStream>
#include <QtGlobal>

#include "eraengine_log.h"
#include "execution_engine.h"

// ---------------------------------------------------------------------------
// 默认扩展名单（EE 存档系）
//
// 只收 **C# 原版没有、EE 发行版激活**的函数（Creator.cs 里被注释、EE 注册）：
//   CHKVARDATA / CHKGLOBALDATA / FIND_VARDATA。
// PUTFORM / FIND_CHARADATA 属核心（BuiltInFunctionCode.cs 枚举内，
// kBuiltinFunctions 表 + BuiltinOp 真实现），不在此登记 —— 扩展不得覆盖核心
// （Xorg: 0-127 核心段保留），否则注册表桩会遮蔽核心真实现。
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kEeDefaultExtensions[] = {
    "CHKVARDATA", "CHKGLOBALDATA", "FIND_VARDATA",
};
constexpr const char* kManifestFile = "emuera_extensions.txt";

// 统一扩展桩：留痕一次 + 跳过 + return true（对齐 EE 的容错语义，不报错）；
// 每个名字只留痕一次，便于从运行日志定位后续补全点。
ExecutionEngine::StatementFn makeExtensionStub(const QString& name)
{
    auto seen = QSharedPointer<QSet<QString>>::create();
    return [name, seen](const LogicalLine& line, const QList<Operand>&) -> bool {
        if (!seen->contains(name)) {
            seen->insert(name);
            qCDebug(eraTrace) << "[ee-ext]" << name
                              << "在运行期被跳过（扩展桩，待补全）。行:"
                              << line.position.toString();
        }
        return true;
    };
}
} // namespace

void registerEngineExtensions(ExecutionEngine& engine, const QString& gameDirectory)
{
    // ① 编译期默认名单
    for (const char* raw : kEeDefaultExtensions) {
        const QString name = QString::fromLatin1(raw);
        engine.registerStatementFunction(name, makeExtensionStub(name));
    }

    // ② 运行期清单发现（wl_registry.bind 式）：游戏目录声明文件，可选
    const QString manifestPath = gameDirectory + QLatin1Char('/') + QLatin1String(kManifestFile);
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        engine.registerStatementFunction(line, makeExtensionStub(line));
    }
    qDebug() << "[ext] 扩展清单已绑定:" << manifestPath;
}
