#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_gui_frames.sh —— GUI（appemuera）端到端 + 每帧抓取渲染
#
# 用 D-Bus 驱动真实 GUI 跑 test/example（不用手点），同时把**每一次画面刷新**
# （ConsoleBackend::windowChanged，即一次渲染帧）抓成 PNG 序列：
#
#   test/example + eraTW 的 CLEARLINE→重打印 复用行号场景，此前会
#   「点按钮后画面不刷新」（增量模型按行号复用旧区块）；帧序列能直接
#   目视对比「点击前后」的画面是否更新。
#
# 用法：
#   ./test/run_gui_frames.sh [游戏目录=test/example] [输出目录=/tmp/emuera_frames] \
#                            [输入序列=0]   # 逗号分隔；x 为 sendAnyKey
#
# 依赖：build 的 appemuera（含 D-Bus /debug）、qdbus6。
#      （cmake --build build --target appemuera）
# ---------------------------------------------------------------------------
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME="${1:-$ROOT/test/example}"
OUT="${2:-/tmp/emuera_frames}"
IFS=',' read -r -a INPUTS <<< "${3:-0}"

# 找 appemuera：在常见构建目录里选**最新**的那个（增量模型/帧抓取等新特性
# 依赖新构建；旧二进制缺 D-Bus 方法会被 qdbus 直接拒绝）
APP=""
APP_T=0
for c in "$ROOT/build/appemuera" "$ROOT/build-debug/appemuera" \
         "/tmp/build/Local_PC-Debug/appemuera"; do
    [ -x "$c" ] || continue
    t=$(stat -c %Y "$c")
    if [ "$t" -gt "$APP_T" ]; then APP="$c"; APP_T="$t"; fi
done
if [ -z "$APP" ]; then
    echo "appemuera 不存在：先 cmake --build build --target appemuera"
    exit 1
fi

rm -rf "$OUT" && mkdir -p "$OUT"
pkill -x appemuera 2>/dev/null && sleep 1

echo "启动 GUI：$APP  日志：$OUT/app.log"
# offscreen：CI/无显示环境也能跑（渲染仍走 QML 合成，抓帧照常工作）
setsid env QT_QPA_PLATFORM=offscreen "$APP" > "$OUT/app.log" 2>&1 < /dev/null &
for i in $(seq 1 40); do
    qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.ping >/dev/null 2>&1 && break
    sleep 0.25
done
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.ping >/dev/null 2>&1 \
    || { echo "D-Bus /debug 未就绪（见 $OUT/app.log）"; exit 1; }

echo "打开目录：$GAME"
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.openDirectory "$GAME"
sleep 3

echo "开始每帧抓取：$OUT/frame#####.png"
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.startFrameCapture "$OUT/frame" 0 >/dev/null

step() {  # step <值>：x = 任意键继续，其余为整数输入
    local v="$1"
    if [ "$v" = "x" ]; then
        qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.sendAnyKey >/dev/null
    else
        qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.sendInput "$v" >/dev/null
    fi
    sleep 2   # 等画面刷新 + 帧落盘（grabToImage 异步）
}

for v in "${INPUTS[@]}"; do
    echo "输入：$v"
    step "$v"
done

qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.stopFrameCapture >/dev/null
echo "--- 末屏（dumpScreen 20）---"
qdbus6 io.yigekuyou.emuera /debug io.yigekuyou.emuera.Debug.dumpScreen 20
echo "--- 帧序列 ---"
ls -la "$OUT" | grep -c 'frame.*\.png' | xargs echo "帧数："
ls "$OUT" | grep 'frame.*\.png' | head -5

pkill -x appemuera 2>/dev/null
echo "完成：$OUT/frame00000.png …"
