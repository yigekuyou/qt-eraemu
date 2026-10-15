#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build_android.sh —— 本机构建 Android arm64-v8a APK（Qt for Android + NDK + Gradle）
#
# 脚本把这条链路上踩过的坑全部固化了（每条都对应一处失败）：
#   1) Qt for Android 套件里**没有宿主工具**（moc/rcc/uic/qmlcachegen/
#      qmltyperegistrar/qmlimportscanner/androiddeployqt 都不在），必须拿本机宿主 Qt
#      当 QT_HOST_PATH；而发行版把 cmake 包放在 /usr/lib64/cmake、工具放在
#      /usr/libexec/qt6，不在 Qt 前缀里，所以要额外给 QT_HOST_PATH_CMAKE_DIR 与
#      Qt6*Tools_DIR（否则报 "Could NOT find Qt6CoreTools"）。
#   2) NDK 用 zip 解压（不是 unzip）会把符号链接写成**文本文件**，bin/clang++ 变成
#      8 字节文本 → "command not found: clang"。脚本会检测并可 --fix-ndk-symlinks 修复。
#   3) Gradle wrapper 从 services.gradle.org 下载经常超时/被重定向到不可达 CDN。
#      脚本按 Gradle 自己的规则算缓存目录名（base36(md5(distributionUrl))），
#      直接从国内镜像预置 gradle 进 wrapper 缓存（含可执行位与 .ok 标记）。
#   4) dl.google.com 会间歇性超时（依赖解析失败）。脚本确保 ~/.gradle/init.d/ 下有
#      一份镜像 init 脚本：阿里云优先，google()/mavenCentral() 仍作兜底。
#   5) Qt 6.11 的 compileSdk 取「已安装的最高平台」，但新版 SDK 的平台目录叫
#      android-37.0 而不是 android-37 → AGP 报 "Failed to find target with hash
#      string 'android-37'"。项目已在 CMakeLists 里钉死
#      QT_ANDROID_COMPILE_SDK_VERSION=36（见那里的注释），这里不再处理。
#
# 用法：
#   ./test/build_android.sh              # 首次配置 + 编译 + 打包 APK
#   ./test/build_android.sh -r           # 重新配置（改了 CMakeLists / 增删源文件）
#   ./test/build_android.sh -c           # 干净重建（先删 build-android）
#   ./test/build_android.sh -i           # 构建完顺手 adb install -r
#   ./test/build_android.sh --fix-ndk-symlinks    # 修 NDK 被解压坏掉的符号链接
#   ./test/build_android.sh -j 16        # 并行度（默认 nproc）
#
# 可覆盖的环境变量（默认值按本机探测）：
#   ANDROID_SDK_ROOT  ANDROID_NDK_ROOT  JAVA_HOME  QT_ANDROID
#   QT_HOST_PATH  QT_HOST_PATH_CMAKE_DIR  BUILD_DIR  BUILD_TYPE
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-android}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
JOBS="$( (nproc 2>/dev/null || echo 4) | head -1 )"
PKG="io.yigekuyou.emuera"
DO_CLEAN=0
DO_CONFIGURE=0
DO_INSTALL=0
FIX_NDK=0

while [ $# -gt 0 ]; do
    case "$1" in
        -c|--clean)        DO_CLEAN=1; DO_CONFIGURE=1 ;;
        -r|--reconfigure)  DO_CONFIGURE=1 ;;
        -i|--install)      DO_INSTALL=1 ;;
        -j)                JOBS="$2"; shift ;;
        --fix-ndk-symlinks) FIX_NDK=1 ;;
        -h|--help)         sed -n '1,40p' "$0"; exit 0 ;;
        *) echo "未知参数: $1（-h 看用法）" >&2; exit 2 ;;
    esac
    shift
done

c_info()  { printf '\033[1;34m[android]\033[0m %s\n' "$*"; }
c_warn()  { printf '\033[1;33m[android]\033[0m %s\n' "$*"; }
c_err()   { printf '\033[1;31m[android] %s\033[0m\n' "$*" >&2; }
die()     { c_err "$*"; exit 1; }

# ---------------------------------------------------------------------------
# 1. 环境探测
# ---------------------------------------------------------------------------
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/home/light/android-sdk}}"
[ -d "$SDK" ] || die "找不到 Android SDK：$SDK（设 ANDROID_SDK_ROOT 或装 SDK）"

NDK="${ANDROID_NDK_ROOT:-}"
if [ -z "$NDK" ]; then
    NDK="$(ls -d "$SDK"/ndk/*/ 2>/dev/null | sort -V | tail -1)"
    NDK="${NDK%/}"
fi
[ -n "$NDK" ] && [ -d "$NDK" ] || die "找不到 NDK：${NDK:-未设置}（sdkmanager --install 'ndk;27.2.12479018'）"

