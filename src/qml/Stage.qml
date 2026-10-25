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
import QtQuick

// ---------------------------------------------------------------------------
// Stage —— 「逻辑舞台 + 等比缩放呈现」
//
// 舞台尺寸是 **C++ 的事实**（游戏配置 emuera.config 的 ウィンドウ幅/高さ，
// 经 WindowController.stageWidth/stageHeight 暴露）。C++ 用它推导网格
// （列 = 舞台宽 ÷ 半角字宽、行 = 舞台高 ÷ 行高）、maxLineUnits 与
// GETCLIENTSIZE —— 也就是说「脚本看到的画布」只由舞台决定，与窗口无关。
//
// 本组件是呈现层：把整块舞台用**一个** Item.scale 等比缩放到父项（窗口的
// 游戏视口），contain 语义（scale = min(可用宽/舞台宽, 可用高/舞台高)），
// 居中，不足的一边留黑边（父项的背景色 = 游戏背景色，见 EraRender）。
//
//   * 舞台可以**大于**窗口（eraTW 1400x750 跑在 475x400 的 Android 窗口里）
//     → scale < 1，整块画布缩小显示，内容不重排、不丢行、不削字；
//   * 舞台也可以**小于**窗口（桌面放大窗口 / 平板）→ scale > 1，整块放大。
//     这里**不设上限**：contain 本来就不会裁内容，加上限只会在「窗口远大于
//     舞台」时多出无法利用的黑边；Qt Quick 的文字与图片随场景图矩阵一起缩放
//     （不是先光栅再位图放大），放大后字形仍然锐利。
//   * 文字不重叠由「一个统一变换」自然保证：格宽/行距/字号全部是舞台逻辑
//     像素，缩放只是乘同一个系数，不存在 per-cell 补偿。
//
// 输入事件：Qt 文档（Item::scale）——变换级联到子项，鼠标/触摸坐标按逆变换
// 映射回舞台本地坐标，所以舞台缩小显示时按钮命中仍然正确。
//
// 用法（Main.qml）：
//   Stage { stageWidth: windowController.stageWidth
//           stageHeight: windowController.stageHeight
//           EraRender { anchors.fill: parent } }
// 位置/缩放全部由本组件从 parent 推导；子项只需 anchors.fill: parent。
// ---------------------------------------------------------------------------
Item {
    id: stage

    // 逻辑舞台尺寸（唯一来源：C++ WindowController 的配置值）
    property real stageWidth: 760
    property real stageHeight: 640

    // 舞台的逻辑尺寸就是它自己的几何 —— 子项按逻辑像素布局
    width: Math.max(1, stageWidth)
    height: Math.max(1, stageHeight)

    // 可用区域 = 父项（窗口的游戏视口）尺寸
    readonly property real hostWidth: parent ? parent.width : width
    readonly property real hostHeight: parent ? parent.height : height

    // ---- 呈现：一次等比缩放（contain）----
    // 舞台永远完整可见：scale 取两个方向里较小的那个。
    readonly property real stageScale: Math.max(0, Math.min(
        hostWidth / width, hostHeight / height))

    // 缩放以中心为原点（Item.Center），再按「未缩放的舞台」居中到父项：
    // 缩放后包围盒的中心正好落在父项中心，多出来的空间就是左右/上下黑边。
    transformOrigin: Item.Center
    scale: stageScale
    x: (hostWidth - width) / 2
    y: (hostHeight - height) / 2

    // 黑边尺寸（诊断/验收用）：contain 时至少有一边为 0
    readonly property real letterboxX: Math.max(0, (hostWidth - width * stageScale) / 2)
    readonly property real letterboxY: Math.max(0, (hostHeight - height * stageScale) / 2)

    // 裁掉缩放后落在父项之外的部分（理论上 contain 不会有，防御性保留）：
    // 舞台自身不裁（子项可能有意溢出到舞台边缘外，例如 ypos 的立絵），
    // 由父项（gameViewport）的 clip 负责，见 Main.qml。
}
