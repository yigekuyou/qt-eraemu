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
// ---------------------------------------------------------------------------
// test_key_map.cpp
//
// 键码模型（GameView/key_map.h）回归。
//
// 设计：UI 层用 Qt 键码；C++ 在**编译期**把 Qt 键码对应到 Emuera 键码。
// 权威语义来自 C# —— 键盘输入最终落到 System.Windows.Forms.Keys 的数值：
//   * MainWindow.richTextBox1_KeyDown -> PressPrimitiveKey(e.KeyCode, e.KeyData)
//   * EmueraConsole.PressPrimitiveKey -> InputMouseKey(3, keycode, keydata, 0, 0)
//   * KeyData = KeyCode | 修饰位（Keys.Shift=0x10000 / Control=0x20000 /
//     Alt=0x40000）
// 「编译期对应」由 key_map.h 里的 static_assert 保证（写错一行即编译失败）；
// 本测试再从运行时入口复核一遍，并锁定 ConsoleBackend::submitQtKey 的端到端行为。
//
//   A. 内建表：特殊键的 Emuera 键码（对齐 C# Keys 数值）
//   B. 字母 / 主键盘数字（Qt 键码 == VK，区间直通）
//   C. 标点（WinForms 的 OEM 码，与 Qt 键码不同）
//   D. 修饰位（Shift/Control/Alt -> 0x10000/0x20000/0x40000）
//   E. 小键盘（KeypadModifier -> VK_NUMPAD0..9 等独立键码）
//   F. 未收录键透传（与旧 QML「special 未命中就用 e.key」一致）
//   G. ConsoleBackend::submitQtKey 端到端（INPUTMOUSEKEY 等待时发出键码）
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>

#include "key_map.h"
#include "console_backend.h"

static int g_failures = 0;

