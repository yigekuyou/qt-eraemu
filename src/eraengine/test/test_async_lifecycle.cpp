#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include "eraengine.h"
#include "erb_loader.h"

static int failures = 0;
static void check(bool ok, const char* message) {
    if (!ok) { qCritical() << message; ++failures; }
}
static void write(const QString& path, const QByteArray& data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "fixture file opens");
    file.write(data);
}
static bool waitLoaded(EraEngine& engine) {
    QEventLoop loop;
    bool success = false;
    QObject::connect(&engine, &EraEngine::scriptsLoaded, &loop, [&](bool ok) {
        success = ok;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    return success;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    check(tmp.isValid(), "temporary fixture");
    const QString first = tmp.filePath("first"), second = tmp.filePath("second");
    for (const auto& root : {first, second}) {
        QDir().mkpath(root + "/CSV"); QDir().mkpath(root + "/ERB");
    }
    write(first + "/ERB/FIRST.ERB", "@SYSTEM_TITLE\n#DIM OLDVAR, 3\n#DIM CONST OLD_LITERAL = 77\nPRINT old\nRETURN\n");
    write(second + "/ERB/SECOND.ERB", "@SYSTEM_TITLE\n#DIM NEWVAR, 2\nIF 1\nPRINT new\nENDIF\nRETURN\n");
    {
        EraEngine engine;
        int completions = 0;
        QObject::connect(&engine, &EraEngine::scriptsLoaded, &engine, [&](bool) { ++completions; });
        engine.loadAsync(first);
        engine.loadAsync(second); // cancel during preparation, before event delivery
        check(waitLoaded(engine), "replacement load succeeds");
        check(completions == 1, "old preparation completion is not published");
        check(!engine.isLoadingScripts(), "published load is no longer loading");
        check(engine.getParseTable()->scriptNames() == QStringList{"SECOND.ERB"}, "only replacement AST remains");
        QWeakPointer<ExpressionNode> oldNode;
        if (const auto* script = engine.getParseTable()->script("SECOND.ERB")) {
            for (const auto& line : script->lines) if (line.condition) oldNode = line.condition;
        }
        check(!oldNode.isNull(), "fixture holds a finalized AST node");
        auto* vars = engine.getVariableStorage();
        vars->setGlobalInt1DInCurrentFrame("OLDVALUE", 0, 77);
        vars->setGlobalStr1DInCurrentFrame("OLDTEXT", 0, "old");
        vars->setCharaStrRaw("NAME", 3, 0, "old character");
        vars->markCsvExists(3); vars->snapshotCharaTemplates(); vars->addChara(3);
        engine.getConsole()->print("old output");
        QList<std::function<void()>> timerCallbacks;
        engine.getSystemStateMachine()->setTimer([&](int, std::function<void()> cb) {
            timerCallbacks.append(std::move(cb));
        });
        engine.getSystemStateMachine()->waitTimedInput(100, 99);
        VariableTable temporaryTable;
        auto& table = temporaryTable;
        table.setConstInt("OLD_CONST", 77);
        table.setConstStr("OLD_CONST_STR", "old");
        table.setConstArray("OLD_CONST_ARRAY", {1, 2});
        table.setConstStrArray("OLD_CONST_STR_ARRAY", {"old", "text"});
        table.setConstExprs("OLD_CONST_EXPR", {"1 + 2"});
        auto dimOwner = QSharedPointer<int>::create(1);
        QWeakPointer<int> oldDimOwner = dimOwner;
        table.setDimEvaluator([dimOwner](const QString&, const QString&) { return *dimOwner; });
        dimOwner.clear();
        engine.gameBaseData()->set("タイトル", "old title");
        engine.gameBaseData()->set("コード", "777");
        engine.loadAsync(second);
        table.clear();
        qint64 oldInt = 0;
        QString oldString;
        check(!table.constInt("OLD_CONST", oldInt), "clear releases scalar integer constants");
        check(!table.constStr("OLD_CONST_STR", oldString), "clear releases scalar string constants");
        check(table.constArraySize("OLD_CONST_ARRAY") == 0, "clear releases integer constant arrays");
        check(table.constStrArraySize("OLD_CONST_STR_ARRAY") == 0, "clear releases string constant arrays");
        check(table.constExprs("OLD_CONST_EXPR").isEmpty(), "clear releases constant source expressions");
        check(oldDimOwner.isNull(), "clear releases dimension evaluator captures");
        check(engine.gameBaseData()->title().isEmpty() && engine.gameBaseData()->get("コード").isEmpty(),
              "reload without GameBase.csv releases old metadata");
        for (const auto& callback : timerCallbacks) callback();
        check(engine.getProcessState()->getExecState() == ExecState::Halt,
              "old timed input cannot resume execution during reload");
        check(oldNode.isNull(), "reload destroys old AST nodes");
        check(vars->getGlobalInt1DInCurrentFrame("OLDVALUE", 0) == 0, "reload releases numeric variables");
        check(vars->getGlobalStr1DInCurrentFrame("OLDTEXT", 0).isEmpty(), "reload releases flat strings");
        check(vars->charaNum() == 0 && !vars->existCsv(3), "reload releases characters and CSV templates");
        check(engine.getConsole()->rowCount() == 0, "reload releases console rows");
        check(engine.getParseTable()->scriptNames().isEmpty(), "reload releases old AST before preparation");
        check(waitLoaded(engine), "same directory reload succeeds");
        check(completions == 2, "one completion per accepted load");
    }
    {
        EraEngine engine;
        bool replaced = false;
        int completions = 0;
        auto& loader = engine.getExecutionEngine()->getErbLoader();
        QObject::connect(&engine, &EraEngine::scriptsLoaded, &engine, [&](bool) { ++completions; });
        // 收尾（markLoadComplete）在 loadCompleted 回调内执行，随后才发 scriptsLoaded。
        QObject::connect(&engine, &EraEngine::scriptsLoadStarted, &engine, [&] {
            if (replaced) return;
            QObject::connect(&loader, &ErbLoader::loadCompleted, &engine, [&](bool ok) {
                if (!ok || replaced) return;
                check(engine.isLoadingScripts(), "loading includes the finalization window");
                replaced = true;
                engine.loadAsync(second);
            });
        });
        engine.loadAsync(first);
        check(waitLoaded(engine), "replacement during finalization succeeds");
        check(replaced && completions == 1, "stale completion callback does not publish");
        check(engine.getParseTable()->scriptNames() == QStringList{"SECOND.ERB"}, "no old parse data survives");
        qint64 oldConstant = 0;
        check(!engine.getParseTable()->variableTable().constInt("OLD_LITERAL", oldConstant),
              "replacement clears old ERB constants");
    }
    {
        ErbLoader loader;
        loader.loadDirectoryAsync(first + "/ERB");
        loader.cancelAndWait();
        check(!loader.isLoading() && loader.getLoadedScripts().isEmpty(), "cancel releases loader results");
        loader.loadDirectoryAsync(second + "/ERB");
        QEventLoop loop;
        bool ok = false;
        QObject::connect(&loader, &ErbLoader::loadCompleted, &loop, [&](bool loaded) { ok = loaded; loop.quit(); });
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        check(ok && loader.getLoadedScripts().contains("SECOND.ERB"), "standalone loader restarts after cancellation");
    }
    {
        EraEngine engine;
        bool replaced = false;
        int completions = 0;
        QObject::connect(&engine, &EraEngine::scriptsLoaded, &engine, [&](bool) { ++completions; });
        QObject::connect(&engine, &EraEngine::scriptsLoadProgress, &engine, [&](int processed, int) {
            if (processed > 0 && !replaced) {
                replaced = true;
                engine.loadAsync(second);
            }
        });
        engine.loadAsync(first);
        check(waitLoaded(engine), "replacement during merge succeeds");
        check(replaced && completions == 1, "merge continuation from old load is discarded");
        check(engine.getParseTable()->scriptNames() == QStringList{"SECOND.ERB"}, "merge replacement contains only new script");
    }
    {
        const QString scopedRoot = tmp.filePath("scoped");
        QDir().mkpath(scopedRoot + "/CSV"); QDir().mkpath(scopedRoot + "/ERB");
        write(scopedRoot + "/ERB/SCOPED.ERB",
              "@SYSTEM_TITLE\n#DIMS SHARED\nPRINTS @\"%SHARED%\"\nRETURN\n@SECOND\n#DIM SHARED\nRETURN\n");
        EraEngine engine;
        engine.loadAsync(scopedRoot);
        check(waitLoaded(engine), "private FORM async load succeeds");
        const auto* line = engine.getParseTable()->lineAt("SCOPED.ERB", 2);
        check(line && line->arguments.first().ast && !line->isError && line->argument.typeOk,
              "worker FORM receives owner private type before publication");
        check(engine.parseWarnings().isEmpty(), "valid private FORM emits no load diagnostics");
    }
    { EraEngine engine; engine.loadAsync(first); } // destruction during preparation
    { ErbLoader loader; loader.loadDirectoryAsync(first + "/ERB"); }
    QCoreApplication::processEvents();
    return failures ? 1 : 0;
}
