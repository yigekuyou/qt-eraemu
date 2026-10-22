// test_load_snapshots.cpp
//
// 2026-10-22 golden 口径变更（本文件不再保存版本字面量）：
//   私有作用域身份由函数名改为段身份 (脚本, 段起点行) —— 夹具里的 #DIM SAVEDATA
//   条目改按 eravar::segmentKeyText 建作用域；存档容器版本 1808 -> 1809，旧
//   golden 不可比。头里的版本字段与文本起始标记里的版本号一律从
//   EraBinaryData::Version 派生，避免第三份副本。
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QtEndian>
#include <limits>
#include "execution_engine.h"
#include "expression_evaluator.h"
#include "era_parse_table.h"
#include "ast_builder.h"
#include "game_base_data.h"



[[maybe_unused]] static QString scopeKeyOfFunction(const VariableTable& vt, const QString& function) {
    for (const VariableDecl& d : vt.declarations()) {
        if (!d.scopeKey.isEmpty() && d.function.compare(function, Qt::CaseInsensitive) == 0)
            return d.scopeKey;
    }
    return QString();
}

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
    VariableDecl local; local.name="STATIC"; local.scope=VarScope::Local; local.function="FUNC"; local.scopeKey=eravar::segmentKeyText("snapshot.ERB",0); local.lengths={2}; local.defaultInt={19,23}; const_cast<VariableTable&>(table.variableTable()).add(local);
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
    check(original.startsWith(QByteArray::fromHex("894552410d0a1a0a")),"binary magic on engine output");
    {
        // 版本字段由 EraBinaryData::Version 派生（测试不再抄 1808/1809 字面量）。
        QByteArray version(4, '\0');
        qToLittleEndian<quint32>(EraBinaryData::Version, version.data());
        check(original.mid(8,4)==version,"engine writes the current container version");
    }
    EraBinaryData::Document document; check(EraBinaryData::decode(original,document),"engine document decodes");
    check(document.uniqueCode==42 && document.scriptVersion==1808,"real game code/version");
    bool hasSaved=false,hasTemp=false,hasGlobal=false;
    for(const auto& v:document.variables){ if(v.name=="SAVED"){hasSaved=true;check(v.dimensions==QList<int>({2,3}),"declared matrix dimensions");} hasTemp|=v.name=="TEMP";hasGlobal|=v.name=="GLOBAL"||v.name=="USER_GLOBAL"; }
    check(hasSaved && !hasTemp && !hasGlobal,"normal save declaration scope filtering");
    storage.setPrivateScope(eravar::internName(local.scopeKey),{"STATIC","SAVED"}); storage.setGlobalInt1D("STATIC",0,88);
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
    storage.setPrivateScope(eravar::internName(local.scopeKey),{"STATIC"});check(storage.getGlobalInt1D("STATIC",0)==19,"private STATIC declaration initializer restored");storage.setLocalContext({});
    check(storage.getCharaStr("NAME",0,0)=="one" && storage.getCharaStr("NAME",1,0)=="zero","character block order maps runtime slots");
    check(storage.getCharaInt("ABL",0,0)==202 && storage.getCharaInt("ABL",1,0)==101 && storage.getCharaInt("USER_C",0,1)==55,"character values and savedata restored");
    check(storage.getGlobalStr1D("SAVESTR",0)=="界😀\t\n\r","binary string newlines and unicode survive");
    write(first.filePath("sav/save00.sav"),"eraemu-save-v1\nCHARALIST\t");
    check(engine.loadSaveSnapshot(0),"runtime cache ignores external edits"); engine.reloadSaveSnapshots();
    check(engine.checkSaveSnapshot(0)==1 && !engine.loadSaveSnapshot(0),"old Qt format treated as no data");
    check(engine.writeSaveSnapshot(0,"overwrite unsupported"),"unsupported save can be overwritten");
    check(engine.checkSaveSnapshot(0)==0 && engine.loadSaveSnapshot(0),"overwritten unsupported save loads");
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
    // RESETDATA restores ERB/ERH declaration initializers but leaves GLOBAL lifetime intact.
    VariableDecl resetDim; resetDim.name="RESET_DIM"; resetDim.defaultInt={500};
    const_cast<VariableTable&>(table.variableTable()).add(resetDim);
    VariableDecl resetDims; resetDims.name="RESET_DIMS"; resetDims.type=OperandType::Str; resetDims.defaultStr={"ready"};
    const_cast<VariableTable&>(table.variableTable()).add(resetDims);
    VariableDecl resetGlobal=resetDim; resetGlobal.name="RESET_GLOBAL"; resetGlobal.isGlobal=true;
    const_cast<VariableTable&>(table.variableTable()).add(resetGlobal);
    storage.setGlobalInt1D("RESET_DIM",0,1);
    storage.setGlobalStr1D("RESET_DIMS",0,"changed");
    storage.setGlobalInt1D("RESET_GLOBAL",0,77);
    storage.resetForNewGame();
    check(storage.getGlobalInt1D("RESET_DIM",0)==500,"RESETDATA restores #DIM initializer");
    check(storage.getGlobalStr1D("RESET_DIMS",0)=="ready","RESETDATA restores #DIMS initializer");
    check(storage.getGlobalInt1D("RESET_GLOBAL",0)==77,"RESETDATA preserves #GLOBAL value");
    // 2/3 维的下标换算（最后一维连续）与「初值没覆盖到的尾部清 0」是
    // resetForNewGame 重写后的关键行为：批量 memset + 直写下标，不再逐元素
    // 调 setGlobal*D / coordinates()（FL3.8 有 400 万个元素，旧写法要 1 秒以上）。
    VariableDecl reset2d; reset2d.name="RESET_2D"; reset2d.dimension=2; reset2d.lengths={2,3};
    reset2d.defaultInt={1,2,3,4,5,6};
    VariableDecl reset2dPartial; reset2dPartial.name="RESET_2D_PART"; reset2dPartial.dimension=2;
    reset2dPartial.lengths={2,2}; reset2dPartial.defaultInt={7};
    VariableDecl reset3d; reset3d.name="RESET_3D"; reset3d.dimension=3; reset3d.lengths={2,2,2};
    reset3d.defaultInt={10,11,12,13,14,15,16,17};
    VariableDecl reset2ds; reset2ds.name="RESET_2DS"; reset2ds.type=OperandType::Str;
    reset2ds.dimension=2; reset2ds.lengths={2,2}; reset2ds.defaultStr={"a","b","c","d"};
    for (const VariableDecl& d : {reset2d, reset2dPartial, reset3d, reset2ds})
        const_cast<VariableTable&>(table.variableTable()).add(d);
    storage.setGlobalInt2D("RESET_2D",1,2,99); storage.setGlobalInt3D("RESET_3D",1,1,1,99);
    storage.setGlobalStr2D("RESET_2DS",1,1,"z"); storage.setGlobalInt2D("RESET_2D_PART",0,1,99);
    storage.resetForNewGame();
    check(storage.getGlobalInt2D("RESET_2D",0,0)==1 && storage.getGlobalInt2D("RESET_2D",0,2)==3
          && storage.getGlobalInt2D("RESET_2D",1,0)==4 && storage.getGlobalInt2D("RESET_2D",1,2)==6,
          "RESETDATA restores 2-D array along the last axis");
    check(storage.getGlobalInt3D("RESET_3D",0,0,0)==10 && storage.getGlobalInt3D("RESET_3D",1,0,0)==14
          && storage.getGlobalInt3D("RESET_3D",1,1,1)==17,
          "RESETDATA restores 3-D array (x*ny*nz + y*nz + z)");
    check(storage.getGlobalInt2D("RESET_2D_PART",0,0)==7 && storage.getGlobalInt2D("RESET_2D_PART",0,1)==0
          && storage.getGlobalInt2D("RESET_2D_PART",1,0)==0,
          "2-D tail beyond the initializer is cleared, not left over");
    check(storage.getGlobalStr2D("RESET_2DS",0,0)=="a" && storage.getGlobalStr2D("RESET_2DS",1,1)=="d",
          "RESETDATA restores 2-D string array");
    storage.setSystemVariable("DAY",0,7);

    engine.setSaveFormatProvider([]{return QPair<bool,bool>{true,true};});
    check(engine.writeSaveSnapshot(2,"gzip"),"engine compressed save writes");
    const auto compressed=read(second.filePath("sav/save02.sav"));
    check(compressed.startsWith(QByteArray::fromHex("894552415a49500a")) && EraBinaryData::decode(compressed,document),"engine honors gzip setting");
    engine.setSaveFormatProvider([]{return QPair<bool,bool>{false,true};});
    storage.delAllChara();
    storage.setGlobalStr1D("SAVESTR",0,"text界😀");
    check(engine.writeSaveSnapshot(3,"text"),"native text mode saves");
    const auto textBytes=read(second.filePath("sav/save03.sav"));
    const QByteArray textMarker = QByteArrayLiteral("__EMUERA_")+QByteArray::number(EraBinaryData::Version)+QByteArrayLiteral("_STRAT__");
    check(textBytes.startsWith(QByteArray::fromHex("efbbbf")) && textBytes.contains(textMarker),"text mode honors binary false even if zip true");
    engine.reloadSaveSnapshots();storage.setSystemVariable("DAY",0,123);
    const bool nativeTextLoaded = engine.loadSaveSnapshot(3);
    check(nativeTextLoaded,"native text reload succeeds");
    check(storage.getSystemVariable("DAY",0)==7,"native text reload restores state");
    run("SAVEGLOBAL");const auto textGlobal=read(second.filePath("sav/global.sav"));
    EraBinaryData::Document globalSchema;globalSchema.type=EraBinaryData::FileType::Global;
    check(EraBinaryData::decodeText(textGlobal,globalSchema,document),"native GLOBAL text decodes");
    engine.reloadSaveSnapshots();run("LOADGLOBAL");check(storage.getSystemVariable("RESULT",0)==1,"native GLOBAL reload succeeds");
    // ---- eraUpperKey 的大小写折叠语义（零分配快路径不改结果）----
    //   · 纯 CJK 名字 toUpper() 恒等 —— 读写必须仍落同一槽位；
    //   · 全角小写 ａ(U+FF41) 属 Ll（isLower==true），仍要正确映射到 Ａ 键
    //    （此前「任何 >= 0x80 字符都 toUpper」改成「真含小写字母才 toUpper」，
    //     这两条是回归面）。
    {
        storage.setGlobalInt1D(QStringLiteral("触手"),0,5);
        storage.setGlobalInt1D(QStringLiteral("全角ＡＢ"),1,7);
        check(storage.getGlobalInt1D(QStringLiteral("触手"),0)==5,
              "eraUpperKey：纯 CJK 名字读写落同一槽位（toUpper 恒等）");
        check(storage.getGlobalInt1D(QStringLiteral("全角ａｂ"),1)==7,
              "eraUpperKey：全角小写 ａｂ 仍映射到 ＡＢ 键");
        storage.setPrivateScope(eravar::internName(QStringLiteral("函")),{QStringLiteral("私変数")});
        check(storage.resolvedStorageName(QStringLiteral("私変数"))
                  == QStringLiteral("函\x1f私変数"),
              "eraUpperKey：私有作用域映射对 CJK 名字不受影响");
        storage.setLocalContext({});
    }
    if(argc>1) { QDir().mkpath(QString::fromLocal8Bit(argv[1]));
        QFile::copy(second.filePath("sav/save03.sav"),QString::fromLocal8Bit(argv[1])+"/engine-normal-text.sav");
        QFile::copy(second.filePath("sav/global.sav"),QString::fromLocal8Bit(argv[1])+"/engine-global-text.sav"); }
    qInfo()<<"load snapshot failures:"<<failures;return failures ? 1 : 0;
}
