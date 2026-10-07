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
#ifndef KEY_MAP_H
#define KEY_MAP_H

#include <QtCore/qnamespace.h>

// ---------------------------------------------------------------------------
// 键码模型（key map）—— UI 层用 Qt 键码，C++ 在**编译期**把对应关系定死
//
// 分工：
//   * UI 层（QML KeyEvent）只上报 **Qt 键码** 与 **Qt 修饰符**；
//   * 本表在**编译期**把 Qt 键码对应到 Emuera 键码（Win32 VK / C# Keys），
//     运行时只是一次查表，没有任何「解析/构建」步骤。
//
// 为什么要对应：ERB 脚本是数据，脚本里直接写着 WinForms 的数字键码
// （见 eraMegaten MESSAGE_WINDOW.ERB 的 `RESULT:1 == 0x100000`、GETKEY(0x12)），
// 这些数字必须保持原样 —— 所以引擎内部用 Qt 键码，只在交给 ERB 的边界上
// 落回 WinForms 数值。
//
// 参考：Qt 文档 KeyEvent（QML）/ Qt::Key；Win32 虚拟键码（MSDN GetKeyState）
// 与 C# System.Windows.Forms.Keys / MouseButtons。
// ---------------------------------------------------------------------------
namespace KeyMap {

// WinForms Keys 的修饰位（对齐 C#：Keys.Shift / Control / Alt）。
inline constexpr int EmuShift   = 0x10000;
inline constexpr int EmuControl = 0x20000;
inline constexpr int EmuAlt     = 0x40000;

// 一条对应：Qt 键码 -> Emuera 键码。
struct Entry {
    int qtKey;
    int emuKey;
};

// 编译期表。字母 A..Z（65..90）与主键盘数字 0..9（48..57）的 Qt 键码
// **恰好**等于 Win32 VK，不逐个列出（见 emuKeyCode 的区间分支）；
// 其余按键（编辑键 / F 键 / 标点 OEM / 锁定键 / 小键盘兜底）两边不相等，必须显式列。
inline constexpr Entry kEntries[] = {
    // ---- 编辑 / 导航 ----
    { Qt::Key_Backspace,   8   },
    { Qt::Key_Tab,         9   },
    { Qt::Key_Backtab,     9   },   // Shift+Tab（WinForms 同为 Tab）
    { Qt::Key_Return,      13  },
    { Qt::Key_Enter,       13  },   // 小键盘回车
    { Qt::Key_Escape,      27  },
    { Qt::Key_Space,       32  },
    { Qt::Key_PageUp,      33  },
    { Qt::Key_PageDown,    34  },
    { Qt::Key_End,         35  },
    { Qt::Key_Home,        36  },
    { Qt::Key_Left,        37  },
    { Qt::Key_Up,          38  },
    { Qt::Key_Right,       39  },
    { Qt::Key_Down,        40  },
    { Qt::Key_Insert,      45  },
    { Qt::Key_Delete,      46  },

    // ---- 锁定 / 系统键 ----
    { Qt::Key_CapsLock,    20  },
    { Qt::Key_NumLock,     144 },
    { Qt::Key_ScrollLock,  145 },
    { Qt::Key_Print,       44  },   // VK_SNAPSHOT
    { Qt::Key_SysReq,      44  },
    { Qt::Key_Pause,       19  },
    { Qt::Key_Menu,        93  },   // 应用键（VK_APPS）

    // ---- 修饰键本身被按下（WinForms 会给独立键码）----
    { Qt::Key_Shift,       16  },
    { Qt::Key_Control,     17  },
    { Qt::Key_Alt,         18  },   // WinForms Keys.Menu
    { Qt::Key_Meta,        91  },   // WinForms Keys.LWin

    // ---- 标点（WinForms 用 OEM 码，与 Qt 键码不同）----
    // 主键盘与小键盘共用同一批 Qt 键码，靠 KeypadModifier 区分：
    // 这里只列**主键盘**，小键盘在 toMouseKey() 的 keypad 分支处理。
    { Qt::Key_QuoteLeft,   192 },   // ` ~
    { Qt::Key_Minus,       189 },
    { Qt::Key_Underscore,  189 },   // Shift+-（同主键盘减号位）
    { Qt::Key_Equal,       187 },   // VK_OEM_PLUS
    { Qt::Key_Plus,        187 },   // Shift+=（同主键盘等号位）
    { Qt::Key_BracketLeft, 219 },
    { Qt::Key_BracketRight,221 },
    { Qt::Key_Backslash,   220 },
    { Qt::Key_Semicolon,   186 },   // VK_OEM_1
    { Qt::Key_Colon,       186 },   // Shift+;
    { Qt::Key_Apostrophe,  222 },   // VK_OEM_7
    { Qt::Key_QuoteDbl,    222 },   // Shift+'
    { Qt::Key_Comma,       188 },
    { Qt::Key_Period,      190 },
    { Qt::Key_Slash,       191 },   // VK_OEM_2
    { Qt::Key_Question,    191 },   // Shift+/

    // ---- F1..F24（WinForms：112..135）----
    { Qt::Key_F1,  112 }, { Qt::Key_F2,  113 }, { Qt::Key_F3,  114 },
    { Qt::Key_F4,  115 }, { Qt::Key_F5,  116 }, { Qt::Key_F6,  117 },
    { Qt::Key_F7,  118 }, { Qt::Key_F8,  119 }, { Qt::Key_F9,  120 },
    { Qt::Key_F10, 121 }, { Qt::Key_F11, 122 }, { Qt::Key_F12, 123 },
    { Qt::Key_F13, 124 }, { Qt::Key_F14, 125 }, { Qt::Key_F15, 126 },
    { Qt::Key_F16, 127 }, { Qt::Key_F17, 128 }, { Qt::Key_F18, 129 },
    { Qt::Key_F19, 130 }, { Qt::Key_F20, 131 }, { Qt::Key_F21, 132 },
    { Qt::Key_F22, 133 }, { Qt::Key_F23, 134 }, { Qt::Key_F24, 135 },
};

// Qt 键码 -> Emuera 键码；未收录返回 -1。constexpr：可在编译期求值。
inline constexpr int emuKeyCode(int qtKey)
{
    // 字母 / 主键盘数字：Qt 键码与 Win32 VK 相同，区间直通。
    if ((qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        || (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9))
        return qtKey;
    for (const Entry& e : kEntries) {
        if (e.qtKey == qtKey)
            return e.emuKey;
    }
    return -1;
}

// 一次「鼠标/键盘」输入的两个号（对齐 C# PressPrimitiveKey 交付的
// keycode / keydata）。constexpr：可在编译期求值。
//
//   keyCode = Emuera 键码（未收录时**透传 Qt 键码**，与旧 QML 行为一致）
//   keyData = keyCode | 修饰位（Shift/Control/Alt；Keypad 在 WinForms 里
//             不是「修饰」而是独立键码，故不叠加）
struct MouseKey {
    int keyCode;
    int keyData;
};

inline constexpr MouseKey toMouseKey(int qtKey, int qtModifiers)
{
    int code;
    if (qtModifiers & Qt::KeypadModifier) {
        // 小键盘在 WinForms 里是独立键码（VK_NUMPAD0..9 / VK_MULTIPLY…）；
        // Qt 用 KeypadModifier 区分，故这里单独特判。
        if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
            code = 96 + (qtKey - Qt::Key_0);      // VK_NUMPAD0..9
        else if (qtKey == Qt::Key_Period || qtKey == Qt::Key_Comma)
            code = 110;                           // VK_DECIMAL
        else if (qtKey == Qt::Key_Plus)
            code = 107;                           // VK_ADD
        else if (qtKey == Qt::Key_Minus)
            code = 109;                           // VK_SUBTRACT
        else if (qtKey == Qt::Key_Asterisk)
            code = 106;                           // VK_MULTIPLY
        else if (qtKey == Qt::Key_Slash)
            code = 111;                           // VK_DIVIDE
        else
            code = emuKeyCode(qtKey);
    } else {
        code = emuKeyCode(qtKey);
    }
    if (code < 0)
        code = qtKey;    // 未收录：透传 Qt 键码

    int data = code;
    if (qtModifiers & Qt::ShiftModifier)   data |= EmuShift;
    if (qtModifiers & Qt::ControlModifier) data |= EmuControl;
    if (qtModifiers & Qt::AltModifier)     data |= EmuAlt;

    return MouseKey{ code, data };
}

// ---------------------------------------------------------------------------
// 编译期自检：表的对应关系在**编译期**就成立（写错一行即编译失败）。
// 这是「cpp 在编译期对应上」的落点 —— 不是运行时构建，而是编译期断言。
// ---------------------------------------------------------------------------
static_assert(emuKeyCode(Qt::Key_Return)  == 13,  "Return 应为 13");
static_assert(emuKeyCode(Qt::Key_Enter)   == 13,  "Enter 应为 13");
static_assert(emuKeyCode(Qt::Key_Escape)  == 27,  "Escape 应为 27");
static_assert(emuKeyCode(Qt::Key_Backspace) == 8, "Backspace 应为 8");
static_assert(emuKeyCode(Qt::Key_Tab)     == 9,   "Tab 应为 9");
static_assert(emuKeyCode(Qt::Key_Space)   == 32,  "Space 应为 32");
static_assert(emuKeyCode(Qt::Key_Left)    == 37,  "Left 应为 37");
static_assert(emuKeyCode(Qt::Key_Up)      == 38,  "Up 应为 38");
static_assert(emuKeyCode(Qt::Key_Right)   == 39,  "Right 应为 39");
static_assert(emuKeyCode(Qt::Key_Down)    == 40,  "Down 应为 40");
static_assert(emuKeyCode(Qt::Key_Insert)  == 45,  "Insert 应为 45");
static_assert(emuKeyCode(Qt::Key_Delete)  == 46,  "Delete 应为 46");
static_assert(emuKeyCode(Qt::Key_F1)      == 112, "F1 应为 112");
static_assert(emuKeyCode(Qt::Key_F24)     == 135, "F24 应为 135");
static_assert(emuKeyCode(Qt::Key_A)       == 65,  "A 应为 65（Qt 键码 == VK）");
static_assert(emuKeyCode(Qt::Key_0)       == 48,  "0 应为 48（Qt 键码 == VK）");
static_assert(emuKeyCode(Qt::Key_Semicolon) == 186, "Semicolon 应为 186（VK_OEM_1）");
static_assert(emuKeyCode(Qt::Key_F35)     == -1,  "未收录键应为 -1");

static_assert(toMouseKey(Qt::Key_A, Qt::ShiftModifier).keyData
                  == (65 | EmuShift), "Shift+A 的 keydata 应带 0x10000");
static_assert(toMouseKey(Qt::Key_A, Qt::ControlModifier).keyData
                  == (65 | EmuControl), "Ctrl+A 的 keydata 应带 0x20000");
static_assert(toMouseKey(Qt::Key_A, Qt::AltModifier).keyData
                  == (65 | EmuAlt), "Alt+A 的 keydata 应带 0x40000");
static_assert(toMouseKey(Qt::Key_Left, Qt::ShiftModifier).keyCode == 37,
              "带修饰时 keycode 仍是去掉修饰的键（对齐 C# e.KeyCode）");
static_assert(toMouseKey(Qt::Key_5, Qt::KeypadModifier).keyCode == 101,
              "小键盘 5 应为 101（VK_NUMPAD5）");
static_assert(toMouseKey(Qt::Key_5, Qt::KeypadModifier).keyData == 101,
              "小键盘不叠加 KeypadModifier（WinForms 无此修饰位）");

} // namespace KeyMap

#endif // KEY_MAP_H
