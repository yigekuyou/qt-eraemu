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
#ifndef RESOURCE_IMAGE_PROVIDER_H
#define RESOURCE_IMAGE_PROVIDER_H

#include <QQuickImageProvider>
#include <QString>
#include <QHash>
#include "Content/csv_loader.h"
// ---------------------------------------------------------------------------
// ResourceImageProvider —— 供 QML 用 `Image { source: "image://emuera/<name>" }`
// 读取游戏资源图（对齐 C# GameView 的 ConstImage「资源名 → 图片」加载）。
//
//    图集（sprite sheet）定义
//      通过 loadAtlas(".../list.csv") 加载形如：
//          <名>,<图集文件>[,<x>,<y>,<w>,<h>]
//      的行；命中后从源图集上裁出子图返回。后四列可省略 —— 表示整张图即该资源。
// 混合形态里 C++ 只提供「资源名 → 文件」的服务；实际显示由 QML 的 Image 对象负责。
// 名称解析顺序（第一个存在者胜出）：
//     <root>/resources/<name>[.ext]
//     <root>/<name>[.ext]
// 默认扩展名：png / jpg / jpeg / bmp / webp / gif（未写扩展名时逐个尝试）。
//
// requestImage 返回 QImage（不依赖 GUI 平台），因此可用 QCoreApplication 单测。
// ---------------------------------------------------------------------------
class ResourceImageProvider : public QQuickImageProvider {
public:
	struct Sprite {
			QString sourceFile;        // 图集图片文件名（相对于图集所在目录 / root）
			bool    hasRect = false;   // false = 整张图集就是该资源
			int     x = 0;
			int     y = 0;
			int     w = 0;
			int     h = 0;
			// CSV 第 7/8 列：输出时的位置调整（C# SpriteF.DestBasePosition /
			// AppContents.CreateFromCsv 的 tokens[6],tokens[7]）。
			// eraTW 的白蓮(55)/路人立绘都是「把带偏移的部件叠进一张 G」合成的：
			// 丢掉偏移 -> 所有部件挤在 (0,0)，立绘不完整、差分图像盖不到该盖的地方。
			// （只用来区分「显式写了第 7/8 列」与「没写」，绘制路径上两者等价。）
			bool    hasOffset = false;
			int     offsetX = 0;
			int     offsetY = 0;
	};
    ResourceImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // 资源根目录（游戏目录）；由 EraEngine 在装载时设置
    static void setRoot(const QString& dir);
    [[nodiscard]] static QString root();

    // 解析资源名到绝对文件路径（不存在返回空串）。root 为空时用已设置的根。
    [[nodiscard]] static QString resolvePath(const QString& id, const QString& root = QString());

    // 资源图片的**固有像素尺寸**（对齐 C# Emuera 的 ImageResource）。
    // `<img src='X'>` 不带 width/height 时，排版必须用图片自身尺寸；否则会被当成
    // 「一个字号见方」而缩成小方块（eraTW 标题画面正是这样被压成 16×16 的）。
    // 命中 resources/list.csv 的矩形取 w/h，否则按文件实际尺寸。找不到返回 false。
    static bool intrinsicSize(const QString& id, int& width, int& height);

    // 静态资源是否存在（图集条目或 resources 目录下的文件）。
    // C# 的 AppContents.LoadContents 会把 resources 下所有 csv 条目注册成精灵，
    // 因此 SPRITECREATED 系的查询也要把静态资源当作「已存在的精灵」。
    [[nodiscard]] static bool hasResource(const QString& id);

    // 仅按静态资源取图（图集矩形裁剪 / 文件），不含运行期精灵回退。
    // 供 GraphicsStore 的「静态资源即精灵」兜底使用。
    [[nodiscard]] static QImage loadResourceImage(const QString& id);

    // 静态精灵（CSV 定义的图集条目）的**输出偏移** —— CSV 第 7/8 列，
    // 对应 C# SpriteF 的 DestBasePosition（AppContents.CreateFromCsv 的
    // tokens[6]/tokens[7]）。eraTW 的 リソース作成.ERB / モブ子表示.ERB 靠它把
    // 部件叠到正确位置。返回 false = 不是 CSV 精灵（无偏移概念）。
    // 注：控制台 `<img src='X'>` 的排版路径（ConsoleLayout::measurePart）**不**叠加
    // 这个偏移（C# ConsoleImagePart.DrawTo 会）。eraTW 里带偏移的精灵（白蓮/路人部件）
    // 都是经 GDRAWSPRITE 参与合成的，直接 `<img>` 显示的精灵偏移都是 (0,0)。
    static bool spriteOffset(const QString& id, int& x, int& y);

    // 图片解码（先 Qt -> 再 libwebp -> 最后尺寸头回退）。
    //   Qt 的 webp 解码器对某些合法的 lossless 小图会失败：
    //   eraTW 的 resources/ダミー.webp 只有 34 字节（VP8L,180×180,全透明），
    //   4×4 纯色小图同理；用 QImage/QImageReader 一律「Unable to read image data」，
    //   但 libwebp（C# 走 WebPWrapper 直连）能正常读出真像素。
    //   eraTW 的立绘合成第一步是
    //   `GCREATE(GID, SPRITEWIDTH("ダミー"), SPRITEHEIGHT("ダミー"))`，尺寸读成 0
    //   会让整张合成图创建失败；把有内容的图退化成全透明则会让拼接缺块、特效消失。
    //   因此：Qt 失败时**直连 libwebp 解真像素**，实在解不出才按文件头
    //   （PNG/JPEG/BMP/GIF/WebP）解析尺寸、返回同尺寸全透明图兜底排版。
    [[nodiscard]] static QImage loadImageFile(const QString& path);

private:
    static QString s_root;
    static QHash<QString, Sprite> s_atlas;
    static QString s_atlasRoot;

    static void ensureAtlasLoaded(const QString& root);
};

#endif // RESOURCE_IMAGE_PROVIDER_H
