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
#ifndef AST_FUNCTION_TYPES_H
#define AST_FUNCTION_TYPES_H

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <utility>
#include <QHash>
#include <QString>
#include <QtGlobal>
#include "operand_type.h"

// ---------------------------------------------------------------------------
// 内置函数（内部命令）目录 —— 对齐 C# GameData/Function/FunctionMethodCreator
//
//   C# 在 `Creator.cs` 里把 162 个 `FunctionMethod` 注册进 `methodList`，
//   解析期由 `IdentifierDictionary.GetFunctionMethod(LabelDictionary, name, args)`
//   查表；查不到就是「未定义関数」，查得到则调用 `CheckArgumentType()` 做
//   **参数个数 + 逐位类型**校验，失败抛 `CodeEE`（被 ParserMediator 收成告警）。
//
//   本表把「名字 → 返回类型 + 参数个数 + 位置参数形态」全部搬进 AST，
//   使解析期就能：
//     * 给 `FunctionNode` 标注强类型（valueType()）；
//     * 在 `validateBuiltinCall()` 里复刻 CheckArgumentType；
//     * 区分「内置函数 / 用户自定义函数 / 未定义函数」。
//
//   位置参数形态（argPattern，逐字符）：
//     a = Any      任意表达式（整数或字符串）
//     i = Int      整数表达式
//     s = Str      字符串表达式
//     v = Var      变量（任意类型；C# `is VariableTerm`）
//     n = VarInt   整数变量
//     t = VarStr   字符串变量
//     A = VarArray 数组变量（1D/2D/3D）
//     1 = VarIntArray 一维整数数组变量
//     c = VarChara 角色变量
//   模式短于实参个数时，多出来的位置按 Any 处理（如 MIN/MAX/GROUPMATCH 的可变长实参）。
// ---------------------------------------------------------------------------

enum class BuiltinArg : quint8 {
    Any,          // 任意表达式
    Int,          // 整数表达式
    Str,          // 字符串表达式
    Var,          // 变量（任意）
    VarInt,       // 整数变量
    VarStr,       // 字符串变量
    VarArray,     // 数组变量
    VarIntArray,  // 一维整数数组变量
    VarChara,     // 角色变量
};

[[nodiscard]] constexpr BuiltinArg builtinArgFromCode(char c) noexcept {
    switch (c) {
    case 'i': return BuiltinArg::Int;
    case 's': return BuiltinArg::Str;
    case 'v': return BuiltinArg::Var;
    case 'n': return BuiltinArg::VarInt;
    case 't': return BuiltinArg::VarStr;
    case 'A': return BuiltinArg::VarArray;
    case '1': return BuiltinArg::VarIntArray;
    case 'c': return BuiltinArg::VarChara;
    default:  return BuiltinArg::Any;
    }
}

[[nodiscard]] constexpr std::string_view builtinArgName(BuiltinArg a) noexcept {
    switch (a) {
    case BuiltinArg::Any:         return "任意";
    case BuiltinArg::Int:         return "整型";
    case BuiltinArg::Str:         return "字符串";
    case BuiltinArg::Var:         return "变量";
    case BuiltinArg::VarInt:      return "整型变量";
    case BuiltinArg::VarStr:      return "字符串变量";
    case BuiltinArg::VarArray:    return "数组变量";
    case BuiltinArg::VarIntArray: return "一维整型数组变量";
    case BuiltinArg::VarChara:    return "角色变量";
    }
    return "?";
}

