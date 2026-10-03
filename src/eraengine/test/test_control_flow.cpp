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
    // 调用族：以前只实现了 CALL，CALLFORM/TRYCALL/TRYCALLFORM 全被静默跳过
    // （eraTW `CALLFORM CUSTOM_%ARGS%_MENU(ARG)` 不执行 -> 菜单空白）。
    run("CALLFORM 格式化标签", {
        "A = 7",
        "CALLFORM SUB_%A%(A)",
        "RETURN",
        "@SUB_7(NUM)",
        "CHECK = NUM * 10",
        "RETURN"}, 70);
    run("TRYCALL 存在则调用", {
        "TRYCALL SUB_9",
        "CHECK += 1",
        "RETURN",
        "@SUB_9",
        "CHECK = 5",
        "RETURN"}, 6);
    run("TRYCALLFORM 不存在则静默跳过", {
        "A = 3",
        "CHECK = 1",
        "TRYCALLFORM NOPE_%A%",
        "CHECK += 1"}, 2);
    // TRYC 系 + CATCH + THROW（eraTW 的 EXISTOBJ：TRYCCALLFORM EXIST_xxx + CALLF SET_EXIST）
    //   * 目标存在   -> 执行 TRY 体，落到 CATCH 时跳到 ENDCATCH 之后
    //   * 目标不存在 -> 从 CATCH 的**下一行**开始执行异常体
    run("TRYCCALLFORM 命中 = 正常调用", {
        "A = 1",
        "TRYCCALLFORM SUB_%A%",
        "CHECK = 10",
        "CATCH",
        "CHECK = 999",
        "ENDCATCH",
        "RETURN",
        "@SUB_1",
        "CHECK += 1",
        "RETURN"}, 10);
    run("TRYCCALLFORM 未命中 = 进入 CATCH", {
        "A = 2",
        "TRYCCALLFORM SUB_%A%",
        "CHECK = 10",
        "CATCH",
        "CHECK = 7",
        "ENDCATCH",
        "RETURN",
        "@SUB_1",
        "CHECK = 999",
        "RETURN"}, 7);
    run("TRY 无 CATCH 未命中 = 静默跳过", {
        "A = 3",
        "TRYCCALLFORM SUB_%A%",
        "CHECK = 1",
        "RETURN",
        "@SUB_1",
        "CHECK = 999",
        "RETURN"}, 1);
    // THROW **不会**被 CATCH 接住：Emuera 文档《异常分支：TRYC / CATCH / ENDCATCH》
    // THROW 中断执行（C# THROW_Instruction -> throw new CodeEE）：
    // TRYC 系的 CATCH 只捕获「函数不存在」，不捕获被调函数里抛出的 THROW。
    // 曾有一段时期这里「只告警不中断」以便观察上游求值缺陷；上游缺陷修完
    // 后已恢复 C# 语义 —— THROW 所在脚本以 Error 状态终止（CHECK = 999）。
    run("THROW 中断执行（CATCH 不捕获，异常体不执行）", {
        "TRYCCALLFORM THROWER",
        "CHECK = 10",
        "CATCH",
        "CHECK = 3",
        "ENDCATCH",
        "RETURN",
        "@THROWER",
        "CHECK = 999",
        "THROW \"boom\"",
        "RETURN"}, 999, ExecState::Error);
    // CALLF：调用式中関数；对齐 C# CALLF_Instruction（mToken.GetValue(exm)），
    // **返回值被丢弃**，不写 RESULT —— 此前把返回值写进 RESULT / LOCALS:0，
    // LOCALS:0 会覆盖调用者的局部槽（eraTW 的 TEMP_RE_STR 因此恒返回空串）。
    // 这里用副作用验证函数确实被调用。
    run("CALLF 调用式中関数（返回值丢弃，副作用生效）", {
        "CALLF DOUBLE(21)",
        "CHECK = CHECK:1",
        "RETURN",
        "@DOUBLE(N)",
        "#FUNCTION",
        "CHECK:1 = N * 2",
        "RETURNF N * 2"}, 42);
    run("CALLFORMF 格式化函数名（返回值丢弃，副作用生效）", {
        "A = 7",
        "CALLFORMF TRIPLE_%A%(A)",
        "CHECK = CHECK:1",
        "RETURN",
        "@TRIPLE_7(N)",
        "#FUNCTION",
        "CHECK:1 = N * 3",
        "RETURNF N * 3"}, 21);
    run("CASE mixed forms", {"SELECTCASE 5", "CASE 1, 4 TO 6, IS > 10", "CHECK = 1", "CASEELSE", "CHECK = 999", "ENDSELECT"}, 1);
    run("CASE reversed range", {"SELECTCASE 5", "CASE 6 TO 4", "CHECK = 999", "CASEELSE", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE string relation", {"SELECTCASE \"abc\"", "CASE IS > \"zzz\"", "CHECK = 999", "CASE IS < \"def\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE quoted TO", {"SELECTCASE \"A TO B\"", "CASE \"X TO Y\"", "CHECK = 999", "CASE \"A TO B\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("CASE string range", {"SELECTCASE \"m\"", "CASE \"a\" TO \"c\"", "CHECK = 999", "CASE \"j\" TO \"z\"", "CHECK = 1", "ENDSELECT"}, 1);
    run("call restores loops and locals", {"FOR LOCAL:0, 0, 3", "CALL SUB", "CHECK += LOCAL:0 + 1", "NEXT", "RETURN", "@SUB",
        "FOR LOCAL:0, 0, 10", "RETURN", "NEXT"}, 6);
    run("bare RETURN clears RESULT", {"CALL FIRST", "CALL SECOND", "CHECK = RESULT", "RETURN", "@FIRST", "RETURN 9", "@SECOND", "RETURN"}, 0);
    // 对齐 C#：自然结束走 state.Return(0)，Return/ReturnF 不写 RESULT ——
    // 只有显式 RETURN 语句写。eraTW 选地主的 `FLAG:地主 = RESULT` 靠此语义。
    run("implicit RETURN preserves RESULT", {"CALL FIRST", "CALL SECOND", "CHECK = RESULT", "RETURN", "@FIRST", "RETURN 9", "@SECOND", "A = 1", "@THIRD", "RETURN 99"}, 9);
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
