#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <limits>
#include "execution_engine.h"
#include "expression_evaluator.h"
#include "era_parse_table.h"
#include "ast_builder.h"
#include "game_base_data.h"

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); int failures=0;
    const auto check=[&](bool ok,const char* label){ if(!ok){qCritical()<<label;++failures;} };
    QTemporaryDir first,second;
    VariableStorage storage; storage.initialize(10,10);
    GameBaseData base; base.set("コード","42"); base.set("バージョン","1808"); base.set("バージョン違い認める","1700");
    ExecutionEngine engine(&storage,&base); ExpressionEvaluator evaluator; ProcessState state;
    EraParseTable table(&state); table.setVariableStorage(&storage); table.setExpressionEvaluator(&evaluator);
    engine.setParseTable(&table); engine.setExpressionEvaluator(&evaluator); engine.setGameDataDir(first.path());
    evaluator.setSaveDirectory(first.filePath("sav"));
    VariableDecl saved; saved.name="SAVED"; saved.isSavedata=true; saved.lengths={2,3}; saved.dimension=2;
    const_cast<VariableTable&>(table.variableTable()).add(saved);
    auto transient=saved; transient.name="TEMP"; transient.isSavedata=false; const_cast<VariableTable&>(table.variableTable()).add(transient);
    auto global=saved; global.name="USER_GLOBAL"; global.isGlobal=true; global.isGlobalSave=true; const_cast<VariableTable&>(table.variableTable()).add(global);
    auto unsavedGlobal=global; unsavedGlobal.name="UNSAVED_GLOBAL"; unsavedGlobal.isSavedata=false;
    const_cast<VariableTable&>(table.variableTable()).add(unsavedGlobal);
    VariableDecl local; local.name="STATIC"; local.scope=VarScope::Local; local.function="FUNC"; local.lengths={2}; local.defaultInt={19,23}; const_cast<VariableTable&>(table.variableTable()).add(local);
    VariableDecl chars; chars.name="USER_C"; chars.isCharaData=true; chars.isSavedata=true; chars.lengths={2}; const_cast<VariableTable&>(table.variableTable()).add(chars);
    storage.registerCharaDataVariable("USER_C",false,1);
    const AstResolver resolve=[&](const QString& s){return table.expressionAst(s);};
    const auto run=[&](const QString& source){engine.executeLogicalLine(AstBuilder::build(source,ScriptPosition("snapshot.ERB",0,0),resolve));};
    const auto evaluate=[&](const QString& source){auto ast=table.expressionAst(source);return ast ? evaluator.evaluate(*ast,&storage,&base).toLongLong() : -99;};
    const auto read=[&](const QString& path){QFile f(path);check(f.open(QIODevice::ReadOnly),"open file");return f.readAll();};
    const auto write=[&](const QString& path,const QByteArray& bytes){QFile f(path);check(f.open(QIODevice::WriteOnly),"open edit");check(f.write(bytes)==bytes.size(),"write edit");};
    storage.setSystemVariable("DAY",0,7); storage.setGlobalInt2D("SAVED",1,2,std::numeric_limits<qint64>::min());
    storage.setGlobalInt2D("TEMP",1,2,91); storage.setGlobalInt2D("USER_GLOBAL",1,2,77);
    storage.setGlobalInt2D("UNSAVED_GLOBAL",1,2,314);
    storage.setGlobalInt1D("GLOBAL",0,999); storage.setGlobalStr1D("SAVESTR",0,"界😀\t\n\r");
    storage.setCharaStrRaw("NAME",0,0,"zero"); storage.setCharaStrRaw("NAME",1,0,"one");
    storage.setCharaIntRaw("ABL",0,0,101); storage.setCharaIntRaw("ABL",1,0,202);
    storage.appendCharacterSnapshot({{1,false},{0,false}});
    storage.setCharaInt("USER_C",0,1,55);
    check(engine.writeSaveSnapshot(0,"C# snapshot"),"binary save succeeds");
    const QByteArray original=read(first.filePath("sav/save00.sav"));
    check(original.startsWith(QByteArray::fromHex("894552410d0a1a0a10070000")),"C#1808 header on engine output");
    EraBinaryData::Document document; check(EraBinaryData::decode(original,document),"engine document decodes");
    check(document.uniqueCode==42 && document.scriptVersion==1808,"real game code/version");
    bool hasSaved=false,hasTemp=false,hasGlobal=false;
    for(const auto& v:document.variables){ if(v.name=="SAVED"){hasSaved=true;check(v.dimensions==QList<int>({2,3}),"declared matrix dimensions");} hasTemp|=v.name=="TEMP";hasGlobal|=v.name=="GLOBAL"||v.name=="USER_GLOBAL"; }
    check(hasSaved && !hasTemp && !hasGlobal,"normal save declaration scope filtering");
    storage.setPrivateScope("FUNC",{"STATIC","SAVED"}); storage.setGlobalInt1D("STATIC",0,88);
    storage.setLocalInt(0,99);storage.setArgInt(0,98);storage.setReference("SAVED","TEMP");
    storage.setSystemVariable("DAY",0,90);
    check(engine.loadSaveSnapshot(0),"cached binary load");
    check(storage.getSystemVariable("DAY",0)==7,"ordinary game state restored");
    check(storage.getGlobalInt2D("SAVED",1,2)==std::numeric_limits<qint64>::min(),"int64 matrix restored");
    check(storage.getGlobalInt2D("TEMP",1,2)==0,"non savedata declaration reset");
    check(storage.getGlobalInt1D("GLOBAL",0)==999 && storage.getGlobalInt2D("USER_GLOBAL",1,2)==77,"normal load preserves GLOBAL lifetime");
    check(storage.getGlobalInt2D("UNSAVED_GLOBAL",1,2)==314,"unsaved GLOBAL survives normal load");
    check(storage.getSystemVariable("LASTLOAD_NO",0)==0 && storage.getSystemVariable("LASTLOAD_VERSION",0)==1808 && storage.getGlobalStr1D("LASTLOAD_TEXT",0)=="C# snapshot","last load metadata restored");
    check(storage.getLocalInt(0)==0 && storage.getArgInt(0)==0 && !storage.hasReference("SAVED"),"locals arguments and refs reset");
    storage.setPrivateScope("FUNC",{"STATIC"});check(storage.getGlobalInt1D("STATIC",0)==19,"private STATIC declaration initializer restored");storage.setLocalContext({});
    check(storage.getCharaStr("NAME",0,0)=="one" && storage.getCharaStr("NAME",1,0)=="zero","character block order maps runtime slots");
    check(storage.getCharaInt("ABL",0,0)==202 && storage.getCharaInt("ABL",1,0)==101 && storage.getCharaInt("USER_C",0,1)==55,"character values and savedata restored");
    check(storage.getGlobalStr1D("SAVESTR",0)=="界😀\t\n\r","binary string newlines and unicode survive");
    write(first.filePath("sav/save00.sav"),"eraemu-save-v1\nCHARALIST\t");
    check(engine.loadSaveSnapshot(0),"runtime cache ignores external edits"); engine.reloadSaveSnapshots();
    check(engine.checkSaveSnapshot(0)==4 && !engine.loadSaveSnapshot(0),"old Qt format rejected");
    write(first.filePath("sav/save00.sav"),original);engine.reloadSaveSnapshots();check(engine.loadSaveSnapshot(0),"binary disk reload");
    base.set("コード","43");check(engine.checkSaveSnapshot(0)==2,"mismatched game code rejected");base.set("コード","42");
    base.set("バージョン違い認める","1900");base.set("バージョン","2000");check(engine.checkSaveSnapshot(0)==3,"compatibility minimum enforced");
    base.set("バージョン","1808");check(engine.checkSaveSnapshot(0)==0,"exact version allowed below minimum");base.set("バージョン違い認める","1700");
    check(base.acceptsSave(0,1808) && !base.acceptsSave(43,1808),"zero file code wildcard");
    run("SAVEGLOBAL"); const QByteArray globalBytes=read(first.filePath("sav/global.sav"));
    check(EraBinaryData::decode(globalBytes,document) && document.type==EraBinaryData::FileType::Global,"GLOBAL actual binary path");
    storage.setGlobalInt1D("GLOBAL",0,0);storage.setGlobalInt2D("USER_GLOBAL",1,2,0);run("LOADGLOBAL");
    check(storage.getGlobalInt1D("GLOBAL",0)==999 && storage.getGlobalInt2D("USER_GLOBAL",1,2)==77,"global multidimensional cached load");
    write(first.filePath("sav/global.sav"),"{}");run("LOADGLOBAL");check(storage.getSystemVariable("RESULT",0)==1,"global cache immutable");
    engine.reloadSaveSnapshots();run("LOADGLOBAL");check(storage.getSystemVariable("RESULT",0)==0,"global corrupt reload rejected");
    check(evaluate("SAVECHARA(\"characters\",\"summary\",1)")==1,"character binary save");
    const QByteArray charBytes=read(first.filePath("dat/chara_characters.dat"));
    check(EraBinaryData::decode(charBytes,document) && document.type==EraBinaryData::FileType::Character && document.characters.size()==1,"character path/type/count");
    storage.delAllChara();check(evaluate("LOADCHARA(\"characters\")")==1 && storage.charaNum()==1 && storage.getCharaStr("NAME",0,0)=="zero","character full data append");
    write(first.filePath("dat/chara_characters.dat"),"bad");check(evaluate("LOADCHARA(\"characters\")")==1 && storage.charaNum()==2,"character cache ignores external edit");
    evaluator.reloadCharacterSnapshots();check(evaluate("LOADCHARA(\"characters\")")==0 && storage.charaNum()==2,"character corrupt reload atomic");
    write(first.filePath("dat/chara_characters.dat"),charBytes);evaluator.reloadCharacterSnapshots();check(evaluate("LOADCHARA(\"characters\")")==1,"character disk reload");
    engine.setGameDataDir(second.path());check(!engine.loadSaveSnapshot(0),"directory switch clears ordinary cache");
    evaluator.setSaveDirectory(second.filePath("sav"));check(evaluate("LOADCHARA(\"characters\")")==0,"directory switch clears character cache");
    // Per-axis resizing must preserve the overlap rectangle, including cleared tail cells.
    VariableStorage resized;VariableTable resizedTable;auto resizeDecl=saved;resizeDecl.lengths={3,2};resizedTable.add(resizeDecl);resized.setSaveVariableTable(&resizedTable);
    document={};document.type=EraBinaryData::FileType::Normal;EraBinaryData::Variable matrix;matrix.name="SAVED";matrix.dimensions={2,3};matrix.integers={1,2,3,4,5,6};document.variables={matrix};resized.applyBinarySave(document);
    check(resized.getGlobalInt2D("SAVED",1,0)==4 && resized.getGlobalInt2D("SAVED",1,1)==5 && resized.getGlobalInt2D("SAVED",2,0)==0,"resize copies per axis instead of flattened prefix");
    engine.setSaveFormatProvider([]{return QPair<bool,bool>{true,true};});
    check(engine.writeSaveSnapshot(2,"gzip"),"engine compressed save writes");
    const auto compressed=read(second.filePath("sav/save02.sav"));
    check(compressed.startsWith(QByteArray::fromHex("894552415a49500a")) && EraBinaryData::decode(compressed,document),"engine honors gzip setting");
    engine.setSaveFormatProvider([]{return QPair<bool,bool>{false,true};});
    storage.delAllChara();
    storage.setGlobalStr1D("SAVESTR",0,"text界😀");
    check(engine.writeSaveSnapshot(3,"text"),"native text mode saves");
    const auto textBytes=read(second.filePath("sav/save03.sav"));
    check(textBytes.startsWith(QByteArray::fromHex("efbbbf")) && textBytes.contains("__EMUERA_1808_STRAT__"),"text mode honors binary false even if zip true");
    engine.reloadSaveSnapshots();storage.setSystemVariable("DAY",0,123);
    check(engine.loadSaveSnapshot(3) && storage.getSystemVariable("DAY",0)==7,"native text reload restores state");
    run("SAVEGLOBAL");const auto textGlobal=read(second.filePath("sav/global.sav"));
    EraBinaryData::Document globalSchema;globalSchema.type=EraBinaryData::FileType::Global;
    check(EraBinaryData::decodeText(textGlobal,globalSchema,document),"native GLOBAL text decodes");
    engine.reloadSaveSnapshots();run("LOADGLOBAL");check(storage.getSystemVariable("RESULT",0)==1,"native GLOBAL reload succeeds");
    if(argc>1) { QDir().mkpath(QString::fromLocal8Bit(argv[1]));
        QFile::copy(second.filePath("sav/save03.sav"),QString::fromLocal8Bit(argv[1])+"/engine-normal-text.sav");
        QFile::copy(second.filePath("sav/global.sav"),QString::fromLocal8Bit(argv[1])+"/engine-global-text.sav"); }
    qInfo()<<"load snapshot failures:"<<failures;return failures ? 1 : 0;
}