// 求值 opcode：ExpressionEvaluator 用 switch 分派（跳转表）。
// 未实现求值的内置函数标 None（解析/校验仍然完整）。
enum class BuiltinOp : quint16 {
    None = 0,
    Abs, Sign, Rand, Min, Max, Limit,
    Power, Sqrt, Cbrt, Log, Log10, Exponent,
    GetBit, InRange, StrLen, StrLenU, Substring, SubstringU,
    StrFind, StrFindU, StrCount, ToStr, ToInt, ToUpper,
    ToLower, ToHalf, ToFull, Replace, Unicode, UnicodeByte,
    Convert, IsNumeric, Escape, EncodeToUni, CharAtU, StrForm, ChkData,
    LineIsEmpty, StrJoin, GetTime, GetTimes, GetMillisecond, GetSecond,
    MoneyStr, BarStr, PrintCPerLine, PrintCLength, SumArray, SumCharaArray,
    MaxArray, MaxCharaArray, MinArray, MinCharaArray, InRangeArray, InRangeCharaArray,
    Match, CharaMatch, GroupMatch, Nosames, Allsames, FindElement,
    FindLastElement, GetNum, VarSize, GetConfig, GetConfigs,
    ExistCsv, CsvChara, GetColor, GetStyle,
    // ---- 显示状态查询（语句形式与式中形式都用；对齐 C# 的 Console 系函数）----
    GetDefColor, GetBgColor, GetDefBgColor, CurrentRedraw,
    // ---- 格式化串长度（STRLENFORM / STRLENFORMU）----
    StrLenForm, StrLenFormU,
    // ---- G 图像 / 精灵（对齐 C# Graphics*Method / Sprite*Method）----
    GCreated, GWidth, GHeight, GCreate, GCreateFromFile, GDispose,
    GClear, GFillRectangle, GDrawSprite, GDrawG, GGetColor, GSetColor,
    SpriteCreated, SpriteWidth, SpriteHeight, SpritePosX, SpritePosY,
    SpriteMove, SpriteSetPos, SpriteCreate, SpriteDispose, SpriteGetColor,
    // ---- 显示状态 / 输入（文档「显示处理」「AWAIT 相关」组）----
    CurrentAlign, GetFocusColor, GetFont, ChkFont,
    ClientWidth, ClientHeight, Isskip, Messkip, MouseSkip,
    GetLineStr, GetKey, GetKeyTriggered, MouseX, MouseY, IsActive,
    // ---- 扩展式中函数（EE 等；实现在注册类的扩展侧，求值经注入的回调）----
    Extension,
    // ---- 随机数状态（RANDDATA）----
    DumpRand, InitRand,
    // ---- 角色操作 / 检索（文档「角色操作·引用」组）----
    GetChara, GetSpChara, FindChara, FindCharaLast,
    FindCharaData, FindCharaDataLast, ChkCharaData,
    CopyChara, AddCopyChara, SwapChara, PickupChara,
    LoadChara, SaveChara, ResetStain,
    // ---- 存档 / 文本（文档「游戏存档的操作」组）----
    SaveText, LoadText, PutForm, SaveNos, DebugClear,
    // ---- 数组 / 阈值 ----
    ArrayMSort, GetPalamLv, GetExpLv,
    // ---- 颜色名 / 其它 ----
    ColorFromName, ColorFromRgb, SetAnimeTimer, TwAit, ResetBgColor,
    // ---- HTML（文档「HTML_PRINT 相关」）----
    HtmlEscape, HtmlToPlainText, HtmlGetPrintedStr, HtmlPopPrintingStr,
    // ---- G 图像补全 / CBG 背景 / 精灵动画 ----
    GSetBrush, GSetPen, GSetFont, GSave, GLoad, GDrawGWithMask,
    CbgSetG, CbgSetSprite, CbgClear, CbgClearButton, CbgRemoveRange,
    CbgRemoveBmap, CbgSetBmapG, CbgSetButtonSprite,
    SpriteAnimeCreate, SpriteAnimeAddFrame,
    // ---- G 系补全（EE/EM：GDRAWTEXT / GDRAWLINE / GDASHSTYLE / GDRAWGWITHROTATE
    //      与状态回读 GGET* / GGETTEXTSIZE；画布就是 GraphicsStore 的 QImage）----
    GDrawText, GDrawLine, GDashStyle, GDrawGWithRotate,
    // SPRITEDISPOSEALL(delCsvImage)：C# AppContents.SpriteDisposeAll
    SpriteDisposeAll,
    GGetFont, GGetFontSize, GGetFontStyle, GGetBrush, GGetPen, GGetPenWidth, GGetTextSize,
};

struct BuiltinFunctionSpec {
    std::string_view name;          // 大写函数名
    OperandType      ret;           // 返回类型（C# FunctionMethod.ReturnType）
    qint8            minArgs;       // 最少实参个数
    qint8            maxArgs;       // 最多实参个数（-1 = 不限）
    std::string_view argPattern;    // 逐位置的参数形态（见上表）
    bool             canRestructure;// C# CanRestructure（常量折叠候选）
    BuiltinOp        op;            // 求值 opcode（None = 尚未实现求值）
};

