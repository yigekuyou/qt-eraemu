#include <QCoreApplication>
#include <QDebug>
#include "ast/ast_builder.h"
#include "ast/expression_evaluator.h"
#include "process_state.h"
#include "era_parse_table.h"
#include "execution_engine.h"
#include "script_runner.h"
#include "variable_storage.h"

static int failures = 0;
static void run(const QString& name, const QStringList& source, qint64 expected, ExecState expectedState = ExecState::Halt, bool restart = false) {
    VariableStorage storage;
    ProcessState state;
    EraParseTable table(&state);
    ExecutionEngine engine(&storage, nullptr);
    ExpressionEvaluator evaluator;
    engine.setParseTable(&table);
    engine.setExpressionEvaluator(&evaluator);
    table.setExpressionEvaluator(&evaluator);
    table.setVariableStorage(&storage);
    ScriptRunner runner(&table, &engine, &state, &storage);
    runner.setExpressionEvaluator(&evaluator);
    runner.setStepLimit(1000);
    QList<LogicalLine> lines;
    QStringList script = {"@MAIN"};
    script.append(source);
    for (int i = 0; i < script.size(); ++i)
        lines.append(AstBuilder::build(script[i], ScriptPosition("flow.ERB", i, 0),
            [&table](const QString& text) { return table.expressionAst(text); }));
    table.loadScript("flow", lines);
    table.setEntryPoint("MAIN");
    auto result = runner.runToCompletion();
    if (restart) {
        table.setEntryPoint("MAIN");
        result = runner.runToCompletion();
    }
    const qint64 actual = storage.getGlobalInt1D("CHECK", 0);
    if (result != expectedState || actual != expected) {
        qCritical() << name << "state" << int(result) << "actual" << actual << "expected" << expected;
        ++failures;
    } else qInfo() << "PASS" << name;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    run("64 bit IF/SIF/ELSEIF", {"IF 4294967296", "CHECK += 1", "ENDIF",
        "SIF -4294967296", "CHECK += 2", "IF 0", "CHECK = 999", "ELSEIF 4294967296", "CHECK += 4", "ENDIF"}, 7);
    run("nested branches and loops", {"FOR A, 0, 3", "IF A == 1", "CONTINUE", "ELSE", "WHILE 1",
        "SELECTCASE A", "CASE 0", "CHECK += 2", "CASEELSE", "CHECK += 3", "ENDSELECT", "BREAK", "WEND", "ENDIF", "NEXT"}, 5);
    run("DO LOOP and CONTINUE", {"DO", "A += 1", "SIF A == 2", "CONTINUE", "CHECK += A", "LOOP A < 3"}, 4);
    run("DO BREAK", {"DO", "CHECK += 1", "BREAK", "LOOP 1"}, 1);
    run("REPEAT COUNT REND", {"REPEAT 3", "CHECK += COUNT + 1", "REND"}, 6);
    run("REPEAT counter mutation", {"REPEAT 5", "CHECK += 1", "COUNT += 1", "REND"}, 3);
    run("REPEAT zero resets COUNT", {"COUNT = 9", "REPEAT 0", "CHECK = 999", "REND", "CHECK = COUNT"}, 0);
    run("BREAK increments FOR counter", {"FOR A, 2, 10, 3", "BREAK", "NEXT", "CHECK = A"}, 5);
    run("FOR initialization before end", {"A = 99", "FOR A, 0, A + 3", "CHECK += 1", "NEXT"}, 3);
    run("FOR expression index", {"B = 2", "FOR A:B, 0, 3", "CHECK += A:2 + 1", "NEXT"}, 6);
    run("FOR descending/empty/zero step", {"FOR A, 3, 0, -1", "CHECK += A", "NEXT", "FOR A, 1, 1", "CHECK = 999", "NEXT",
        "FOR A, 0, 9, 0", "CHECK = 999", "NEXT", "WHILE 0", "CHECK = 999", "WEND"}, 6);
    run("CASE mixed forms", {"SELECTCASE 5", "CASE 1, 4 TO 6, IS > 10", "CHECK = 1", "CASEELSE", "CHECK = 999", "ENDSELECT"}, 1);
    run("CASE reversed range", {"SELECTCASE 5", "CASE 6 TO 4", "CHECK = 999", "CASEELSE", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE string relation", {"SELECTCASE \"abc\"", "CASE IS > \"zzz\"", "CHECK = 999", "CASE IS < \"def\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE quoted TO", {"SELECTCASE \"A TO B\"", "CASE \"X TO Y\"", "CHECK = 999", "CASE \"A TO B\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE string range", {"SELECTCASE \"m\"", "CASE \"a\" TO \"c\"", "CHECK = 999", "CASE \"j\" TO \"z\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("call restores loops and locals", {"FOR LOCAL:0, 0, 3", "CALL SUB", "CHECK += LOCAL:0 + 1", "NEXT", "RETURN", "@SUB",
        "FOR LOCAL:0, 0, 10", "RETURN", "NEXT"}, 6);
    run("bare RETURN clears RESULT", {"CALL FIRST", "CALL SECOND", "CHECK = RESULT", "RETURN", "@FIRST", "RETURN 9", "@SECOND", "RETURN"}, 0);
    run("implicit RETURN clears RESULT", {"CALL FIRST", "CALL SECOND", "CHECK = RESULT", "RETURN", "@FIRST", "RETURN 9", "@SECOND", "A = 1", "@THIRD", "RETURN 99"}, 0);
    run("SIF skips next logical instruction", {"SIF 0", "", "; comment", "CHECK = 999", "CHECK += 1"}, 1);
    run("branch ending at function boundary", {"IF 0", "CHECK = 999", "ENDIF", "@SECOND", "CHECK = 777"}, 0);
    run("short circuit side effects", {"IF 0 && ++A", "CHECK = 999", "ENDIF", "IF 1 || ++A", "CHECK = A + 1", "ENDIF"}, 1);
    run("CALL return at function boundary", {"CALL SUB", "@OTHER", "CHECK = 999", "RETURN", "@SUB", "CHECK = 1", "RETURN"}, 1);
    run("nested SELECT", {"SELECTCASE 1", "CASE 1", "SELECTCASE 2", "CASE 2", "CHECK = 1", "CASEELSE", "CHECK = 999", "ENDSELECT", "CASEELSE", "CHECK = 999", "ENDSELECT"}, 1);
    run("REPEAT continue and BREAK counter", {"REPEAT 4", "SIF COUNT < 2", "CONTINUE", "CHECK += COUNT", "BREAK", "REND", "CHECK += COUNT"}, 5);
    run("FOR body changes counter", {"FOR A, 0, 6", "CHECK += 1", "A += 1", "NEXT"}, 3);
    run("callee cannot break caller loop", {"FOR A, 0, 3", "CALL SUB", "CHECK += 1", "NEXT", "RETURN", "@SUB", "BREAK", "RETURN"}, 3);
    run("method loop shares step budget", {"CHECK = SPIN()", "RETURN", "@SPIN", "#FUNCTION", "WHILE 1", "WEND", "RETURNF 0"}, 0, ExecState::Error);
    run("RETURN list", {"CALL SUB", "CHECK = RESULT:0 + RESULT:1 + RESULT:2", "RETURN", "@SUB", "RETURN 1, 2, 4"}, 7);
    run("method RETURNF preserves RESULT", {"RESULT = 9", "CHECK = VALUE()", "CHECK += RESULT", "RETURN", "@VALUE", "#FUNCTION", "RETURNF 2"}, 11);
    run("restart clears unfinished loops", {"CHECK = 0", "WHILE 1", "CHECK += 1", "RETURN", "WEND"}, 1, ExecState::Halt, true);
    run("64 bit WHILE and DO condition", {"A = 4294967296", "WHILE A", "CHECK += 1", "A = 0", "WEND", "DO", "CHECK += 1", "A += 1", "LOOP (A < 2) * 4294967296"}, 3);
    return failures ? 1 : 0;
}
