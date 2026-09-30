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
#include <string_view>
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
    Convert, IsNumeric, Escape, EncodeToUni, CharAtU, StrForm,
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
    {"GETCHARA"            , OperandType::Int, 1, 2, "ii", true , BuiltinOp::None},
    {"GETSPCHARA"          , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
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
    {"FINDCHARA"           , OperandType::Int, 2, 4, "vaii", true , BuiltinOp::None},
    {"FINDLASTCHARA"       , OperandType::Int, 2, 4, "vaii", true , BuiltinOp::None},
    {"EXISTCSV"            , OperandType::Int, 1, 2, "ii", true , BuiltinOp::ExistCsv},
    {"VARSIZE"             , OperandType::Int, 1, 2, "si", true , BuiltinOp::VarSize},
    {"CHKFONT"             , OperandType::Int, 1, 1, "s", true , BuiltinOp::None},
    {"CHKDATA"             , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"ISSKIP"              , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"MOUSESKIP"           , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"MESSKIP"             , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"GETCOLOR"            , OperandType::Int, 0, 0, """", true , BuiltinOp::GetColor},
    {"GETDEFCOLOR"         , OperandType::Int, 0, 0, """", true , BuiltinOp::GetDefColor},
    {"GETFOCUSCOLOR"       , OperandType::Int, 0, 0, """", true , BuiltinOp::None},
    {"GETBGCOLOR"          , OperandType::Int, 0, 0, """", true , BuiltinOp::GetBgColor},
    {"GETDEFBGCOLOR"       , OperandType::Int, 0, 0, """", true , BuiltinOp::GetDefBgColor},
    {"GETSTYLE"            , OperandType::Int, 0, 0, """", false, BuiltinOp::GetStyle},
    {"GETFONT"             , OperandType::Str, 0, 0, """", false, BuiltinOp::None},
    {"BARSTR"              , OperandType::Str, 3, 3, "iii", true , BuiltinOp::BarStr},
    {"CURRENTALIGN"        , OperandType::Str, 0, 0, """", false, BuiltinOp::None},
    {"CURRENTREDRAW"       , OperandType::Int, 0, 0, """", false, BuiltinOp::CurrentRedraw},
    {"COLOR_FROMNAME"      , OperandType::Int, 1, 1, "s", true , BuiltinOp::None},
    {"COLOR_FROMRGB"       , OperandType::Int, 3, 3, "iii", true , BuiltinOp::None},
    {"CHKCHARADATA"        , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"FIND_CHARADATA"      , OperandType::Int, 0, 1, "s", false, BuiltinOp::None},
    {"MONEYSTR"            , OperandType::Str, 1, 2, "is", true , BuiltinOp::MoneyStr},
    {"PRINTCPERLINE"       , OperandType::Int, 0, 0, """", true , BuiltinOp::PrintCPerLine},
    {"PRINTCLENGTH"        , OperandType::Int, 0, 0, """", true , BuiltinOp::PrintCLength},
    {"SAVENOS"             , OperandType::Int, 0, 0, """", true , BuiltinOp::None},
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
    {"GETPALAMLV"          , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"GETEXPLV"            , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"FINDELEMENT"         , OperandType::Int, 2, 5, "vaiii", true , BuiltinOp::FindElement},
    {"FINDLASTELEMENT"     , OperandType::Int, 2, 5, "vaiii", true , BuiltinOp::FindLastElement},
    {"INRANGE"             , OperandType::Int, 3, 3, "iii", false, BuiltinOp::InRange},
    {"INRANGEARRAY"        , OperandType::Int, 3, 6, "niii", false, BuiltinOp::InRangeArray},
    {"INRANGECARRAY"       , OperandType::Int, 3, 6, "viii", false, BuiltinOp::InRangeCharaArray},
    {"GETNUMB"             , OperandType::Int, 2, 2, "vs", false, BuiltinOp::GetNum},
    {"ARRAYMSORT"          , OperandType::Int, 2, -1, "v", false, BuiltinOp::None},
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
    {"GETLINESTR"          , OperandType::Str, 1, 1, "s", false, BuiltinOp::None},
    {"STRFORM"             , OperandType::Str, 1, 1, "s", true , BuiltinOp::StrForm},
    {"STRJOIN"             , OperandType::Str, 1, 4, "vaii", true , BuiltinOp::StrJoin},
    {"GETCONFIG"           , OperandType::Int, 1, 1, "s", true , BuiltinOp::GetConfig},
    {"GETCONFIGS"          , OperandType::Str, 1, 1, "s", true , BuiltinOp::GetConfigs},
    {"HTML_GETPRINTEDSTR"  , OperandType::Str, 0, 1, "i", false, BuiltinOp::None},
    {"HTML_POPPRINTINGSTR" , OperandType::Str, 0, 0, """", false, BuiltinOp::None},
    {"HTML_TOPLAINTEXT"    , OperandType::Str, 1, 1, "s", true , BuiltinOp::None},
    {"HTML_ESCAPE"         , OperandType::Str, 1, 1, "s", true , BuiltinOp::None},
    {"SPRITECREATED"       , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"SPRITEWIDTH"         , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"SPRITEHEIGHT"        , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"SPRITEMOVE"          , OperandType::Int, 3, 3, "sii", false, BuiltinOp::None},
    {"SPRITESETPOS"        , OperandType::Int, 3, 3, "sii", false, BuiltinOp::None},
    {"SPRITEPOSX"          , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"SPRITEPOSY"          , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"CLIENTWIDTH"         , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"CLIENTHEIGHT"        , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"GETKEY"              , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"GETKEYTRIGGERED"     , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"MOUSEX"              , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"MOUSEY"              , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"ISACTIVE"            , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"SAVETEXT"            , OperandType::Int, 4, 4, "siii", false, BuiltinOp::None},
    {"LOADTEXT"            , OperandType::Str, 3, 3, "iii", false, BuiltinOp::None},
    {"GCREATED"            , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"GWIDTH"              , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"GHEIGHT"             , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"GGETCOLOR"           , OperandType::Int, 3, 3, "iii", false, BuiltinOp::None},
    {"SPRITEGETCOLOR"      , OperandType::Int, 3, 3, "sii", false, BuiltinOp::None},
    {"GCREATE"             , OperandType::Int, 3, 3, "iii", false, BuiltinOp::None},
    {"GCREATEFROMFILE"     , OperandType::Int, 2, 2, "is", false, BuiltinOp::None},
    {"GDISPOSE"            , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"GCLEAR"              , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"GFILLRECTANGLE"      , OperandType::Int, 5, 5, "iiiii", false, BuiltinOp::None},
    {"GDRAWSPRITE"         , OperandType::Int, 6, 6, "isiiii", false, BuiltinOp::None},
    {"GSETCOLOR"           , OperandType::Int, 4, 4, "iiii", false, BuiltinOp::None},
    {"GDRAWG"              , OperandType::Int, 10, 11, "iiiiiiiiiia", false, BuiltinOp::None},
    {"GDRAWGWITHMASK"      , OperandType::Int, 5, 5, "iiiii", false, BuiltinOp::None},
    {"GSETBRUSH"           , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"GSETFONT"            , OperandType::Int, 3, 3, "isi", false, BuiltinOp::None},
    {"GSETPEN"             , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"SPRITECREATE"        , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"SPRITEDISPOSE"       , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"CBGSETG"             , OperandType::Int, 4, 4, "iiii", false, BuiltinOp::None},
    {"CBGSETSPRITE"        , OperandType::Int, 4, 4, "siii", false, BuiltinOp::None},
    {"CBGCLEAR"            , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"CBGCLEARBUTTON"      , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"CBGREMOVERANGE"      , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"CBGREMOVEBMAP"       , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"CBGSETBMAPG"         , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},
    {"CBGSETBUTTONSPRITE"  , OperandType::Int, 7, 7, "issiiis", false, BuiltinOp::None},
    {"GSAVE"               , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"GLOAD"               , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"SPRITEANIMECREATE"   , OperandType::Int, 3, 3, "sii", false, BuiltinOp::None},
    {"SPRITEANIMEADDFRAME" , OperandType::Int, 9, 9, "siiiiiiii", false, BuiltinOp::None},
    {"SETANIMETIMER"       , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},

    // ------------------------------------------------------------------
    // 语句形式的内部函数（对齐 C# FunctionIdentifier.funcDic 里
    // 「只有 ArgumentBuilder、没有 Instruction」的一类：它们既能作式中函数，
    // 也能单独成行当**函数语句**用，返回值写 RESULT / RESULTS:0）。
    // eraTW 里实际出现但此前完全没登记的名字都补在这里。
    // ------------------------------------------------------------------
    {"TWAIT"               , OperandType::Int, 1, 2, "ii", false, BuiltinOp::None},   // TWAIT <ms>[, <skip>]
    {"RESETBGCOLOR"        , OperandType::Int, 0, 0, """", false, BuiltinOp::None},   // 背景色复位
    {"RESET_STAIN"         , OperandType::Int, 1, 1, "i", false, BuiltinOp::None},    // 污渍清零
    {"SAVECHARA"           , OperandType::Int, 2, -1, "ssi", false, BuiltinOp::None}, // <文件>,<摘要>,<角色番号>…
    {"LOADCHARA"           , OperandType::Int, 1, 1, "s", false, BuiltinOp::None},
    {"SWAPCHARA"           , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"COPYCHARA"           , OperandType::Int, 2, 2, "ii", false, BuiltinOp::None},
    {"ADDCOPYCHARA"        , OperandType::Int, 1, -1, "i", false, BuiltinOp::None},
    {"PICKUPCHARA"         , OperandType::Int, 1, -1, "i", false, BuiltinOp::None},
    {"STRLENFORM"          , OperandType::Int, 0, 1, "s", false, BuiltinOp::StrLenForm},
    {"STRLENFORMU"         , OperandType::Int, 0, 1, "s", false, BuiltinOp::StrLenFormU},
    {"PUTFORM"             , OperandType::Int, 0, 1, "s", false, BuiltinOp::None},    // 存档摘要（SAVEDATA_TEXT）
    {"INITRAND"            , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"DUMPRAND"            , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
    {"DEBUGCLEAR"          , OperandType::Int, 0, 0, """", false, BuiltinOp::None},
};

inline constexpr std::size_t kBuiltinFunctionCount = std::size(kBuiltinFunctions);

[[nodiscard]] constexpr const BuiltinFunctionSpec* findBuiltinFunction(std::string_view upperName) noexcept {
    for (const BuiltinFunctionSpec& s : kBuiltinFunctions) {
        if (s.name == upperName) return &s;
    }
    return nullptr;
}

[[nodiscard]] constexpr int builtinFunctionIndex(std::string_view upperName) noexcept {
    for (std::size_t i = 0; i < kBuiltinFunctionCount; ++i) {
        if (kBuiltinFunctions[i].name == upperName) return static_cast<int>(i);
    }
    return -1;
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
        return true;
    }
    return false;
}

#endif // AST_FUNCTION_TYPES_H