# JDK：Gradle 9 不认太新的 JDK（本机系统 java 是 26），优先用 SDK 目录里自带的 21
if [ -z "${JAVA_HOME:-}" ] || [ ! -x "${JAVA_HOME:-}/bin/java" ]; then
    JH="$(ls -d "$SDK"/jdk-* 2>/dev/null | sort -V | tail -1)"
    if [ -n "$JH" ] && [ -x "$JH/bin/java" ]; then
        JAVA_HOME="$JH"
    else
        JAVA_HOME=""
    fi
fi
if [ -n "$JAVA_HOME" ]; then
    export JAVA_HOME
else
    c_warn "未找到 JDK（$SDK/jdk-* 或 JAVA_HOME），将用 PATH 里的 java —— 若是 JDK 22+ Gradle 可能拒绝"
fi

# Qt for Android 交叉套件（含 arm64 库与 qt-cmake 包装脚本，**不含宿主工具**）
if [ -z "${QT_ANDROID:-}" ]; then
    QT_ANDROID="$(ls -d /home/light/Qt/*/android_arm64_v8a 2>/dev/null | sort -V | tail -1)"
fi
[ -n "$QT_ANDROID" ] && [ -x "$QT_ANDROID/bin/qt-cmake" ] || die \
    "找不到 Qt for Android 套件（缺 $QT_ANDROID/bin/qt-cmake）。
    装法：python3 -m venv ~/.aqt-venv && ~/.aqt-venv/bin/pip install aqtinstall
          ~/.aqt-venv/bin/aqt install-qt linux android 6.11.2 android_arm64_v8a \
              -O ~/Qt -m qtimageformats qtmultimedia"

# 宿主 Qt：只提供 moc/rcc/uic/qmlcachegen/qmltyperegistrar/qmlimportscanner/androiddeployqt
QT_HOST_PATH="${QT_HOST_PATH:-/usr/lib64/qt6}"
if [ -z "${QT_HOST_PATH_CMAKE_DIR:-}" ]; then
    if [ -d "$QT_HOST_PATH/lib/cmake/Qt6" ]; then
        QT_HOST_PATH_CMAKE_DIR="$QT_HOST_PATH/lib/cmake"
    elif [ -d /usr/lib64/cmake/Qt6 ]; then
        QT_HOST_PATH_CMAKE_DIR="/usr/lib64/cmake"     # 发行版布局（openSUSE/Fedora）
    elif [ -d /usr/lib/x86_64-linux-gnu/cmake/Qt6 ]; then
        QT_HOST_PATH_CMAKE_DIR="/usr/lib/x86_64-linux-gnu/cmake"
    else
        die "找不到宿主 Qt 的 cmake 目录（QT_HOST_PATH_CMAKE_DIR）"
    fi
fi

# 宿主工具是否真的存在（发行版把可执行文件放在 libexec/qt6）
find_host_tool() {
    for d in /usr/libexec/qt6 "$QT_HOST_PATH/libexec/qt6" "$QT_HOST_PATH/libexec" \
             "$QT_HOST_PATH/bin" /usr/lib64/qt6/libexec /usr/lib64/qt6/bin; do
        [ -x "$d/$1" ] && { echo "$d/$1"; return 0; }
    done
    return 1
}
MOC="$(find_host_tool moc)"   || die "宿主 Qt 缺 moc（QT_HOST_PATH=$QT_HOST_PATH）"
QMLCG="$(find_host_tool qmlcachegen)" || die "宿主 Qt 缺 qmlcachegen"
ADQ="$(find_host_tool androiddeployqt || ls /usr/lib64/qt6/bin/androiddeployqt 2>/dev/null)" \
    || die "宿主 Qt 缺 androiddeployqt"

TOOLS_FLAGS=""
for t in Qt6CoreTools Qt6GuiTools Qt6QmlTools Qt6WidgetsTools; do
    [ -d "$QT_HOST_PATH_CMAKE_DIR/$t" ] && TOOLS_FLAGS="$TOOLS_FLAGS -D${t}_DIR=$QT_HOST_PATH_CMAKE_DIR/$t"
done
[ -n "$TOOLS_FLAGS" ] || die "宿主 Qt 的 Qt6*Tools cmake 包缺失（$QT_HOST_PATH_CMAKE_DIR）"

export ANDROID_SDK_ROOT="$SDK" ANDROID_HOME="$SDK"
export ANDROID_NDK_ROOT="$NDK" ANDROID_NDK_HOME="$NDK"