inline constexpr BuiltinFunctionSpec kBuiltinFunctions[] = {
    // 名字 / 返回类型 / 参数个数[min,max] / 位置参数形态 / 可否常量折叠 / 求值 opcode
    // （对齐 C# GameData/Function/Creator.Method.cs 的 FunctionMethodCreator 方法表）
    {"GETCHARA"            , OperandType::Int, 1, 2, "ii", false , BuiltinOp::GetChara},
    {"GETSPCHARA"          , OperandType::Int, 1, 1, "i", false, BuiltinOp::GetSpChara},
    {"CSVNAME"             , OperandType::Str, 1, 2, "ii", true , BuiltinOp::CsvChara},
    {"CSVCALLNAME"         , OperandType::Str, 1, 2, "ii", true , BuiltinOp::CsvChara},
    {"CSVNICKNAME"         , OperandType::Str, 1, 2, "ii", true , BuiltinOp::CsvChara},
    {"CSVMASTERNAME"       , OperandType::Str, 1, 2, "ii", true , BuiltinOp::CsvChara},
    {"CSVCSTR"             , OperandType::Str, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVBASE"             , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVABL"              , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVMARK"             , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVEXP"              , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVRELATION"         , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVTALENT"           , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVCFLAG"            , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVEQUIP"            , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"CSVJUEL"             , OperandType::Int, 2, 3, "iii", true , BuiltinOp::CsvChara},
    {"FINDCHARA"           , OperandType::Int, 2, 4, "vaii", false , BuiltinOp::FindChara},
    {"FINDLASTCHARA"       , OperandType::Int, 2, 4, "vaii", false , BuiltinOp::FindCharaLast},
    {"EXISTCSV"            , OperandType::Int, 1, 2, "ii", true , BuiltinOp::ExistCsv},
    {"VARSIZE"             , OperandType::Int, 1, 2, "ai", true , BuiltinOp::VarSize},
    {"CHKFONT"             , OperandType::Int, 1, 1, "s", true , BuiltinOp::ChkFont},
    {"CHKDATA"             , OperandType::Int, 1, 1, "i", false, BuiltinOp::ChkData},
    {"ISSKIP"              , OperandType::Int, 0, 0, """", false, BuiltinOp::Isskip},
    {"MOUSESKIP"           , OperandType::Int, 0, 0, """", false, BuiltinOp::MouseSkip},
    {"MESSKIP"             , OperandType::Int, 0, 0, """", false, BuiltinOp::Messkip},
    {"GETCOLOR"            , OperandType::Int, 0, 0, """", false , BuiltinOp::GetColor},
    {"GETDEFCOLOR"         , OperandType::Int, 0, 0, """", false , BuiltinOp::GetDefColor},
    {"GETFOCUSCOLOR"       , OperandType::Int, 0, 0, """", false , BuiltinOp::GetFocusColor},
    {"GETBGCOLOR"          , OperandType::Int, 0, 0, """", false , BuiltinOp::GetBgColor},
    {"GETDEFBGCOLOR"       , OperandType::Int, 0, 0, """", false , BuiltinOp::GetDefBgColor},
    {"GETSTYLE"            , OperandType::Int, 0, 0, """", false, BuiltinOp::GetStyle},
    {"GETFONT"             , OperandType::Str, 0, 0, """", false, BuiltinOp::GetFont},
    {"BARSTR"              , OperandType::Str, 3, 3, "iii", true , BuiltinOp::BarStr},
    {"CURRENTALIGN"        , OperandType::Str, 0, 0, """", false, BuiltinOp::CurrentAlign},
    {"CURRENTREDRAW"       , OperandType::Int, 0, 0, """", false, BuiltinOp::CurrentRedraw},
    {"COLOR_FROMNAME"      , OperandType::Int, 1, 1, "s", true , BuiltinOp::ColorFromName},
    {"COLOR_FROMRGB"       , OperandType::Int, 3, 3, "iii", true , BuiltinOp::ColorFromRgb},
    {"CHKCHARADATA"        , OperandType::Int, 1, 1, "s", false, BuiltinOp::ChkCharaData},
    {"FIND_CHARADATA"      , OperandType::Int, 0, 1, "s", false, BuiltinOp::FindCharaData},
    {"MONEYSTR"            , OperandType::Str, 1, 2, "is", true , BuiltinOp::MoneyStr},
    {"PRINTCPERLINE"       , OperandType::Int, 0, 0, """", false , BuiltinOp::PrintCPerLine},
    {"PRINTCLENGTH"        , OperandType::Int, 0, 0, """", false , BuiltinOp::PrintCLength},
    {"SAVENOS"             , OperandType::Int, 0, 0, """", true , BuiltinOp::SaveNos},
    {"GETTIME"             , OperandType::Int, 0, 0, """", false, BuiltinOp::GetTime},
    {"GETTIMES"            , OperandType::Str, 0, 0, """", false, BuiltinOp::GetTimes},
    {"GETMILLISECOND"      , OperandType::Int, 0, 0, """", false, BuiltinOp::GetMillisecond},
    {"GETSECOND"           , OperandType::Int, 0, 0, """", false, BuiltinOp::GetSecond},
    {"RAND"                , OperandType::Int, 1, 2, "ii", false, BuiltinOp::Rand},
    {"MIN"                 , OperandType::Int, 1, -1, "i", false, BuiltinOp::Min},
    {"MAX"                 , OperandType::Int, 1, -1, "i", false, BuiltinOp::Max},
    {"ABS"                 , OperandType::Int, 1, 1, "i", true , BuiltinOp::Abs},
    {"POWER"               , OperandType::Int, 2, 2, "ii", true , BuiltinOp::Power},
    {"SQRT"                , OperandType::Int, 1, 1, "i", true , BuiltinOp::Sqrt},
    {"CBRT"                , OperandType::Int, 1, 1, "i", true , BuiltinOp::Cbrt},
    {"LOG"                 , OperandType::Int, 1, 1, "i", true , BuiltinOp::Log},
    {"LOG10"               , OperandType::Int, 1, 1, "i", true , BuiltinOp::Log10},
    {"EXPONENT"            , OperandType::Int, 1, 1, "i", true , BuiltinOp::Exponent},
    {"SIGN"                , OperandType::Int, 1, 1, "i", true , BuiltinOp::Sign},
    {"LIMIT"               , OperandType::Int, 3, 3, "iii", true , BuiltinOp::Limit},
    {"SUMARRAY"            , OperandType::Int, 1, 3, "nii", false, BuiltinOp::SumArray},
    {"SUMCARRAY"           , OperandType::Int, 1, 3, "vii", false, BuiltinOp::SumCharaArray},
    {"MATCH"               , OperandType::Int, 2, 4, "vaii", true , BuiltinOp::Match},
    {"CMATCH"              , OperandType::Int, 2, 4, "vaii", true , BuiltinOp::CharaMatch},
    {"GROUPMATCH"          , OperandType::Int, 2, -1, "aa", false, BuiltinOp::GroupMatch},
    {"NOSAMES"             , OperandType::Int, 2, -1, "aa", true , BuiltinOp::Nosames},
    {"ALLSAMES"            , OperandType::Int, 2, -1, "aa", true , BuiltinOp::Allsames},
    {"MAXARRAY"            , OperandType::Int, 1, 3, "1ii", true , BuiltinOp::MaxArray},
    {"MAXCARRAY"           , OperandType::Int, 1, 3, "vii", true , BuiltinOp::MaxCharaArray},
    {"MINARRAY"            , OperandType::Int, 1, 3, "1ii", true , BuiltinOp::MinArray},
    {"MINCARRAY"           , OperandType::Int, 1, 3, "vii", true , BuiltinOp::MinCharaArray},
    {"GETBIT"              , OperandType::Int, 2, 2, "ii", true , BuiltinOp::GetBit},
    {"GETNUM"              , OperandType::Int, 2, 2, "vs", false, BuiltinOp::GetNum},
    {"GETPALAMLV"          , OperandType::Int, 2, 2, "ii", false, BuiltinOp::GetPalamLv},
    {"GETEXPLV"            , OperandType::Int, 2, 2, "ii", false, BuiltinOp::GetExpLv},
    {"FINDELEMENT"         , OperandType::Int, 2, 5, "vaiii", true , BuiltinOp::FindElement},
    {"FINDLASTELEMENT"     , OperandType::Int, 2, 5, "vaiii", true , BuiltinOp::FindLastElement},
    {"INRANGE"             , OperandType::Int, 3, 3, "iii", false, BuiltinOp::InRange},
    {"INRANGEARRAY"        , OperandType::Int, 3, 6, "niii", false, BuiltinOp::InRangeArray},
    {"INRANGECARRAY"       , OperandType::Int, 3, 6, "viii", false, BuiltinOp::InRangeCharaArray},
    {"GETNUMB"             , OperandType::Int, 2, 2, "vs", false, BuiltinOp::GetNum},
    {"ARRAYMSORT"          , OperandType::Int, 2, -1, "v", false, BuiltinOp::ArrayMSort},
    // [qdbug] C# 原版全量：STRLEN/STRLENU 是基础版正式名（BuiltInFunctionCode.cs:111/114，
    //   STRLENS/STRLENSU 在 C# 基础版被注释、属 EE 扩展名 —— 两套名字都收，
    //   语义一致：STRLEN/STRLENS=字节数，STRLENU/STRLENSU=字符数）
    {"STRLEN"              , OperandType::Int, 1, 1, "s", true , BuiltinOp::StrLen},
    {"STRLENU"             , OperandType::Int, 1, 1, "s", true , BuiltinOp::StrLenU},
    {"STRLENS"             , OperandType::Int, 1, 1, "s", true , BuiltinOp::StrLen},
    {"STRLENSU"            , OperandType::Int, 1, 1, "s", true , BuiltinOp::StrLenU},
    {"SUBSTRING"           , OperandType::Str, 1, 3, "sii", true , BuiltinOp::Substring},
    {"SUBSTRINGU"          , OperandType::Str, 1, 3, "sii", true , BuiltinOp::SubstringU},
    {"STRFIND"             , OperandType::Int, 2, 3, "ssi", true , BuiltinOp::StrFind},
    {"STRFINDU"            , OperandType::Int, 2, 3, "ssi", true , BuiltinOp::StrFindU},
    {"STRCOUNT"            , OperandType::Int, 2, 2, "ss", true , BuiltinOp::StrCount},
    {"TOSTR"               , OperandType::Str, 1, 2, "is", true , BuiltinOp::ToStr},
    {"TOINT"               , OperandType::Int, 1, 1, "s", true , BuiltinOp::ToInt},
    {"TOUPPER"             , OperandType::Str, 1, 1, "s", true , BuiltinOp::ToUpper},
    {"TOLOWER"             , OperandType::Str, 1, 1, "s", true , BuiltinOp::ToLower},
    {"TOHALF"              , OperandType::Str, 1, 1, "s", true , BuiltinOp::ToHalf},
    {"TOFULL"              , OperandType::Str, 1, 1, "s", true , BuiltinOp::ToFull},
    {"LINEISEMPTY"         , OperandType::Int, 0, 0, """", false, BuiltinOp::LineIsEmpty},
    {"REPLACE"             , OperandType::Str, 3, 3, "sss", true , BuiltinOp::Replace},
    {"UNICODE"             , OperandType::Str, 1, 1, "i", true , BuiltinOp::Unicode},
    {"UNICODEBYTE"         , OperandType::Int, 1, 1, "s", true , BuiltinOp::UnicodeByte},
    {"CONVERT"             , OperandType::Str, 2, 2, "ii", true , BuiltinOp::Convert},
    {"ISNUMERIC"           , OperandType::Int, 1, 1, "s", true , BuiltinOp::IsNumeric},
    {"ESCAPE"              , OperandType::Str, 1, 1, "s", true , BuiltinOp::Escape},
    {"ENCODETOUNI"         , OperandType::Int, 1, 2, "si", true , BuiltinOp::EncodeToUni},
    {"CHARATU"             , OperandType::Str, 2, 2, "si", true , BuiltinOp::CharAtU},
    {"GETLINESTR"          , OperandType::Str, 1, 1, "s", false, BuiltinOp::GetLineStr},
    // 注：EmueraEE 式中函数（EXISTFUNCTION / GETDOINGFUNCTION / GETDISPLAYLINE）
    // 不在此原生表 —— 它们是**扩展**，经 ExtensionRegistry::regExpr 注入运行期
    // 扩展函数表（见本文件底部 extensionFunctionStore）。原生归原生、扩展归扩展。
    {"STRFORM"             , OperandType::Str, 1, 1, "s", true , BuiltinOp::StrForm},
    {"STRJOIN"             , OperandType::Str, 1, 4, "vaii", true , BuiltinOp::StrJoin},
    {"GETCONFIG"           , OperandType::Int, 1, 1, "s", true , BuiltinOp::GetConfig},
    {"GETCONFIGS"          , OperandType::Str, 1, 1, "s", true , BuiltinOp::GetConfigs},
    {"HTML_GETPRINTEDSTR"  , OperandType::Str, 0, 1, "i", false, BuiltinOp::HtmlGetPrintedStr},
    {"HTML_POPPRINTINGSTR" , OperandType::Str, 0, 0, """", false, BuiltinOp::HtmlPopPrintingStr},
    {"HTML_TOPLAINTEXT"    , OperandType::Str, 1, 1, "s", true , BuiltinOp::HtmlToPlainText},
    {"HTML_ESCAPE"         , OperandType::Str, 1, 1, "s", true , BuiltinOp::HtmlEscape},
    {"SPRITECREATED"       , OperandType::Int, 1, 1, "s", false, BuiltinOp::SpriteCreated},
    {"SPRITEWIDTH"         , OperandType::Int, 1, 1, "s", false, BuiltinOp::SpriteWidth},
    {"SPRITEHEIGHT"        , OperandType::Int, 1, 1, "s", false, BuiltinOp::SpriteHeight},
    {"SPRITEMOVE"          , OperandType::Int, 3, 3, "sii", false, BuiltinOp::SpriteMove},
    {"SPRITESETPOS"        , OperandType::Int, 3, 3, "sii", false, BuiltinOp::SpriteSetPos},
    {"SPRITEPOSX"          , OperandType::Int, 1, 1, "s", false, BuiltinOp::SpritePosX},
    {"SPRITEPOSY"          , OperandType::Int, 1, 1, "s", false, BuiltinOp::SpritePosY},
    {"CLIENTWIDTH"         , OperandType::Int, 0, 0, """", false, BuiltinOp::ClientWidth},
    {"CLIENTHEIGHT"        , OperandType::Int, 0, 0, """", false, BuiltinOp::ClientHeight},
    {"GETKEY"              , OperandType::Int, 1, 1, "i", false, BuiltinOp::GetKey},
    {"GETKEYTRIGGERED"     , OperandType::Int, 1, 1, "i", false, BuiltinOp::GetKeyTriggered},
    {"MOUSEX"              , OperandType::Int, 0, 0, """", false, BuiltinOp::MouseX},
    {"MOUSEY"              , OperandType::Int, 0, 0, """", false, BuiltinOp::MouseY},
    {"ISACTIVE"            , OperandType::Int, 0, 0, """", false, BuiltinOp::IsActive},
    // SAVETEXT str text, {int fileNo | str filepath}{, int force_savdir, int force_UTF8}
    // （C# Creator.Method.cs:6898：`argumentTypeArrayEx = {String, Any, Int, Int}, OmitStart = 2`
    //   —— 第 2 实参是 **Any**：静态类型是 long 走文件号形态，是 string 走**路径形态**
    //   （EM 私家版_LoadText＆SaveText機能拡張））
    {"SAVETEXT"            , OperandType::Int, 2, 4, "saii", false, BuiltinOp::SaveText},
    // LOADTEXT {int fileNo | str filepath}{, int force_savdir, int force_UTF8}
    // （C# Creator.Method.cs:7007：`{ArgType.Any, ArgType.Int, ArgType.Int}, OmitStart = 1`）
    {"LOADTEXT"            , OperandType::Str, 1, 3, "aii", false, BuiltinOp::LoadText},
    // 注：EM 私family 的 GETVAR / GETVARS / SETVAR / EXISTVAR（变量名字符串化）、
    // EXISTFILE / ISDEFINED / REGEXPMATCH / HTML_STRINGLEN/SUBSTRING/LINES 都
    // **不在本表** —— 它们是 fork 扩展，登记在扩展侧（ExtensionRegistry::regExpr，
    // 见 GameProc/fork_em.cpp）。本表只放 C# 原版 `FunctionMethodCreator` 的方法。
    {"GCREATED"            , OperandType::Int, 1, 1, "i", false, BuiltinOp::GCreated},
    {"GWIDTH"              , OperandType::Int, 1, 1, "i", false, BuiltinOp::GWidth},
    {"GHEIGHT"             , OperandType::Int, 1, 1, "i", false, BuiltinOp::GHeight},
    {"GGETCOLOR"           , OperandType::Int, 3, 3, "iii", false, BuiltinOp::GGetColor},
    {"SPRITEGETCOLOR"      , OperandType::Int, 3, 3, "sii", false, BuiltinOp::SpriteGetColor},
    {"GCREATE"             , OperandType::Int, 3, 3, "iii", false, BuiltinOp::GCreate},
    {"GCREATEFROMFILE"     , OperandType::Int, 2, 2, "is", false, BuiltinOp::GCreateFromFile},
    {"GDISPOSE"            , OperandType::Int, 1, 1, "i", false, BuiltinOp::GDispose},
    {"GCLEAR"              , OperandType::Int, 2, 2, "ii", false, BuiltinOp::GClear},
    {"GFILLRECTANGLE"      , OperandType::Int, 5, 5, "iiiii", false, BuiltinOp::GFillRectangle},
    // GDRAWSPRITE：C# 允许 2/4/6/7 个实参（7 = 带颜色矩阵，第 7 实参是 2D/3D 数组变量）
    {"GDRAWSPRITE"         , OperandType::Int, 2, 7, "isiiiia", false, BuiltinOp::GDrawSprite},
    {"GSETCOLOR"           , OperandType::Int, 4, 4, "iiii", false, BuiltinOp::GSetColor},
    {"GDRAWG"              , OperandType::Int, 10, 11, "iiiiiiiiiia", false, BuiltinOp::GDrawG},
    {"GDRAWGWITHMASK"      , OperandType::Int, 5, 5, "iiiii", false, BuiltinOp::GDrawGWithMask},
    {"GSETBRUSH"           , OperandType::Int, 2, 2, "ii", false, BuiltinOp::GSetBrush},
    // GSETFONT(ID, 字体名{, 字号, 字体样式})：C# `argumentTypeArrayEx` 的
    // `{ArgType.Int, ArgType.String, ArgType.Int, ArgType.Int}, OmitStart = 2`
    //（Creator.Method.cs:5447）。第 4 参是 EE 扩展的**字体样式**位掩码
    //（1=粗体 2=斜体 4=删除线 8=下划线，见 test/data/functions/GSETFONT.md）。
    // 此前上限写死 3，FL3.8 的 `GSETFONT gid, "Arial", THICK, 0` 被误报「参数过多」。
    // 求值侧（GraphicsStore::gSetFont）暂不应用样式（两处调用的样式都是 0）。
    {"GSETFONT"            , OperandType::Int, 2, 4, "isii", false, BuiltinOp::GSetFont},
    {"GSETPEN"             , OperandType::Int, 3, 3, "iii", false, BuiltinOp::GSetPen},
    // SPRITECREATE(name, gID[, x, y, w, h])：把 G 图像登记成具名精灵
    {"SPRITECREATE"        , OperandType::Int, 2, 6, "siiiii", false, BuiltinOp::SpriteCreate},
    {"SPRITEDISPOSE"       , OperandType::Int, 1, 1, "s", false, BuiltinOp::SpriteDispose},
    // ---- G 系补全（EE/EM；语义见 commands/GDRAWTEXT.md / GDRAWLINE.md / GDASHSTYLE.md
    //      与 C# Creator.Method.cs:5568(GraphicsDrawString)/5807(GraphicsDrawLine)/
    //      5549(GraphicsSetDashStyle)/5759(GraphicsDrawGWithRotate)/5305(GraphicsState)）----
    // GDRAWTEXT(gid, text{, x, y})：2 或 4 参（C# OmitStart=2）；画完写
    // RESULT:1=文本宽 / RESULT:2=文本高，返回 1。
    {"GDRAWTEXT"           , OperandType::Int, 2, 4, "isii", false, BuiltinOp::GDrawText},
    {"GDRAWLINE"           , OperandType::Int, 5, 5, "iiiii", false, BuiltinOp::GDrawLine},
    {"GDASHSTYLE"          , OperandType::Int, 3, 3, "iii", false, BuiltinOp::GDashStyle},
    // GDRAWGWITHROTATE(dst, src, angle{, px, py})：3 或 5 参（3 参时轴心 = src 中心）
    {"GDRAWGWITHROTATE"    , OperandType::Int, 3, 5, "iiiii", false, BuiltinOp::GDrawGWithRotate},
    // SPRITEDISPOSEALL(delCsvImage)：丢弃运行期精灵（非 0 时连同 CSV 图集精灵），
    // 返回丢弃数（C# SpriteDisposeAllMethod：`arguments[0] != 0` 传 delCsvImage）
    {"SPRITEDISPOSEALL"    , OperandType::Int, 1, 1, "i", false, BuiltinOp::SpriteDisposeAll},
    {"GGETFONT"            , OperandType::Str, 1, 1, "i", false, BuiltinOp::GGetFont},
    {"GGETFONTSIZE"        , OperandType::Int, 1, 1, "i", false, BuiltinOp::GGetFontSize},
    {"GGETFONTSTYLE"        , OperandType::Int, 1, 1, "i", false, BuiltinOp::GGetFontStyle},
    {"GGETBRUSH"           , OperandType::Int, 1, 1, "i", false, BuiltinOp::GGetBrush},
    {"GGETPEN"             , OperandType::Int, 1, 1, "i", false, BuiltinOp::GGetPen},
    {"GGETPENWIDTH"        , OperandType::Int, 1, 1, "i", false, BuiltinOp::GGetPenWidth},
    // GGETTEXTSIZE(text, fontname, fontsize{, style})：RESULT:1 = 高，返回宽
    {"GGETTEXTSIZE"        , OperandType::Int, 3, 4, "ssii", false, BuiltinOp::GGetTextSize},
    {"CBGSETG"             , OperandType::Int, 4, 4, "iiii", false, BuiltinOp::CbgSetG},
    {"CBGSETSPRITE"        , OperandType::Int, 4, 4, "siii", false, BuiltinOp::CbgSetSprite},
    {"CBGCLEAR"            , OperandType::Int, 0, 0, """", false, BuiltinOp::CbgClear},
    {"CBGCLEARBUTTON"      , OperandType::Int, 0, 0, """", false, BuiltinOp::CbgClearButton},
    {"CBGREMOVERANGE"      , OperandType::Int, 2, 2, "ii", false, BuiltinOp::CbgRemoveRange},
    {"CBGREMOVEBMAP"       , OperandType::Int, 0, 0, """", false, BuiltinOp::CbgRemoveBmap},
    {"CBGSETBMAPG"         , OperandType::Int, 1, 1, "i", false, BuiltinOp::CbgSetBmapG},
    // CBGSETBUTTONSPRITE <按钮值>, <精灵名>, <选中精灵名>, <x>, <y>, <z>{, <tooltip>}
    // C# CBGSETButtonSpriteMethod：ArgTypeList OmitStart=6 -> 第 7 参 tooltip 可省，
    // 合法实参个数是 6 或 7（Command.html 也写了 6 参与 7 参两种形态）。
    {"CBGSETBUTTONSPRITE"  , OperandType::Int, 6, 7, "issiiis", false, BuiltinOp::CbgSetButtonSprite},
    {"GSAVE"               , OperandType::Int, 2, 2, "ii", false, BuiltinOp::GSave},
    {"GLOAD"               , OperandType::Int, 2, 2, "ii", false, BuiltinOp::GLoad},
    {"SPRITEANIMECREATE"   , OperandType::Int, 3, 3, "sii", false, BuiltinOp::SpriteAnimeCreate},
    {"SPRITEANIMEADDFRAME" , OperandType::Int, 9, 9, "siiiiiiii", false, BuiltinOp::SpriteAnimeAddFrame},
    {"SETANIMETIMER"       , OperandType::Int, 1, 1, "i", false, BuiltinOp::SetAnimeTimer},

    // ------------------------------------------------------------------
    // 语句形式的内部函数（对齐 C# FunctionIdentifier.funcDic 里
    // 「只有 ArgumentBuilder、没有 Instruction」的一类：它们既能作式中函数，
    // 也能单独成行当**函数语句**用，返回值写 RESULT / RESULTS:0）。
    // eraTW 里实际出现但此前完全没登记的名字都补在这里。
    // ------------------------------------------------------------------
    {"TWAIT"               , OperandType::Int, 1, 2, "ii", false, BuiltinOp::TwAit},   // TWAIT <ms>[, <skip>]
    {"RESETBGCOLOR"        , OperandType::Int, 0, 0, """", false, BuiltinOp::ResetBgColor},   // 背景色复位
    {"RESET_STAIN"         , OperandType::Int, 1, 1, "i", false, BuiltinOp::ResetStain},    // 污渍清零
    {"SAVECHARA"           , OperandType::Int, 2, -1, "ssi", false, BuiltinOp::SaveChara}, // <文件>,<摘要>,<角色番号>…
    {"LOADCHARA"           , OperandType::Int, 1, 1, "s", false, BuiltinOp::LoadChara},
    {"SWAPCHARA"           , OperandType::Int, 2, 2, "ii", false, BuiltinOp::SwapChara},
    {"COPYCHARA"           , OperandType::Int, 2, 2, "ii", false, BuiltinOp::CopyChara},
    {"ADDCOPYCHARA"        , OperandType::Int, 1, -1, "i", false, BuiltinOp::AddCopyChara},
    {"PICKUPCHARA"         , OperandType::Int, 1, -1, "i", false, BuiltinOp::PickupChara},
    {"STRLENFORM"          , OperandType::Int, 0, 1, "s", false, BuiltinOp::StrLenForm},
    {"STRLENFORMU"         , OperandType::Int, 0, 1, "s", false, BuiltinOp::StrLenFormU},
    {"PUTFORM"             , OperandType::Int, 0, 1, "s", false, BuiltinOp::PutForm},    // 存档摘要（SAVEDATA_TEXT）
    {"INITRAND"            , OperandType::Int, 0, 0, """", false, BuiltinOp::InitRand},
    {"DUMPRAND"            , OperandType::Int, 0, 0, """", false, BuiltinOp::DumpRand},
    {"DEBUGCLEAR"          , OperandType::Int, 0, 0, """", false, BuiltinOp::DebugClear},
};

inline constexpr std::size_t kBuiltinFunctionCount = std::size(kBuiltinFunctions);

// ---------------------------------------------------------------------------
// 「核心命令的实参个数放宽」（EM 私家版拡張：GCLEAR 2/6 参等）
//
// 原生表 kBuiltinFunctions 保持 constexpr 不变；扩展（EM/EE）经注册类
// （ExtensionRegistry::regCoreArgRange）注入本表 —— 只**放大**核心命令的
// 实参个数区间（对齐 C# `argumentTypeArrayEx` 给同一个方法补第二个形态），
// 不改写核心的返回类型/求值行为。查表时与原生声明合并（取并集）。
// ---------------------------------------------------------------------------
struct CoreArgWidenStore {
    std::unordered_map<std::string, std::pair<int, int>> ranges;   // 名字（大写）-> [min, max]
};

inline CoreArgWidenStore& coreArgWidenStore() {
    static CoreArgWidenStore store;
    return store;
}

// 注册（覆盖式；名字规范为大写，min/max 与 EE 的 argumentTypeArrayEx 同义）
inline void registerCoreArgWidenSpec(const std::string& upperName, int minArgs, int maxArgs) {
    coreArgWidenStore().ranges.insert_or_assign(upperName,
                                                std::pair<int, int>{minArgs, maxArgs});
}

// 便利重载（QString 名）。
inline void registerCoreArgWiden(const QString& upperName, int minArgs, int maxArgs) {
    registerCoreArgWidenSpec(upperName.toStdString(), minArgs, maxArgs);
}

[[nodiscard]] inline const std::pair<int, int>* findCoreArgWiden(std::string_view upperName) {
    const auto& store = coreArgWidenStore();
    const auto it = store.ranges.find(std::string(upperName));
    return it == store.ranges.end() ? nullptr : &it->second;
}

// 原生声明与「扩展放宽」合并：只放大，不缩小（核心的 2 参形态不受影响）。
inline void mergeCoreArgWiden(const std::string_view upperName, int& minArgs, int& maxArgs) {
    if (const auto* w = findCoreArgWiden(upperName)) {
        minArgs = qMin(minArgs, w->first);
        maxArgs = (w->second < 0) ? -1 : qMax(maxArgs, w->second);
    }
}


[[nodiscard]] constexpr const BuiltinFunctionSpec* findBuiltinFunction(std::string_view upperName) noexcept {
    for (const BuiltinFunctionSpec& s : kBuiltinFunctions) {
        if (s.name == upperName) return &s;
    }
    return nullptr;
}

// 运行期查表索引（CALLF/TRYCALLF 的 resolveFunctionCall **每次执行**都走、
// userCallReturnType 也在式中调用路径上）：kBuiltinFunctions 是 constexpr 静态数据，
// 建一次 QString→下标 哈希。旧实现是 189 项线性 strcmp + 调用方
// upper.toStdString() 的堆分配 —— CALLFORM 密集的游戏（eraTW 地图逐格
// TRYCALLFORM 一次上万次）每次全付。
// 解析期常量折叠不需要 constexpr 查表（findBuiltinFunction 保留线性版即可，
// 那是装载期一次性开销）。
[[nodiscard]] inline const QHash<QString, int>& builtinFunctionIndexMap() {
    static const QHash<QString, int> map = [] {
        QHash<QString, int> m;
        m.reserve(int(kBuiltinFunctionCount));
        for (std::size_t i = 0; i < kBuiltinFunctionCount; ++i)
            m.insert(QString::fromLatin1(kBuiltinFunctions[i].name), int(i));
        return m;
    }();
    return map;
}

[[nodiscard]] inline int builtinFunctionIndex(const QString& upperName) noexcept {
    return builtinFunctionIndexMap().value(upperName, -1);
}

// 由下标取回声明（FunctionNode 记下标，避免求值期做字符串查找）
[[nodiscard]] constexpr const BuiltinFunctionSpec& builtinFunctionAt(int index) noexcept {
    return kBuiltinFunctions[static_cast<std::size_t>(index)];
}

[[nodiscard]] constexpr bool isBuiltinFunction(std::string_view upperName) noexcept {
    return findBuiltinFunction(upperName) != nullptr;
}

// 内置函数返回类型；未登记返回 Unknown（对齐 C# methodDic 未命中）。
[[nodiscard]] constexpr OperandType builtinFunctionReturnType(std::string_view upperName) noexcept {
    if (const BuiltinFunctionSpec* s = findBuiltinFunction(upperName)) return s->ret;
    return OperandType::Unknown;
}

// 参数个数范围（-1 表示不限）；未登记返回 false。
[[nodiscard]] constexpr bool builtinFunctionArgRange(std::string_view upperName,
                                                     int& minArgs, int& maxArgs) noexcept {
    if (const BuiltinFunctionSpec* s = findBuiltinFunction(upperName)) {
        minArgs = s->minArgs;
        maxArgs = s->maxArgs;
        mergeCoreArgWiden(upperName, minArgs, maxArgs);   // 扩展放宽（EM 私家版拡張）
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 运行期「扩展式中函数」表
//
// **原生归原生、扩展归扩展**：原生函数表 kBuiltinFunctions 保持 constexpr 不变；
// 扩展（EE 等）在启动时经注册类（ExtensionRegistry::regExpr ->
// AstBuilder::registerExtensionFunction）注入本表。表中函数的求值 opcode 统一为
// BuiltinOp::Extension —— 具体求值由注册类持有的回调完成（见 ExpressionEvaluator
// 的扩展函数回调）。这样解析期能像内置函数一样拿到返回类型/参数个数，
// 但实现完全住在扩展侧（GameProc/ee_extension.cpp）。
// ---------------------------------------------------------------------------
struct ExtensionFunctionStore {
    std::deque<std::string> names;                             // 稳定存储（spec.name 指向它）
    std::unordered_map<std::string, BuiltinFunctionSpec> specs; // 名字（大写）-> 声明
};

inline ExtensionFunctionStore& extensionFunctionStore() {
    static ExtensionFunctionStore store;
    return store;
}

// 注册（重复注册按后者覆盖；名字规范为大写）
inline void registerExtensionFunctionSpec(const std::string& upperName, OperandType ret,
                                          int minArgs, int maxArgs) {
    ExtensionFunctionStore& store = extensionFunctionStore();
    store.names.push_back(upperName);
    BuiltinFunctionSpec spec{};
    spec.name = store.names.back();
    spec.ret = ret;
    spec.minArgs = static_cast<qint8>(minArgs);
    spec.maxArgs = static_cast<qint8>(maxArgs);
    spec.argPattern = std::string_view{};
    spec.canRestructure = false;
    spec.op = BuiltinOp::Extension;
    store.specs.insert_or_assign(upperName, spec);
}

[[nodiscard]] inline const BuiltinFunctionSpec* findExtensionFunction(std::string_view upperName) {
    const auto& store = extensionFunctionStore();
    const auto it = store.specs.find(std::string(upperName));
    return it == store.specs.end() ? nullptr : &it->second;
}

// 合并查表：原生优先，其次扩展（解析期知道「是个函数 + 返回类型 + 参数个数」）。
[[nodiscard]] inline const BuiltinFunctionSpec* findFunctionSpec(std::string_view upperName) {
    if (const BuiltinFunctionSpec* s = findBuiltinFunction(upperName)) return s;
    return findExtensionFunction(upperName);
}

#endif // AST_FUNCTION_TYPES_H
