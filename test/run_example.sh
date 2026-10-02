#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_example.sh —— 运行 test/example（era 全函数覆盖示例）
#
# 用法：
#   ./test/run_example.sh          # 全部自动运行（组1..11 + 汇总）
#   ./test/run_example.sh 5        # 只运行第 5 组
#   ./test/run_example.sh 12       # 破坏性组（BEGIN TITLE，会回到标题画面）
#   ./test/run_example.sh 13       # 破坏性组（THROW，预期「执行出错」）
#
# 依赖：build/src/eraengine/test_cli
#       （cmake --build build --target test_cli）
# 退出码：0 = 全部断言通过且渲染自检无异常；非 0 = 有 FAIL / 渲染可疑项
#
# 关于「自动输入」：组 11（输入族）与组 0（全部）里的 INPUT / INPUTS /
# ONEINPUT / TINPUT / WAITANYKEY 等会挂起等待输入 —— GUI 下必须手点，
# 只有 test_cli 能用 --script 序列自动喂入。这里在后面补一串填充值
# （菜单 0 + 12 个填充），够组 11 的每条等待指令各消耗一个。
# ---------------------------------------------------------------------------
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="$ROOT/build/src/eraengine/test_cli"
GAME="$ROOT/test/example"

if [ ! -x "$CLI" ]; then
    echo "test_cli 不存在：$CLI"
    echo "先执行：cmake --build $ROOT/build --target test_cli"
    exit 1
fi

SELECTION="${1:-0}"
if [ "$SELECTION" = "0" ]; then
    # 全部自动：菜单选 0，随后 14 个填充值供「输入族」消费
    SCRIPT="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"
else
    SCRIPT="$SELECTION"
fi

# --check：渲染自检（未展开的 %..%/{..}、漏按钮等）发现可疑即退出码 2
OUT="$("$CLI" "$GAME" --script "$SCRIPT" --check --frames 400 2>&1)"
STATUS=$?
printf '%s\n' "$OUT"

# 断言失败不会体现在退出码里（--check 只管渲染），这里显式检查汇总行
if printf '%s' "$OUT" | grep -q "全部断言通过"; then
    ASSERT_OK=1
else
    ASSERT_OK=0
fi
if printf '%s' "$OUT" | grep -q "\[FAIL\]"; then
    ASSERT_OK=0
fi

echo
if [ $STATUS -eq 0 ] && [ "$ASSERT_OK" = "1" ]; then
    echo "== run_example：通过（渲染自检 + 全部断言）=="
    exit 0
fi
echo "== run_example：失败（渲染退出码 $STATUS，断言通过=$ASSERT_OK）=="
exit 1