c_info "SDK      : $SDK"
c_info "NDK      : $NDK"
c_info "JDK      : ${JAVA_HOME:-（PATH 里的 java）}"
c_info "Qt 交叉  : $QT_ANDROID"
c_info "宿主工具 : $MOC"
c_info "构建目录 : $BUILD_DIR  ($BUILD_TYPE, -j$JOBS)"

# ---------------------------------------------------------------------------
# 2. NDK 符号链接体检（zip 解压损坏时 clang++ 不可执行）
# ---------------------------------------------------------------------------
ndk_symlink_report() {
    python3 - "$NDK" "$FIX_NDK" <<'PY'
import os, sys
ndk, do_fix = sys.argv[1], sys.argv[2] == "1"
bad = []
for root, _dirs, files in os.walk(ndk):
    for f in files:
        p = os.path.join(root, f)
        try:
            if os.path.islink(p) or os.lstat(p).st_size > 64:
                continue
            data = open(p, "rb").read()
            if b"\n" in data or b" " in data or not data:
                continue
            txt = data.decode("ascii")
        except (OSError, UnicodeDecodeError):
            continue
        if os.path.exists(os.path.join(root, txt)):   # 目标在同目录 → 原样是符号链接
            bad.append((p, txt))
if do_fix:
    for p, txt in bad:
        os.remove(p); os.symlink(txt, p)
    print("FIXED %d" % len(bad))
else:
    print("BAD %d" % len(bad))
    for p, _t in bad[:8]:
        print("  " + os.path.relpath(p, ndk))
PY
}
if command -v python3 >/dev/null 2>&1; then
    NL="$(ndk_symlink_report | head -1)"
    case "$NL" in
        BAD\ 0) : ;;
        BAD\ *) c_warn "NDK 里有被解压器写成文本的符号链接（clang++ 会不可执行）：${NL#BAD } 个"
                c_warn "  修复：$0 --fix-ndk-symlinks（等价于按内容重建符号链接）"
                [ "$FIX_NDK" = "1" ] && die "（本次已带 --fix-ndk-symlinks，但仍显示未修：请检查权限）" ;;
        FIXED\ *) c_info "已修复 NDK 符号链接：${NL#FIXED } 个" ;;
    esac
    "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" --version >/dev/null 2>&1 \
        || die "NDK 的 clang++ 不可用（$NDK）—— 若是 zip 解压损坏，跑 $0 --fix-ndk-symlinks"
fi

# ---------------------------------------------------------------------------
# 3. Gradle：镜像 init 脚本 + wrapper 发行版预置（避免 CDN 超时）
# ---------------------------------------------------------------------------
MIRROR_INIT="$HOME/.gradle/init.d/emuera-mirrors.gradle"
if [ ! -f "$MIRROR_INIT" ]; then
    mkdir -p "$(dirname "$MIRROR_INIT")"
    cat > "$MIRROR_INIT" <<'EOF'
// CN 网络下 dl.google.com / repo.maven.apache.org 会间歇性超时。
// init 脚本的 allprojects 闭包在各项目 build.gradle 之前执行，故镜像优先、
// 原仓库仍作兜底。全局文件，影响本机其它 Gradle 构建；不需要时删除即可。
allprojects {
    buildscript {
        repositories {
            maven { url = 'https://maven.aliyun.com/repository/google' }
            maven { url = 'https://maven.aliyun.com/repository/public' }
        }
    }
    repositories {
        maven { url = 'https://maven.aliyun.com/repository/google' }
        maven { url = 'https://maven.aliyun.com/repository/public' }
    }
}
EOF
    c_info "已写入 Gradle 镜像 init 脚本：$MIRROR_INIT"
fi

