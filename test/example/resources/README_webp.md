# resources/ 里的 WebP 测试图 —— 来源与作者声明

## 一、来自网络的测试图（作者：ImagicTheCat）

以下 3 个文件从 ImagicTheCat 的 gist
**<https://gist.github.com/ImagicTheCat/e2abcd6bc843fe1c83ce02c513c1b406>**
（「webp animation tests」）下载，仓库根目录 `webp/` 是完整原件：

| 文件 | 规格 | 覆盖形态 |
| --- | --- | --- |
| `isotile_lossy.webp` | 2048×2048，VP8X | 有损（扩展格式头） |
| `anim_add_lossy.webp` | 1408×1536，VP8 | 有损（裸 VP8 头）、帧图集（11×12 格 128×128） |
| `anim_alpha_animated_lossy.webp` | 128×128，VP8X | **动画**（多帧）+ alpha 通道 |

gist 作者自述（README.adoc）：125 帧 128×128 的纹理图集 / WebP 动画，
用 ffmpeg 编码；`isotile` 来自 hiclipart。**gist 未附许可证**，
故仅作本仓库的引擎回归测试用途，不随产品分发、不用于其它目的。

## 二、本仓库自造的测试图

用 ImageMagick 生成（lossless WebP / VP8L，保证解码后**逐像素可断言**）：

| 文件 | 规格 | 内容 |
| --- | --- | --- |
| `webp_atlas.webp` | 16×16 | 四象限色块：左上红、右上蓝、左下绿、右下黄（各 8×8，不透明） |
| `webp_item.webp` | 4×4 | 品红色不透明色块（当「差分」压底片用） |

精灵清单见 `webp_atlas.csv`（组 34 的用法：eraTW 式「底片 + 部件堆叠 + 差分特效 → 精灵序列」）。