static void check(bool cond, const QString& what) {
    if (cond) qDebug().noquote() << "  [ok ]" << what;
    else { qDebug().noquote() << "  [FAIL]" << what; ++g_failures; }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ---- A. 内建表：特殊键 ----
    qDebug() << "\nA) 内建表：特殊键 -> Emuera 键码";
    {
        struct Case { int qt; int emu; };
        const Case cases[] = {
            { Qt::Key_Backspace,  8   },
            { Qt::Key_Tab,        9   },
            { Qt::Key_Return,     13  },
            { Qt::Key_Enter,      13  },
            { Qt::Key_Escape,     27  },
            { Qt::Key_Space,      32  },
            { Qt::Key_PageUp,     33  },
            { Qt::Key_PageDown,   34  },
            { Qt::Key_End,        35  },
            { Qt::Key_Home,       36  },
            { Qt::Key_Left,       37  },
            { Qt::Key_Up,         38  },
            { Qt::Key_Right,      39  },
            { Qt::Key_Down,       40  },
            { Qt::Key_Insert,     45  },
            { Qt::Key_Delete,     46  },
            { Qt::Key_F1,         112 },
            { Qt::Key_F12,        123 },
            { Qt::Key_F24,        135 },
        };
        bool all = true;
        for (const Case& c : cases)
            all = all && KeyMap::emuKeyCode(c.qt) == c.emu;
        check(all, "特殊键 8/9/13/27/32/33..40/45/46/112..135 全部对齐 C# Keys");
    }

    // ---- B. 字母 / 数字 ----
    qDebug() << "\nB) 字母 / 主键盘数字（Qt 键码 == VK）";
    check(KeyMap::emuKeyCode(Qt::Key_A) == 65 && KeyMap::emuKeyCode(Qt::Key_Z) == 90,
          "A..Z -> 65..90");
    check(KeyMap::emuKeyCode(Qt::Key_0) == 48 && KeyMap::emuKeyCode(Qt::Key_9) == 57,
          "0..9 -> 48..57");

    // ---- C. 标点（OEM 码）----
    qDebug() << "\nC) 标点（WinForms OEM 码）";
    check(KeyMap::emuKeyCode(Qt::Key_Semicolon)  == 186, "Semicolon -> 186 (VK_OEM_1)");
    check(KeyMap::emuKeyCode(Qt::Key_Equal)      == 187, "Equal -> 187 (VK_OEM_PLUS)");
    check(KeyMap::emuKeyCode(Qt::Key_Comma)      == 188, "Comma -> 188 (VK_OEM_COMMA)");
    check(KeyMap::emuKeyCode(Qt::Key_Minus)      == 189, "Minus -> 189 (VK_OEM_MINUS)");
    check(KeyMap::emuKeyCode(Qt::Key_Period)     == 190, "Period -> 190 (VK_OEM_PERIOD)");
    check(KeyMap::emuKeyCode(Qt::Key_Slash)      == 191, "Slash -> 191 (VK_OEM_2)");
    check(KeyMap::emuKeyCode(Qt::Key_QuoteLeft)  == 192, "QuoteLeft -> 192 (VK_OEM_3)");
    check(KeyMap::emuKeyCode(Qt::Key_BracketLeft)== 219, "BracketLeft -> 219 (VK_OEM_4)");
    check(KeyMap::emuKeyCode(Qt::Key_Backslash)  == 220, "Backslash -> 220 (VK_OEM_5)");
    check(KeyMap::emuKeyCode(Qt::Key_BracketRight)==221, "BracketRight -> 221 (VK_OEM_6)");
    check(KeyMap::emuKeyCode(Qt::Key_Apostrophe) == 222, "Apostrophe -> 222 (VK_OEM_7)");

    // ---- D. 修饰位 ----
    qDebug() << "\nD) 修饰位对齐 C# Keys";
    {
        auto mk = [](int qt, int mods) { return KeyMap::toMouseKey(qt, mods); };
        check(mk(Qt::Key_A, Qt::ShiftModifier).keyData   == (65 | 0x10000),
              "Shift+A -> keycode 65 / keydata 65|0x10000");
        check(mk(Qt::Key_A, Qt::ControlModifier).keyData == (65 | 0x20000),
              "Ctrl+A -> keycode 65 / keydata 65|0x20000");
        check(mk(Qt::Key_A, Qt::AltModifier).keyData     == (65 | 0x40000),
              "Alt+A -> keycode 65 / keydata 65|0x40000");
        check(mk(Qt::Key_A, Qt::ShiftModifier | Qt::ControlModifier).keyData
                  == (65 | 0x10000 | 0x20000),
              "Shift+Ctrl+A -> 65|0x10000|0x20000");
        check(mk(Qt::Key_Return, Qt::NoModifier).keyCode == 13
                  && mk(Qt::Key_Return, Qt::NoModifier).keyData == 13,
              "Return -> (13, 13)");
        // keycode 恒为「去掉修饰的键」，keydata 才带修饰
        check(mk(Qt::Key_Left, Qt::ShiftModifier).keyCode == 37,
              "Shift+Left 的 keycode 仍是 37（对齐 C# e.KeyCode）");
    }

    // ---- E. 小键盘 ----
    qDebug() << "\nE) 小键盘（KeypadModifier -> 独立 VK）";
    {
        auto mk = [](int qt, int mods) { return KeyMap::toMouseKey(qt, mods); };
        check(mk(Qt::Key_5, Qt::KeypadModifier).keyCode == 101,
              "小键盘 5 -> 101 (VK_NUMPAD5)");
        check(mk(Qt::Key_5, Qt::KeypadModifier).keyData == 101,
              "小键盘 5 不叠加 KeypadModifier（WinForms 无此修饰位）");
        check(mk(Qt::Key_0, Qt::KeypadModifier).keyCode == 96,
              "小键盘 0 -> 96 (VK_NUMPAD0)");
        check(mk(Qt::Key_Plus, Qt::KeypadModifier).keyCode == 107,
              "小键盘 + -> 107 (VK_ADD)");
        check(mk(Qt::Key_Slash, Qt::KeypadModifier).keyCode == 111,
              "小键盘 / -> 111 (VK_DIVIDE)");
        check(mk(Qt::Key_Period, Qt::KeypadModifier).keyCode == 110,
              "小键盘 . -> 110 (VK_DECIMAL)");
        // 主键盘 '+'（Shift+=）走 OemPlus
        check(mk(Qt::Key_Plus, Qt::ShiftModifier).keyCode == 187,
              "主键盘 + (Shift+=) -> 187 (VK_OEM_PLUS)");
    }

    // ---- F. 未收录键透传 ----
    qDebug() << "\nF) 未收录键透传（旧 QML 行为）";
    check(KeyMap::emuKeyCode(Qt::Key_F35) == -1, "F35 不在表内 -> -1");
    check(KeyMap::toMouseKey(Qt::Key_F35, Qt::NoModifier).keyCode == Qt::Key_F35,
          "未收录键透传 Qt 键码");

    // ---- G. ConsoleBackend::submitQtKey 端到端 ----
    qDebug() << "\nG) ConsoleBackend::submitQtKey（INPUTMOUSEKEY 等待）";
    {
        ConsoleBackend console;
        int gotType = -1, gotR1 = -1, gotR2 = -1, gotR3 = -1, gotR4 = -1;
        QObject::connect(&console, &ConsoleBackend::mouseKeySubmitted,
                         [&](int t, int r1, int r2, int r3, int r4) {
                             gotType = t; gotR1 = r1; gotR2 = r2; gotR3 = r3; gotR4 = r4;
                         });

        console.notifyInputRequested(QStringLiteral("INPUTMOUSEKEY"));
        console.submitQtKey(Qt::Key_Return, Qt::NoModifier);
        check(gotType == 3 && gotR1 == 13 && gotR2 == 13 && gotR3 == 0 && gotR4 == 0,
              "submitQtKey(Return) -> mouseKeySubmitted(3, 13, 13, 0, 0)");

        console.notifyInputRequested(QStringLiteral("INPUTMOUSEKEY"));
        console.submitQtKey(Qt::Key_A, Qt::ShiftModifier);
        check(gotType == 3 && gotR1 == 65 && gotR2 == (65 | 0x10000),
              "submitQtKey(Shift+A) -> (3, 65, 65|0x10000)");

        // 非 INPUTMOUSEKEY 等待时拒绝提交（对齐 submitMouseKey 的准入）
        console.notifyInputRequested(QStringLiteral("INPUT"));
        gotType = -1;
        console.submitQtKey(Qt::Key_Return, Qt::NoModifier);
        check(gotType == -1, "非 INPUTMOUSEKEY 等待时 submitQtKey 被拒（无信号）");
    }

    qDebug() << "\n========================";
    if (g_failures == 0) {
        qDebug() << "[SUCCESS] key map tests passed";
        return 0;
    }
    qDebug() << "[FAILURE]" << g_failures << "check(s) failed";
    return 1;
}
