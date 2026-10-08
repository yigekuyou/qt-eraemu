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

project(eraengine VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR})



# 动态（SHARED）QML 模块 —— 而不是静态库。
# 理由（Qt 文档《qt_add_qml_module》Target Structure / 《qt_add_library》）：
#   * 静态 QML 模块没有可部署的实体：类型注册代码会被链进宿主可执行文件，
#     别人 install 之后既没有 qmldir/插件可加载，也没法 import；
#   * SHARED 时 Qt 会生成「后端库 + 独立插件库 + qmldir/typeinfo」，install 到
#     QML 导入路径后，任何 QML 应用（不链接本工程）都能 import io.yigekuoyou.eraengine。
# 显式写 SHARED（不用关键字时「Qt 是共享库就建共享库」，但显式更明确）。
qt_add_library(eraengine SHARED)
qt_add_qml_module(eraengine
    URI io.yigekuoyou.eraengine
    VERSION 1.0
    SOURCES eraengine.cpp eraengine.h eraengine_log.cpp eraengine_log.h
Config/config_loader.cpp
Config/config_loader.h
Content/csv_loader.cpp
Content/csv_loader_chara.cpp
Content/csv_loader.h
Content/erb_loader.cpp
Content/erb_loader.h
Content/ast_disk_cache.cpp
Content/ast_disk_cache.h
Content/encoding/text_encoding.cpp
Content/encoding/encoding_probe.cpp
Content/encoding/encoding_probe.h
Content/encoding/text_encoding.h
Content/constant_table.cpp
Content/constant_table.h
Content/erb_preprocessor.cpp
Content/erb_preprocessor.h

GameData/file_system_io.cpp
GameData/file_system_io.h
GameData/game_base_data.cpp
GameData/game_base_data.h
GameData/mt19937.cpp
GameData/mt19937.h

GameData/identifier_dictionary.cpp
GameData/identifier_dictionary.h

GameData/ast/expression_ast.cpp
GameData/ast/expression_ast.h
GameData/ast/expression_evaluator.cpp
GameData/ast/expression_evaluator.h
GameData/ast/expression_lexer.cpp
GameData/ast/expression_lexer.h
GameData/ast/expression_parser.cpp
GameData/ast/expression_parser.h
GameData/ast/operand_type.h
GameData/ast/operator_table.h
GameData/ast/function_types.h
GameData/ast/parse_diagnostic.cpp
GameData/ast/parse_diagnostic.h
GameData/ast/strform_parser.cpp
GameData/ast/strform_parser.h
GameData/ast/argument_parser.cpp
GameData/ast/argument_parser.h
GameData/ast/variable_table.cpp
GameData/ast/variable_table.h
GameData/ast/user_function.cpp
GameData/ast/user_function.h
GameData/ast/word.h
GameData/ast/logical_line.h
GameData/ast/print_template.cpp
GameData/ast/print_template.h
GameData/ast/ast_builder.cpp
GameData/ast/ast_builder.h

GameData/Function/function_system.cpp
GameData/Function/function_system.h

GameData/Variable/variable_config.cpp
GameData/Variable/variable_config.h
GameData/Variable/variable_storage.cpp
GameData/Variable/variable_storage.h
GameData/Variable/variable_types.cpp
GameData/Variable/variable_types.h

GameProc/era_parse_table.cpp
GameProc/era_parse_table.h
GameProc/execution_engine.cpp
GameProc/execution_engine.h
GameProc/ee_extension.h
GameProc/extension_registry.h
GameProc/ee_extension.cpp
GameProc/fork_extension.h
GameProc/fork_support.h
GameProc/fork_extension.cpp
GameProc/fork_xml.cpp
GameProc/fork_datatable.cpp
GameProc/script_runner.cpp
GameProc/script_runner.h
GameProc/process_state.cpp
GameProc/process_state.h
GameProc/script_processor.cpp
GameProc/script_processor.h
GameProc/signal_hub.cpp
GameProc/signal_hub.h
GameProc/system_state_machine.cpp
GameProc/system_state_machine.h
GameProc/system_status_manager.cpp
GameProc/system_status_manager.h
GameProc/user_defined_variable_data.cpp
GameProc/user_defined_variable_data.h

GameView/input_handler.cpp
GameView/input_handler.h
GameView/key_map.h
GameView/rendering_system.cpp
GameView/rendering_system.h
GameView/console_types.h
GameView/console_buffer.h
GameView/console_layout.cpp
GameView/console_layout.h
GameView/button_string_creator.cpp
GameView/button_string_creator.h
GameView/console_plane.cpp
GameView/console_plane.h
GameView/console_backend.cpp
GameView/console_backend.h
GameView/dbus_debug.cpp
GameView/dbus_debug.h
GameView/gui_manager.cpp
GameView/gui_manager.h
GameView/resource_image_provider.cpp
GameView/resource_image_provider.h
GameView/graphics_store.cpp
GameView/graphics_store.h
GameView/audio_pipeline_pool.cpp
GameView/audio_pipeline_pool.h
)

