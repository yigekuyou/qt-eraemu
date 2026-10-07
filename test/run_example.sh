#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_example.sh —— 运行 test/example（era 全函数覆盖示例）
#
# 用法：
#   ./test/run_example.sh          # 全部自动运行（组1..11 + 汇总）
#   ./test/run_example.sh 5        # 只运行第 5 组
#   ./test/run_example.sh 12       # 破坏性组（BEGIN TITLE，会回到标题画面）
#   ./test/run_example.sh 13       # 破坏性组（THROW，预期「执行出错」）
#   ./test/run_example.sh 15       # 鼠标组（INPUTMOUSEKEY：k 注入 + 超时两条路径）
#   ./test/run_example.sh 18       # 破坏性组（RESTART + EE 破坏系：QUIT 族真实现）
#   ./test/run_example.sh 19       # 破坏性组（DOTRAIN，预期「执行出错」）
#   ./test/run_example.sh 36       # QUIT_AND_RESTART（重启请求·结构性判据）
#   ./test/run_example.sh 37       # FORCE_QUIT_AND_RESTART（立即重启·结构性判据）
#   ./test/run_example.sh 20       # RESTART 菜单复刻（eraTW NEWGAME_CUSTOM 回归）
#   ./test/run_example.sh 21       # GOTO $标签 函数作用域（eraTW COMMON @CHOICE 回归）
#   ./test/run_example.sh 23       # PRINTDATA/DATAFORM/ENDDATA（eraTW TW_TIPS 复现·特征化）
#   ./test/run_example.sh 24       # CSV 名表与 STR 变量（ecd/docs 规范：Str.csv 值 / StrName.csv 名）
#   ./test/run_example.sh 25       # RESULTS 下标/#FUNCTIONS 返回值/裸文本赋值（eraTW OPTION_SETTING 回归）
#   ./test/run_example.sh 26       # 自然结束不清 RESULT/裸 RETURNF 空串（eraTW 農家設定选地主回归）
#   ./test/run_example.sh 27       # 文档语义·表达式/字面量/声明（ecd/docs + ERH 宏/REF/参数初始值）
#   ./test/run_example.sh 28       # 文档语义·SELECTCASE/循环/EE 与 eraTW 惯用法
#   ./test/run_example.sh 29       # 破坏性组（BEGIN FIRST：@EVENTFIRST #PRI/#LATER/#SINGLE/#ONLY 流）
#   ./test/run_example.sh 30       # 音频·图片（用命令随机生成素材：G* 图像命令 + EE 音频命令）
#   ./test/run_example.sh 31       # GETCONFIG/GETCONFIGS（emuera.config 取值白名单/类型/默认）
#   ./test/run_example.sh 32       # 通用图像处理（G/SPRITE/CBG + 真实图像文件 webp：GDRAWSPRITE 落点/覆盖、libwebp）
#   ./test/run_example.sh 33       # END 是变量（#DIM END）不是指令（eraTW 角色移動 死循环回归）
#   ./test/run_example.sh 38       # EE 库 COLUMN_LIB（手写 CALL 用例；COLUMN* 是
#                                  #   EmueraEE 附带的 ERB 库而非引擎命令，无注入输入）
#   ./test/run_example.sh 39       # EM/Emuera.NET fork 族（MAP_/ENUM*/XML_/DT_ 手写断言）
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
elif [ "$SELECTION" = "15" ]; then
    # 鼠标组：菜单选 15 + `k 1 10 10 1 0` 注入（type=1 左键）——
    # 组15 的 INPUTMOUSEKEY(0) 吃注入，INPUTMOUSEKEY(50) 靠超时自动继续；
    # 之后 EE 输入族：`1` 给第一个 INPUTANY（整数分支），
    # `s 任意文本` 给第二个 INPUTANY（字符串分支）。
    SCRIPT="15,k 1 10 10 1 0,1,s 任意文本"
elif [ "$SELECTION" = "20" ]; then
    # RESTART 菜单复刻组：菜单选 20，随后按 @CUSTOM_TERMINAL_REPLICA 的
    # INPUT/INPUTS 序列喂入（0 命中 CASE 0 TO 999 是本次回归的关键）：
    # 改名(0 + s 名字) -> RESTART -> 子菜单(2000 + 9999 返回) -> RESTART -> 完了(9999)
    SCRIPT="20,0,s TW名字,2000,9999,9999"
elif [ "$SELECTION" = "21" ]; then
    # GOTO 标签作用域组：菜单选 21，@CHOICE_REPLICA 先喂 3（越界 ->
    # CASEELSE 惩罚 -> GOTO 回本函数 $INPUT_LOOP），再喂 1（合法 -> 返回 1）
    SCRIPT="21,3,1"
elif [ "$SELECTION" = "35" ]; then
    # 文档语义冒烟组：按 data/doc_smoke.tsv 第 6 列的注入序列喂入
    # （数值 / `s 字符串` / `k` 鼠标；顺序=清单执行顺序，勿随意改动）
    SCRIPT="35,0,0,s A,0,7,7,k 1 10 10 1 0,s 冒烟名字,0,s A,7,s a,0,0,0,7,s abc,7,s q,0,0,0,0"
elif [ "$SELECTION" = "22" ]; then
    # MAP 绘制复现组（eraTW DRAW_MAP 逐字符热路径）：无输入；
    # 帧预算放宽到 0（不限）保证 21 张图画完并出汇总。
    # 计时：time ./test/run_example.sh 22（单张耗时突增即性能回归）
    SCRIPT="22"
    FRAMES=0
else
    SCRIPT="$SELECTION"
fi
FRAMES="${FRAMES:-400}"

# --check：渲染自检（未展开的 %..%/{..}、漏按钮等）发现可疑即退出码 2
OUT="$("$CLI" "$GAME" --script "$SCRIPT" --check --frames "$FRAMES" 2>&1)"
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

# QUIT_AND_RESTART / FORCE_QUIT_AND_RESTART（组 36/37）：这两条是**终止类**命令，
# 不打印断言汇总 —— 判据是结构性的：
#   ① 出现宿主受理重启的标记（[restart] 脚本请求重启）；
#   ② 命令之后的行（★不应出现★）没有被打印；
#   ③ 重启后标题菜单重新出现（标记之后再次出现"请输入编号"）——
#      说明 reload() + runSystem() 真的把游戏重新驱动起来了。
if [ "$SELECTION" = "36" ] || [ "$SELECTION" = "37" ]; then
    ASSERT_OK=0
    if printf '%s' "$OUT" | grep -q "\[restart\] 脚本请求重启" \
       && ! printf '%s' "$OUT" | grep -q "★不应出现在这里★" \
       && printf '%s' "$OUT" | awk '/\[restart\] 脚本请求重启/{f=1} f&&/请输入编号/{ok=1} END{exit ok?0:1}'; then
        ASSERT_OK=1
    fi
fi

echo
if [ $STATUS -eq 0 ] && [ "$ASSERT_OK" = "1" ]; then
    echo "== run_example：通过（渲染自检 + 全部断言）=="
    exit 0
fi
echo "== run_example：失败（渲染退出码 $STATUS，断言通过=$ASSERT_OK）=="
exit 1