ensure_gradle_dist() {
    local props="$BUILD_DIR/android-build/gradle/wrapper/gradle-wrapper.properties"
    [ -f "$props" ] || return 0
    local url ver hash dist
    url="$(sed -n 's/^distributionUrl=//p' "$props" | sed 's/\\//g')"
    ver="$(printf '%s' "$url" | sed -n 's/.*gradle-\([0-9][0-9.]*\)-.*/\1/p')"
    [ -n "$ver" ] || return 0
    hash="$(python3 - "$url" <<'PY'
import hashlib, sys
n = int.from_bytes(hashlib.md5(sys.argv[1].encode()).digest(), 'big')
d = "0123456789abcdefghijklmnopqrstuvwxyz"; s = ""
while n: s = d[n % 36] + s; n //= 36
print(s or "0")
PY
)"
    dist="$HOME/.gradle/wrapper/dists/gradle-$ver-bin/$hash"
    if [ -d "$dist/gradle-$ver" ] && [ -f "$dist/gradle-$ver-bin.zip.ok" ]; then
        c_info "Gradle $ver 已在 wrapper 缓存（$dist）"
        return 0
    fi
    c_info "预置 Gradle $ver 到 wrapper 缓存（绕开 services.gradle.org 的超时/CDN 重定向）"
    mkdir -p "$dist"; rm -f "$dist"/*.part "$dist"/*.lck
    local zip="$dist/gradle-$ver-bin.zip" ok=0
    for m in "https://mirrors.cloud.tencent.com/gradle" \
             "https://mirrors.aliyun.com/macports/distfiles/gradle" \
             "https://services.gradle.org/distributions"; do
        if curl -fSL --retry 3 --retry-delay 2 --max-time 900 -o "$zip" "$m/gradle-$ver-bin.zip"; then
            ok=1; break
        fi
    done
    [ "$ok" = "1" ] || die "Gradle $ver 下载失败（镜像与官方都不可达）"
    python3 - "$zip" "$dist" <<'PY'
import os, sys, zipfile
zip_path, dist = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path) as z:
    z.extractall(dist)
    for i in z.infolist():                     # zip 里记的可执行位必须手工恢复
        if (i.external_attr >> 16) & 0o111:
            p = os.path.join(dist, i.filename)
            if os.path.exists(p):
                os.chmod(p, (os.stat(p).st_mode & 0o777) | 0o111)
PY
    touch "$dist/gradle-$ver-bin.zip.ok"
    c_info "Gradle $ver 就绪：$dist"
}

# ---------------------------------------------------------------------------
# 4. 配置 + 编译 + 打包
# ---------------------------------------------------------------------------
[ "$DO_CLEAN" = "1" ] && { c_info "清理 $BUILD_DIR"; rm -rf "$BUILD_DIR"; }
[ -f "$BUILD_DIR/CMakeCache.txt" ] || DO_CONFIGURE=1

if [ "$DO_CONFIGURE" = "1" ]; then
    c_info "配置（qt-cmake → Ninja）"
    "$QT_ANDROID/bin/qt-cmake" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DEMUERA_WARNINGS_AS_ERRORS=OFF \
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=TRUE \
        -DQT_HOST_PATH="$QT_HOST_PATH" \
        -DQT_HOST_PATH_CMAKE_DIR="$QT_HOST_PATH_CMAKE_DIR" \
        $TOOLS_FLAGS \
        -DANDROID_SDK_ROOT="$SDK" -DANDROID_NDK_ROOT="$NDK" || die "配置失败"
fi

ensure_gradle_dist

c_info "编译 + 打包（target=apk）"
if ! cmake --build "$BUILD_DIR" --target apk -j"$JOBS"; then
    c_err "打包失败。常见原因："
    c_err "  · 依赖下载超时 → 检查 $MIRROR_INIT 是否存在"
    c_err "  · Gradle/AGP 版本与 JDK 不合 → 本脚本默认用 $SDK/jdk-* 里的 JDK"
    die "见上面的 Gradle 输出"
fi

APK="$BUILD_DIR/android-build/build/outputs/apk/debug/android-build-debug.apk"
[ -f "$APK" ] || APK="$BUILD_DIR/android-build/appemuera.apk"
[ -f "$APK" ] || die "构建结束但找不到 APK"

AAPT2="$(ls "$SDK"/build-tools/*/aapt2 2>/dev/null | sort -V | tail -1)"
c_info "APK: $APK ($(du -h "$APK" | cut -f1))"
if [ -n "$AAPT2" ]; then
    "$AAPT2" dump badging "$APK" 2>/dev/null | grep -E "^package:|sdkVersion|targetSdkVersion|native-code" \
        | sed 's/^/        /'
fi
cat <<EOF

下一步：
  安装到设备      adb install -r "$APK"
  看引擎日志      adb logcat -s Emuera          # 装载阶段/心跳/告警都在这里
  只看装载心跳    adb logcat -s Emuera | grep 装载心跳
EOF

# ---------------------------------------------------------------------------
# 5. 可选安装（签名不匹配时只提示，不擅自卸载 —— 卸载会清掉存档）
# ---------------------------------------------------------------------------
if [ "$DO_INSTALL" = "1" ]; then
    command -v adb >/dev/null 2>&1 || die "没有 adb"
    if adb install -r "$APK"; then
        c_info "已安装（保留应用数据）"
    else
        c_warn "覆盖安装失败（大概率是签名与设备上那份不同：CI 的 debug key ≠ 本机 debug key）"
        c_warn "卸载会清掉 files/settings（扫描目录）与 files/save-data（存档）。保数据换包："
        cat <<EOF
        adb shell am force-stop $PKG
        adb exec-out run-as $PKG tar -cf - files > /tmp/$PKG-data.tar
        adb uninstall $PKG
        adb install "$APK"
        adb shell run-as $PKG tar -xf - -C /data/data/$PKG < /tmp/$PKG-data.tar
EOF
        exit 1
    fi
fi