# Add QtConcurrent and OpenMP after the library is defined
target_link_libraries(eraengine
    PRIVATE Qt6::Quick Qt6::Core Qt6::Quick Qt6::Widgets Qt6::Concurrent)
# 可选依赖（见顶层 CMakeLists.txt 的说明）：有则用，没有就静默降级。
if(TARGET Qt6::DBus)
    target_link_libraries(eraengine PRIVATE Qt6::DBus)
    target_compile_definitions(eraengine PRIVATE EMUERA_HAS_DBUS)
endif()
if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(eraengine PRIVATE OpenMP::OpenMP_CXX)
endif()
target_include_directories(eraengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Content
    ${CMAKE_CURRENT_SOURCE_DIR}/Content/encoding
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/ast
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Function
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Variable
    ${CMAKE_CURRENT_SOURCE_DIR}/GameProc
    ${CMAKE_CURRENT_SOURCE_DIR}/GameView
)
target_include_directories(eraengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---- 安装：把 io.yigekuoyou.eraengine 装成「可被其它 QML 应用 import」的模块 ----
# 依据 Qt 文档《qt_query_qml_module》（Example 一节）：一个 QML 模块的可部署部件是
#   ① 后端库  ② 插件库  ③ qmldir  ④ typeinfo  ⑤ QML_FILES / RESOURCES（本模块无）
# 目录约定（qml/<URI 路径> 就是使用方的 QML 导入路径）：
#   <prefix>/<libdir>/qml/io/yigekuoyou/eraengine/   ← qmldir / typeinfo / 插件
#   <prefix>/<libdir>/                               ← 后端库
# 使用方：
#   * 环境变量：QML_IMPORT_PATH=<prefix>/<libdir>/qml
#   * 或 CMake：qt_generate_deploy_qml_app_script() 默认就取 qml/ 这一层
#     （见 QT_DEPLOY_QML_DIR / QT_DEPLOY_PREFIX）。
include(GNUInstallDirs)
set(EMUERA_QML_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/qml"
    CACHE STRING "QML 模块安装目录（相对安装前缀；即使用方的 QML 导入路径根）")

qt_query_qml_module(eraengine
    URI module_uri
    TARGET_PATH module_target_path
    PLUGIN_TARGET module_plugin_target
    QMLDIR module_qmldir
    TYPEINFO module_typeinfo
    QML_FILES module_qml_files
    QML_FILES_DEPLOY_PATHS module_qml_deploy_paths
    RESOURCES module_resources
    RESOURCES_DEPLOY_PATHS module_resources_deploy_paths
)
set(em_eraengine_module_dir "${EMUERA_QML_INSTALL_DIR}/${module_target_path}")

# ① 后端库：别人链接它（C++ 侧）或由插件在运行期依赖它
set_target_properties(eraengine PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
install(TARGETS eraengine
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# ② 插件库：与 qmldir 同目录（QML 引擎在这一层找 plugin）
if(module_plugin_target AND NOT module_plugin_target STREQUAL eraengine)
    install(TARGETS ${module_plugin_target}
        LIBRARY DESTINATION "${em_eraengine_module_dir}"
        RUNTIME DESTINATION "${em_eraengine_module_dir}")
    # 插件在 <libdir>/qml/<URI…>/ 下，后端库在 <libdir>：用 $ORIGIN 相对定位。
    # 不用绝对路径——绝对 RPATH 只在「配置时的 CMAKE_INSTALL_PREFIX」下正确，
    # 而 `cmake --install --prefix <别的目录>` 不会重写 RPATH。
    # （Windows 没有 RPATH，需把后端 DLL 放在 exe 旁或用 windeployqt，另行处理。）
    if(UNIX)
        file(RELATIVE_PATH em_plugin_rel
             "${CMAKE_INSTALL_PREFIX}/${em_eraengine_module_dir}"
             "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
        set_target_properties(${module_plugin_target} PROPERTIES
            INSTALL_RPATH "$ORIGIN/${em_plugin_rel}")
    endif()
endif()

# ③④ 模块元信息
install(FILES "${module_qmldir}" DESTINATION "${em_eraengine_module_dir}")
if(module_typeinfo)
    install(FILES "${module_typeinfo}" DESTINATION "${em_eraengine_module_dir}")
endif()

# ⑤ QML 文件与资源（本模块目前是纯 C++ 类型，两个列表为空；保留以备用）
list(LENGTH module_qml_files em_qml_count)
if(em_qml_count GREATER 0)
    math(EXPR em_qml_last "${em_qml_count} - 1")
    foreach(i RANGE 0 ${em_qml_last})
        list(GET module_qml_files ${i} em_qml_src)
        list(GET module_qml_deploy_paths ${i} em_qml_dst)
        get_filename_component(em_qml_name "${em_qml_dst}" NAME)
        get_filename_component(em_qml_subdir "${em_qml_dst}" DIRECTORY)
        install(FILES "${em_qml_src}"
            DESTINATION "${em_eraengine_module_dir}/${em_qml_subdir}"
            RENAME "${em_qml_name}")
    endforeach()
endif()
list(LENGTH module_resources em_res_count)
if(em_res_count GREATER 0)
    math(EXPR em_res_last "${em_res_count} - 1")
    foreach(i RANGE 0 ${em_res_last})
        list(GET module_resources ${i} em_res_src)
        list(GET module_resources_deploy_paths ${i} em_res_dst)
        get_filename_component(em_res_name "${em_res_dst}" NAME)
        get_filename_component(em_res_subdir "${em_res_dst}" DIRECTORY)
        install(FILES "${em_res_src}"
            DESTINATION "${em_eraengine_module_dir}/${em_res_subdir}"
            RENAME "${em_res_name}")
    endforeach()
endif()

# Keep warning policy explicit and compiler-specific.  Qt headers are included
# under the same policy as project code so new compatibility issues are visible.
    # 告警始终打开；「告警即错误」由 EMUERA_WARNINGS_AS_ERRORS（父作用域 option）决定
    if(MSVC)
        target_compile_options(eraengine PRIVATE /W4)
        if(EMUERA_WARNINGS_AS_ERRORS)
            target_compile_options(eraengine PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(eraengine PRIVATE -Wall -Wextra -Wpedantic)
        if(EMUERA_WARNINGS_AS_ERRORS)
            target_compile_options(eraengine PRIVATE -Werror)
        endif()
    endif()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------

# 所有测试共享的 include 目录
set(ERAENGINE_TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Content
    ${CMAKE_CURRENT_SOURCE_DIR}/Content/encoding
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/ast
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Function
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Variable
    ${CMAKE_CURRENT_SOURCE_DIR}/GameProc
    ${CMAKE_CURRENT_SOURCE_DIR}/GameView
)

# 声明一个测试可执行文件（默认不注册进 CTest）
function(eraengine_add_test target)
    add_executable(${target} test/${target}.cpp)
    target_link_libraries(${target} PRIVATE eraengine Qt6::Core Qt6::Qml Qt6::Quick Qt6::Widgets)
    target_include_directories(${target} PRIVATE ${ERAENGINE_TEST_INCLUDE_DIRS})
    if(EMUERA_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()
endfunction()

# 应用入口 / 完整流程测试
# test_cli 额外需要 DBus（外部输入通道：qdbus io.yigekuyou.emuera.testcli /testcli input 0）
eraengine_add_test(test_cli)
if(TARGET Qt6::DBus)
    target_link_libraries(test_cli PRIVATE Qt6::DBus)
    target_compile_definitions(test_cli PRIVATE EMUERA_HAS_DBUS)
endif()

# 纯单元测试（自包含、无需外部目录）
eraengine_add_test(test_era_parse_table)
eraengine_add_test(test_parse_table_ast)
eraengine_add_test(test_execution_chain)
eraengine_add_test(test_control_flow)
add_test(NAME test_control_flow COMMAND test_control_flow)
eraengine_add_test(test_console_backend)
eraengine_add_test(test_console_plane)
eraengine_add_test(test_gui_manager)
eraengine_add_test(test_resource_image)
eraengine_add_test(test_system_flow)
eraengine_add_test(test_input)
eraengine_add_test(test_key_map)
add_test(NAME test_key_map COMMAND test_key_map)
eraengine_add_test(test_statements)
eraengine_add_test(test_expression_system)
eraengine_add_test(test_expression_types)
eraengine_add_test(test_builtin_functions)
eraengine_add_test(test_user_functions)
eraengine_add_test(test_argument_types)
eraengine_add_test(test_variable_table)
eraengine_add_test(test_erb_preprocessor)
eraengine_add_test(test_async_load)
eraengine_add_test(test_text_encoding)
eraengine_add_test(test_sjis_game)
eraengine_add_test(test_encoding_probe)
eraengine_add_test(test_game_base_data)
eraengine_add_test(test_variable_storage)
eraengine_add_test(test_variable_mutability)
eraengine_add_test(test_csv_name_tables)
eraengine_add_test(test_train_reset)
eraengine_add_test(test_ecd_command_coverage)
eraengine_add_test(test_print_template)
eraengine_add_test(test_extension_registry)
eraengine_add_test(test_audio_pipeline)
add_test(NAME test_audio_pipeline COMMAND test_audio_pipeline)
eraengine_add_test(test_getconfig)
add_test(NAME test_getconfig COMMAND test_getconfig)
add_test(NAME test_print_template COMMAND test_print_template)
eraengine_add_test(test_ast_span)
target_link_libraries(test_ast_span PRIVATE Qt6::Test)
add_test(NAME test_ast_span COMMAND test_ast_span)
eraengine_add_test(test_parse_diagnostic)
target_link_libraries(test_parse_diagnostic PRIVATE Qt6::Test)
add_test(NAME test_parse_diagnostic COMMAND test_parse_diagnostic)
eraengine_add_test(test_print_whitespace)
target_link_libraries(test_print_whitespace PRIVATE Qt6::Test)
eraengine_add_test(test_image_layout)
add_test(NAME test_image_layout COMMAND test_image_layout)

# 需要外部目录参数的诊断/回归工具（编译校验，不进 CTest）
eraengine_add_test(test_erb_loader)
eraengine_add_test(test_expression_parsing)
eraengine_add_test(test_execution_engine)

# QML 侧测试（Qt Quick Test）：测 Console/ConsoleLine 的接线与呈现
if(TARGET Qt6::QuickTest)
    add_executable(test_qml_console test/test_qml_console.cpp)
    # eraengine 现在是**动态** QML 模块：链接后端库即完成类型注册，模块资源
    #（qmldir/typeinfo 的 qrc）随库一起加载，于是「按路径加载的 QML」也能
    # `import io.yigekuoyou.eraengine`。不再需要静态构建那套
    #「链接 eraengineplugin + Q_IMPORT_QML_PLUGIN」——Qt 文档《qt_import_qml_plugins》：
    # 该机制「只在 Qt 以静态方式构建时有效，非静态 Qt 下直接返回」。
    target_link_libraries(test_qml_console PRIVATE eraengine Qt6::QuickTest Qt6::Quick Qt6::Qml)
    target_include_directories(test_qml_console PRIVATE ${ERAENGINE_TEST_INCLUDE_DIRS})
    target_compile_definitions(test_qml_console PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/qml"
        CONSOLE_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../qml/Console.qml"
        EXAMPLE_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../test/example/resources")
    add_test(NAME test_qml_console
             COMMAND test_qml_console -input ${CMAKE_CURRENT_SOURCE_DIR}/test/qml)
    set_tests_properties(test_qml_console PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Register all tests with CTest
add_test(NAME test_era_parse_table COMMAND test_era_parse_table)
add_test(NAME test_parse_table_ast COMMAND test_parse_table_ast)
add_test(NAME test_execution_chain COMMAND test_execution_chain)
add_test(NAME test_console_backend COMMAND test_console_backend)
add_test(NAME test_console_plane COMMAND test_console_plane)
add_test(NAME test_gui_manager COMMAND test_gui_manager)
add_test(NAME test_resource_image COMMAND test_resource_image)
add_test(NAME test_system_flow COMMAND test_system_flow)
add_test(NAME test_input COMMAND test_input)
add_test(NAME test_statements COMMAND test_statements)
add_test(NAME test_expression_system COMMAND test_expression_system)
add_test(NAME test_expression_types COMMAND test_expression_types)
add_test(NAME test_builtin_functions COMMAND test_builtin_functions)
add_test(NAME test_user_functions COMMAND test_user_functions)
add_test(NAME test_argument_types COMMAND test_argument_types)
add_test(NAME test_variable_table COMMAND test_variable_table)
add_test(NAME test_erb_preprocessor COMMAND test_erb_preprocessor)
add_test(NAME test_text_encoding COMMAND test_text_encoding)
add_test(NAME test_sjis_game COMMAND test_sjis_game)
add_test(NAME test_encoding_probe COMMAND test_encoding_probe)
add_test(NAME test_print_whitespace COMMAND test_print_whitespace)
add_test(NAME test_variable_mutability COMMAND test_variable_mutability)
add_test(NAME test_csv_name_tables COMMAND test_csv_name_tables)
add_test(NAME test_train_reset COMMAND test_train_reset)
add_test(NAME test_ecd_command_coverage COMMAND test_ecd_command_coverage)

# 诊断工具：导出全部装载告警
eraengine_add_test(dump_warnings)
# 工具：把游戏目录的文本文件规范化为 UTF-8（dry-run 缺省）
eraengine_add_test(text_convert)
eraengine_add_test(expr_probe)
# qdbug 桩：字符串赋值右值求值诊断（eraTW「種族：[ARGS]」根因复现）
eraengine_add_test(qdbug_probe)
# qdbug 桩：拍平行表导出（确定性顺序，供离线核对拍平是否保序）
eraengine_add_test(dump_lines)
target_compile_definitions(eraengine
    PRIVATE
$<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>)
