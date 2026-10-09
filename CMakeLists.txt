# emuera —— Emuera（ERB 脚本引擎）的 Qt6 + QML/C++ 移植
# Copyright (C) 2026  yigekuyou
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
cmake_minimum_required(VERSION 3.16)

project(emuera VERSION 0.0.1 LANGUAGES CXX)

option(EMUERA_WARNINGS_AS_ERRORS "Treat warnings as errors for project targets" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 必需模块：Android / Windows / macOS / Linux 的官方 Qt 都提供。
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Core Widgets Concurrent Test QuickTest)
# QtDBus 只用于桌面 Linux（单实例唤起 + 全局菜单宿主探测，见 main.cpp 与
# window_controller.cpp）。Android 的 Qt 不保证带这个模块，因此设为可选：
# 找到就定义 EMUERA_HAS_DBUS，源码按该宏裁剪 D-Bus 实现（缺失时静默降级）。
find_package(Qt6 OPTIONAL_COMPONENTS DBus)
# OpenMP 目前没有任何使用点（源码里没有 #pragma omp），设为可选：macOS
# （AppleClang 默认不带 libomp）与 Android NDK 上找不到也不会配置失败。
find_package(OpenMP)
# bison/flex 还没接上生成规则（没有 BISON_TARGET/FLEX_TARGET），
# 所以不做 REQUIRED —— Windows/macOS 的 CI runner 不必额外安装。
find_package(BISON)
find_package(FLEX)
# REQUIRES 6.11 而不是 6.10：engine 里用了 QStringDecoder/QStringEncoder
# ::finalize() 与 QStringConverter::FinalizeResultError（Qt 文档标注
# "introduced in Qt 6.11"，见 Content/encoding/text_encoding.cpp）；写 6.10
# 的话配置能过、编译才炸 C2039，不如让 CMake 早一点报清楚。
qt_standard_project_setup(REQUIRES 6.11)

# 生成 qmlls.ini（QML Language Server）：把工程自己的 import path 写进配置，
# 编辑器侧（qmlls / Qt Creator / VS Code 的 QML 扩展）才能解析
# io.yigekuoyou.appemuera 与 io.yigekuoyou.eraengine 两个模块的类型与属性。
# Qt 6.7+ 提供（见 Qt6QmlMacros.cmake 里的 QT_QML_GENERATE_QMLLS_INI）。
set(QT_QML_GENERATE_QMLLS_INI ON)

# 让 qmllint 能识别「哪些名字是 context property」：开启后 lint 目标会先跑
# qmlcontextpropertydump 再跑 qmllint，于是 QML 里凡是用到 context property 的地方
# 都会被 ContextProperties 告警抓住（Qt 文档《QT_QMLLINT_CONTEXT_PROPERTY_DUMP》:
# "this allows qmllint to warn on the usage of context properties in QML"）。
# 项目口径是不用 context property（main.cpp 已移除 currentDir，改走类型/单例），
# 这条就是防回退的护栏。
set(QT_QMLLINT_CONTEXT_PROPERTY_DUMP ON)

# 现代化的硬护栏：Qt 5 时代到项目下限版本之间「已废弃」的 API 一律编译期不可见
#（用了直接报错，而不是只在告警流里悄悄漂过去）。
# 依据：Qt 文档 / qtdeprecationdefinitions.h —— QT_DISABLE_DEPRECATED_UP_TO
#（Qt 6.5+ 的现名，旧名 QT_DISABLE_DEPRECATED_BEFORE）。Qt 自带的默认值是
# 0x050000（只禁 Qt 5 之前），这里按项目下限 6.11 收紧。
# 需要临时放宽（例如确认某个废弃 API 的旧行为）：-DEMUERA_QT_DISABLE_DEPRECATED_UP_TO=0x050000
set(EMUERA_QT_DISABLE_DEPRECATED_UP_TO "0x060B00"
    CACHE STRING "禁用（编译期报错）到该 Qt 版本为止的废弃 API（QT_DISABLE_DEPRECATED_UP_TO）")
add_compile_definitions(QT_DISABLE_DEPRECATED_UP_TO=${EMUERA_QT_DISABLE_DEPRECATED_UP_TO})

qt_add_executable(appemuera
    src/main.cpp
    src/platform/window_controller.cpp
    src/platform/window_controller.h
    src/platform/tray_controller.cpp
    src/platform/tray_controller.h
)

