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
    {"VARSIZE"             , OperandType::Int, 1, 2, "si", true , BuiltinOp::VarSize},
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
    {"SAVETEXT"            , OperandType::Int, 4, 4, "siii", false, BuiltinOp::SaveText},
    {"LOADTEXT"            , OperandType::Str, 3, 3, "iii", false, BuiltinOp::LoadText},
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
    {"GSETFONT"            , OperandType::Int, 3, 3, "isi", false, BuiltinOp::GSetFont},
    {"GSETPEN"             , OperandType::Int, 2, 2, "ii", false, BuiltinOp::GSetPen},
    // SPRITECREATE(name, gID[, x, y, w, h])：把 G 图像登记成具名精灵
    {"SPRITECREATE"        , OperandType::Int, 2, 6, "siiiii", false, BuiltinOp::SpriteCreate},
    {"SPRITEDISPOSE"       , OperandType::Int, 1, 1, "s", false, BuiltinOp::SpriteDispose},
    {"CBGSETG"             , OperandType::Int, 4, 4, "iiii", false, BuiltinOp::CbgSetG},
    {"CBGSETSPRITE"        , OperandType::Int, 4, 4, "siii", false, BuiltinOp::CbgSetSprite},
    {"CBGCLEAR"            , OperandType::Int, 0, 0, """", false, BuiltinOp::CbgClear},
    {"CBGCLEARBUTTON"      , OperandType::Int, 0, 0, """", false, BuiltinOp::CbgClearButton},
    {"CBGREMOVERANGE"      , OperandType::Int, 2, 2, "ii", false, BuiltinOp::CbgRemoveRange},
    {"CBGREMOVEBMAP"       , OperandType::Int, 0, 0, """", false, BuiltinOp::CbgRemoveBmap},
    {"CBGSETBMAPG"         , OperandType::Int, 1, 1, "i", false, BuiltinOp::CbgSetBmapG},
    {"CBGSETBUTTONSPRITE"  , OperandType::Int, 7, 7, "issiiis", false, BuiltinOp::CbgSetButtonSprite},
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
