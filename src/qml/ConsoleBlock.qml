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
import QtQuick.Controls


// 最小单位区块（QML 侧的「一个区块对象」）
//
// C++ 只给**相对网格坐标与格子数**（单位 = 区块长 / 区块高）：
//   col / row        —— 绝对网格坐标（相对 root 全平面）
//   relCol / relRow  —— 相对网格坐标（相对所属行）
//   cols / rows      —— 占几个单位（动态测量出来的格子数）
// **像素大小由本组件决定**：px = col × cellWidth，py = row × cellHeight，
// 也就是说换字体/缩放只改 cellWidth/cellHeight，不用动 C++。
//
//   kind === "text"  → Text（**可点击 span 也是 Text**，只是多一层悬停高亮）
//   kind === "image" → Image（source: image://emuera/<资源名>；取不到退化成 altText）
//   kind === "shape" → Rectangle（space / rect / line）
//
// 点击由区块自己上报：同一个「段」的多个区块共享同一份点击值，
// 所以点哪个区块都等价（对齐 C# 的「先命中 part，再映射到 ConsoleButtonString」）。
//
// 按钮 span **不再套原生 Button**（00097c1 引入，已移除）：Qt Quick Controls 的
// Button 有自己的内边距/最小尺寸/居中文本，画出来比文字宽，且与按网格排布的
// 相邻区块对不齐。Emuera 的按钮就是「文字 + 换色 + tooltip」，所以这里统一按
// 网格逐字排布，只保留悬停高亮；命中仍只有一个入口 blockButtonMouse
// （「命中 → clickAt → submitInput」，与 C++ 世代校验同源）。
Item {
    id: block

    property var blockData: ({})
    property var backend: null
    // 窗口顶行的绝对行号（根容器传入）：y = (row - windowTopRow + offsetRows) × 行高。
    // 滚动只改这一个值，模型与区块数据不动（Qt 文档：绑定重求值，微秒级）。
    property int windowTopRow: 0
    // ---- 单元格大小（由容器决定）----
    property real cellWidth: 9
    property real cellHeight: 19
    // 空字体/颜色表示使用系统主题默认值；C++ 只在模板显式指定时提供覆盖值。
    property string fontName: ""
    property int fontSize: 16
    property string foreColor: ""
    property string focusColor: ""
    property string logColor: ""
    property bool isBacklog: false
    SystemPalette { id: systemPalette }

    readonly property string effectiveFontName: blockData && blockData.fontName
                                                ? blockData.fontName : fontName
    readonly property color effectiveFocusColor: blockData && blockData.buttonColor
                                                ? blockData.buttonColor
                                                : (focusColor !== "" ? focusColor : systemPalette.highlight)
    readonly property color effectiveTextColor: {
        const d = blockData || ({});
        if (hovered) return effectiveFocusColor;
        if (isBacklog && d.colorChanged === false && logColor !== "") return logColor;
        if (d.color) return d.color;
        return foreColor !== "" ? foreColor : systemPalette.text;
    }

    // SETBGCOLOR 的文字背景色（span style.bgColor；无效 = 透明 = 主题背景）
    readonly property color effectiveBgColor: (blockData && blockData.bgColor)
                                              ? blockData.bgColor : "transparent"

    readonly property string kind: blockData ? (blockData.kind || "text") : "text"
    readonly property int gridCol: blockData && blockData.col !== undefined ? blockData.col : 0
    readonly property int gridRow: blockData && blockData.row !== undefined ? blockData.row : 0
    readonly property int gridCols: blockData && blockData.cols > 0 ? blockData.cols : 1
    readonly property int gridRows: blockData && blockData.rows > 0 ? blockData.rows : 1
    readonly property bool clickable: blockData && backend
        ? blockData.clickable === true && blockData.generation === backend.generation : false
    readonly property bool hovered: mouse.containsMouse && clickable

    // 图片的 `<img ypos=N>`：C++ 折算成「行数」的纵向偏移（负 = 往上盖）。
    // eraTW 的画像枠/時間停止/特效各自在被打印的那一行，靠它拉回来盖在立絵边缘。
    readonly property real offsetRows: blockData && blockData.offsetRows ? blockData.offsetRows : 0

    // 文本按「等宽段」切分后渲染（性能）：
    // 以前**每个字符**一个 Item+Text+Scale —— 一屏 80×38 就是数千个 QQuickText，
    // 而每次窗口重排（滚动一步 / 输出一行 / 换字号）C++ 都会重发模型，Instantiator
    // 把整屏区块对象销毁重建 -> 数千个 Text 反复创建，快速滚动直接卡死。
    // 现在把**相邻、同格宽**的字符并成一段（run）：半角行 = 1 个 Text、全角行 = 1 个
    // Text，混排才切段（通常 1~3 段），对象数降一到两个数量级。
    // 缩放仍按「段宽 = 字数 × 格宽」做，与逐字缩放等价（且不会有字间漂移）。
    readonly property var glyphRuns: {
        const d = block.blockData;
        if (!d || !d.text) return [];
        const t = d.text;
        const runs = [];
        let buf = "";
        let wide = null;
        for (let i = 0; i < t.length; ++i) {
            const u = t.charCodeAt(i);
            const w = !(u < 128 || (u >= 0xff61 && u <= 0xff9f));   // 与逐字版同判定
            if (wide === null || w === wide) {
                buf += t[i];
                wide = w;
            } else {
                runs.push({ text: buf, units: wide ? 2 : 1, count: buf.length });
                buf = t[i];
                wide = w;
            }
        }
        if (buf !== "")
            runs.push({ text: buf, units: wide ? 2 : 1, count: buf.length });
        return runs;
    }

    // 位置与尺寸：网格坐标 × 单元格大小（QML 说了算）
    // row 允许为负 / 超过行数：跨行图与带 ypos 的图层块会探出窗口，交给视口 clip。
    x: gridCol * cellWidth
    y: (gridRow - windowTopRow + offsetRows) * cellHeight
    // 外层至少覆盖 C++ 的网格测量；文本内容本身由 glyph 容器自动撑开。
    width: Math.max(gridCols * cellWidth, contentRow.implicitWidth)
    height: Math.max(gridRows * cellHeight, contentRow.implicitHeight)
    // 叠放顺序 = 控制台打印顺序（C++ 给的扁平 z）。三个 Instantiator 共用同一个
    // 父 Item 后，靠它把 text/image/shape 按真正的绘制顺序交叠 —— 对齐 C#
    // 「逐 part 顺序绘制」：图可压住先打印的字，后打印的字也能盖住先打印的图。
    z: blockData && blockData.z !== undefined ? blockData.z : 0

    // ---- 悬停高亮（可点击区块，含按钮 span）----
    Rectangle {
        anchors.fill: parent
        visible: block.hovered
        color: block.effectiveFocusColor
        opacity: 0.22
        radius: 2
    }

    // A Text.width constrains layout, not glyph advance. Render each grid
    // character in its assigned cells so font fallback/proportional fonts cannot
    // move later characters or paint over the following span.
    // SETBGCOLOR 背景（画在文字之后，z = -1）
    Rectangle {
        objectName: "spanBg"
        visible: block.kind === "text" && block.effectiveBgColor.a > 0
        color: block.effectiveBgColor
        anchors.fill: parent
        z: -1
    }

    Row {
        id: contentRow
        objectName: "textCells"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: block.kind === "text"
        Repeater {
            model: block.visible && block.kind === "text" ? block.glyphRuns : []
            delegate: Item {
                required property var modelData          // { text, units, count }
                width: modelData.units * modelData.count * block.cellWidth
                height: block.gridRows * block.cellHeight
                // 不启用 clip（Qt 文档「Performance considerations」：Clipping inside
                // a delegate is especially bad, avoid at all costs）。缩放原点用
                // Left：绘制严格落在本格宽度内，不会溢到相邻 span。
                Text {
                    id: glyph
                    objectName: "gridGlyph"
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: parent.modelData.text
                    // 空 family 交给 Qt 使用系统默认字体。
                    font.family: block.effectiveFontName
                    font.pixelSize: block.fontSize > 0 ? block.fontSize : undefined
                    transform: Scale {
                        origin.x: 0
                        xScale: glyph.implicitWidth > 0 ? glyph.parent.width / glyph.implicitWidth : 1
                    }
                    color: block.effectiveTextColor
                    font.bold: block.blockData ? block.blockData.bold === true : false
                    font.italic: block.blockData ? block.blockData.italic === true : false
                    font.underline: block.blockData ? block.blockData.underline === true : false
                    font.strikeout: block.blockData ? block.blockData.strike === true : false
                }
            }
        }
    }

    // <img srcb='...'>：按钮选中/悬停态替换图（C# ConsoleImagePart.cImageB：
    // isSelecting||isFocus 时**改画 srcb**，不是叠加 —— 平时 src、指向时 srcb）。
    // 两张 Image 同几何叠放，按 hovered 切换可见性；无 srcb 时退化为单张。
    Image {
        id: imageItemB
        visible: imageItem.visible && block.hovered
                 && block.blockData && (block.blockData.imageButton || "") !== ""
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        source: (visible && block.blockData && block.blockData.imageButton)
                    ? ("image://emuera/" + encodeURIComponent(block.blockData.imageButton)) : ""
        width: Math.max(1, block.width)
        height: Math.max(1, block.height)
        fillMode: Image.PreserveAspectFit
        // Qt 文档（Images）：「Enable image.smooth only if required」——
        // 原尺寸显示时 smooth 无视觉效果还会更慢，仅在缩放时开启。
        smooth: width < imageItemB.implicitWidth || height < imageItemB.implicitHeight
        asynchronous: true
        retainWhileLoading: true
    }

    Image {
        id: imageItem
        objectName: "blockImage"          // 供 QML 测试 findChild 命中
        visible: block.kind === "image" && !imageItemB.visible
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        // QQuickImageProvider（Qt 文档）："image:" scheme + provider 标识 + id；
        // provider 名不区分大小写，id 其余部分保留大小写 —— 非 ASCII 资源名必须
        // 先 encodeURIComponent（C++ 侧 normalizeId 再 QUrl::fromPercentEncoding 还原）。
        source: (visible && block.blockData && block.blockData.text)
                    ? ("image://emuera/" + encodeURIComponent(block.blockData.text)) : ""
        // ConsoleLayout supplies grid dimensions.  Keep the Image item at the
        // same size as its span so a loaded resource cannot paint into the
        // following line or leave a zero-sized QML item.
        //
        // fillMode 用 PreserveAspectFit 而非 QML 默认的 Stretch：区块尺寸是
        // 「列数×半角宽 / 行数×行高」的**网格量化**值，与 C# 的像素精确 destRect
        // 有半格误差。Stretch 会把这个误差变成拉伸（例：eraTW 标题 1041×16 的
        // 条图被拉成 1040×行高）；PreserveAspectFit 保持原图纵横比，视觉等价。
        width: Math.max(1, block.width)
        height: Math.max(1, block.height)
        fillMode: Image.PreserveAspectFit
        smooth: width < imageItem.implicitWidth || height < imageItem.implicitHeight
        asynchronous: true
        // 文档：source 变化时默认立即丢弃旧图（异步加载会闪一下）；
        // 立絵/表情切换频繁，保留旧图直到新图就绪可避免闪烁。
        retainWhileLoading: true

        // 资源取不到时按文本回退（对齐 C#：把 <img src='…'> 当文字画）
        Text {
            visible: imageItem.status === Image.Error
            textFormat: Text.PlainText
            text: block.blockData ? (block.blockData.altText || "") : ""
            color: block.effectiveTextColor
            font.family: block.effectiveFontName
            font.pixelSize: block.fontSize > 0 ? block.fontSize : undefined
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        id: shapeRect
        visible: block.kind === "shape"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: block.width
        height: (block.blockData && block.blockData.shapeType === "line") ? 1 : block.height
        opacity: (block.blockData && block.blockData.shapeType === "space") ? 0 : 1
        color: block.effectiveTextColor
    }

    MouseArea {
        id: mouse
        objectName: "blockButtonMouse"      // 供 QML 测试 findChild 命中
        z: 1
        anchors.fill: parent
        // 只在可点击时开启悬停：全屏数千个区块逐帧 hover 命中测试是
        // 滚动/鼠标移动卡顿的主要来源之一（不可点击区块永远不需要高亮）。
        hoverEnabled: block.clickable
        enabled: block.clickable
        cursorShape: block.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (block.backend && block.clickable && block.blockData)
                block.backend.clickAt(block.blockData.lineIndex, block.blockData.segmentIndex);
        }
        // 按钮的 Title（C# ButtonString.Title）——以前挂在原生 Button 上，
        // 去掉按钮样式后改挂在命中区上，行为不变。
        ToolTip.visible: block.hovered && block.blockData
                         && (block.blockData.tooltip || "") !== ""
        ToolTip.delay: 350
        ToolTip.text: block.blockData ? (block.blockData.tooltip || "") : ""
    }
}