qt_add_qml_module(appemuera
    URI io.yigekuoyou.appemuera
    # 应用模块的 C++ 类型（WindowController）带着引擎模块的类型（GuiManager*），
    # 且 src/qml/*.qml 都 import 引擎模块：显式声明模块导入，生成的 qmldir 才会
    # 带上 `import io.yigekuoyou.eraengine`（DEPENDENCIES 写的是 target 名
    # "eraengine"，qmllint 按模块 URI 解析会找不到）—— qmllint/qmlls 也才能解析这些类型。
    IMPORTS io.yigekuoyou.eraengine
    SOURCES src/platform/game_library.cpp src/platform/game_library.h
    QML_FILES
        src/qml/Main.qml
        src/qml/NativeMenuBar.qml
        src/qml/EraRender.qml
        src/qml/TitleScreen.qml
        src/qml/Console.qml
        src/qml/ConsoleBlock.qml
        src/qml/AudioPlayers.qml
        src/qml/SettingsDialog.qml
        src/qml/AboutDialog.qml
)
set_target_properties(appemuera PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appemuera
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appemuera
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Widgets eraengine
)
# Android 打包：eraengine 现在是动态库，androiddeployqt 只带「主库 + Qt 库」，
# 项目自己的 .so 要显式列进 QT_ANDROID_EXTRA_LIBS 才会被塞进 APK 的 libs 目录
# （Qt 文档《QT_ANDROID_EXTRA_LIBS》：会被复制进 APK 的 libs 并在启动时加载；
#  来自构建树的库还必须有依赖关系 —— 上面 target_link_libraries 已提供）。
# QTP0002（Qt 6.6+）允许这里用生成器表达式。
if(ANDROID)
    set_property(TARGET appemuera PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    # androiddeployqt does not reliably discover shared QML module libraries
    # through a CMake target dependency. Include both the module backend and
    # its plugin explicitly so import io.yigekuoyou.eraengine can initialize.
    set_property(TARGET appemuera APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
        "$<TARGET_FILE:eraengine>"
        "$<TARGET_FILE:eraengineplugin>")
endif()
# D-Bus 可用时才链（实际只有桌面 Linux 用到），同时打开 EMUERA_HAS_DBUS
if(TARGET Qt6::DBus)
    target_link_libraries(appemuera PRIVATE Qt6::DBus)
    target_compile_definitions(appemuera PRIVATE EMUERA_HAS_DBUS)
endif()
# qmltyperegistration 生成的 include 用短名（<tray_controller.h>）
target_include_directories(appemuera PRIVATE src/platform)
    # 告警始终打开；「告警即错误」由 EMUERA_WARNINGS_AS_ERRORS 决定。
    # MSVC / AppleClang 的告警集合和开发用的 GCC 不同，CI 上可以关掉 -Werror，
    # 只看告警而不让构建失败（默认仍为 ON，保持本地开发的行为不变）。
    if(MSVC)
        target_compile_options(appemuera PRIVATE /W4)
        if(EMUERA_WARNINGS_AS_ERRORS)
            target_compile_options(appemuera PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(appemuera PRIVATE -Wall -Wextra -Wpedantic)
        if(EMUERA_WARNINGS_AS_ERRORS)
            target_compile_options(appemuera PRIVATE -Werror)
        endif()
    endif()
target_compile_definitions(appemuera
    PRIVATE
$<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>)
include(GNUInstallDirs)
# 安装后的可执行文件要能找到同前缀的动态库（eraengine 已是 SHARED）。
# 用 $ORIGIN 相对 RPATH（bin -> libdir），这样 `cmake --install --prefix <任意>`
# 都成立——绝对 RPATH 只在配置时的前缀下正确。macOS 交给 macdeployqt（CI 那一步）
# 处理 @rpath/install_name；Windows 没有 RPATH（需 DLL 在 exe 旁或 windeployqt）。
if(UNIX AND NOT APPLE)
    file(RELATIVE_PATH em_app_rel
         "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
         "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(appemuera PROPERTIES INSTALL_RPATH "$ORIGIN/${em_app_rel}")
endif()
install(TARGETS appemuera
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# Enable testing at top level
enable_testing()
include(CTest)

add_subdirectory(src/eraengine)
option(EMUERA_BUILD_TOOLS "Build engine-backed LSP and MCP servers" ON)
if(EMUERA_BUILD_TOOLS AND NOT ANDROID)
    add_subdirectory(src/tools)
endif()

if(BUILD_TESTING AND NOT ANDROID)
    qt_add_executable(test_game_library test/test_game_library.cpp
        src/platform/game_library.cpp src/platform/game_library.h)
    target_include_directories(test_game_library PRIVATE src/platform src/eraengine)
    target_link_libraries(test_game_library PRIVATE Qt6::Quick Qt6::Concurrent Qt6::Test eraengine)
    add_test(NAME test_game_library COMMAND test_game_library)
    set_tests_properties(test_game_library PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
