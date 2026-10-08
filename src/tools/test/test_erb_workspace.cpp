// Standalone regression test; links eproto + QtCore. No moc required.
#include "erb_analyzer.h"
#include <QCoreApplication>
#include <QDebug>

using namespace eproto;

static bool hasCode(const ErbDocument& doc, const QString& code) {
    for (const auto& d : doc.attributed) if (d.code == code) return true;
    return false;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ErbAnalyzer analyzer;
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) { qCritical() << what; ++failures; }
    };
    const auto syntax = analyzer.parse(QStringLiteral("    FOOBARBAZ 1\n"), QStringLiteral("x.erb"));
    check(syntax.attributed.size() == 1 && syntax.attributed.first().startCol == 4
          && syntax.attributed.first().endCol == 13, "syntax range regression");
    check(analyzer.parse(QStringLiteral("PRINT hello\n#DIM Y\n"), QStringLiteral("x.erb"))
              .attributed.isEmpty(), "parse must retain syntax-only compatibility");

    const QString caller = QStringLiteral("C:/project/a.erb");
    const QString callee = QStringLiteral("C:/project/z.erb");
    const QString source = QStringLiteral("@MAIN\nPRINTS TEXT()\n");
    const auto isolated = analyzer.analyzeWorkspace({{caller, source}});
    check(hasCode(isolated[caller], QStringLiteral("arg-check")), "undefined function must be diagnosed");
    for (const auto& d : isolated[caller].attributed)
        if (d.code == QLatin1String("arg-check"))
            check(d.line == 1, "semantic diagnostics must preserve structured physical line");
    const auto repeated = analyzer.analyzeWorkspace({
        {caller, QStringLiteral("@MAIN\nPRINTS MISSING()\nPRINTS MISSING()\n")},
        {callee, QStringLiteral("@OTHER\nPRINTS MISSING()\n")}});
    int repeatedWarnings = 0;
    for (const auto& doc : repeated)
        for (const auto& d : doc.attributed)
            if (d.code == QLatin1String("arg-check") && d.message.contains(QLatin1String("MISSING")))
                ++repeatedWarnings;
    check(repeatedWarnings == 3, "each occurrence must retain its semantic diagnostic");
    const auto workspace = analyzer.analyzeWorkspace({
        {caller, source}, {callee, QStringLiteral("@TEXT\n#FUNCTIONS\nRETURNF \"ok\"\n")}});
    check(!hasCode(workspace[caller], QStringLiteral("arg-check")), "cross-file string function binding");
    const auto replaced = analyzer.analyzeWorkspace({{caller, source}});
    check(hasCode(replaced[caller], QStringLiteral("arg-check")), "removed files must not leave stale symbols");

    const QString header = QStringLiteral("file:///project/z.ERH");
    const auto declarations = analyzer.analyzeWorkspace({
        {header, QStringLiteral("#DIMS SHARED\n#DIMS SHARED\n#DEFINE ENABLED\n")},
        {caller, QStringLiteral("@MAIN\n[IF ENABLED]\nPRINTS SHARED\nVISIBLE_BAD_INSTRUCTION\n[ENDIF]\n")}});
    check(hasCode(declarations[header], QStringLiteral("decl-error")), "global duplicate declaration");
    check(!hasCode(declarations[caller], QStringLiteral("arg-check")), "header string variable type");
    check(hasCode(declarations[caller], QStringLiteral("unknown-instruction")),
          "workspace preprocessing must share defines");
    const auto preprocessing = analyzer.analyzeWorkspace({{caller, QStringLiteral("[SKIPSTART]\n")}});
    check(!preprocessing[caller].preprocessWarnings.isEmpty()
          && hasCode(preprocessing[caller], QStringLiteral("preprocess")), "preprocessing diagnostics must be attributed");
    const auto erbDefine = analyzer.analyzeWorkspace({
        {caller, QStringLiteral("#DEFINE SCRIPT_ONLY\n@MAIN\n[IF SCRIPT_ONLY]\nBAD_INSTRUCTION\n[ENDIF]\n")}});
    check(!hasCode(erbDefine[caller], QStringLiteral("unknown-instruction")),
          "script defines must not enter the global header macro table");
    check(analyzer.analyzeWorkspace({}).isEmpty(), "empty workspace");
    return failures == 0 ? 0 : 1;
}
