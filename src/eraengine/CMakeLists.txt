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



qt_add_library(eraengine STATIC)
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
GameView/gui_manager.cpp
GameView/gui_manager.h
GameView/resource_image_provider.cpp
GameView/resource_image_provider.h
GameView/graphics_store.cpp
GameView/graphics_store.h
)

# Add QtConcurrent and OpenMP after the library is defined
target_link_libraries(eraengine
    PRIVATE Qt6::Quick Qt6::Core Qt6::DBus Qt6::Quick Qt6::Widgets Qt6::Concurrent
    PRIVATE OpenMP::OpenMP_CXX)
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

# Keep warning policy explicit and compiler-specific.  Qt headers are included
# under the same policy as project code so new compatibility issues are visible.
if(EMUERA_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(eraengine PRIVATE /W4 /WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(eraengine PRIVATE -Wall -Wextra -Wpedantic -Werror)
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
target_link_libraries(test_cli PRIVATE Qt6::DBus)

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
eraengine_add_test(test_print_template)
add_test(NAME test_print_template COMMAND test_print_template)
eraengine_add_test(test_print_whitespace)
target_link_libraries(test_print_whitespace PRIVATE Qt6::Test)

# 需要外部目录参数的诊断/回归工具（编译校验，不进 CTest）
eraengine_add_test(test_erb_loader)
eraengine_add_test(test_expression_parsing)
eraengine_add_test(test_execution_engine)

# QML 侧测试（Qt Quick Test）：测 Console/ConsoleLine 的接线与呈现
if(TARGET Qt6::QuickTest)
    add_executable(test_qml_console test/test_qml_console.cpp)
    target_link_libraries(test_qml_console PRIVATE eraengine Qt6::QuickTest Qt6::Quick Qt6::Qml)
    target_include_directories(test_qml_console PRIVATE ${ERAENGINE_TEST_INCLUDE_DIRS})
    target_compile_definitions(test_qml_console PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/qml"
        CONSOLE_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../qml/Console.qml")
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

# 诊断工具：导出全部装载告警
eraengine_add_test(dump_warnings)
# 工具：把游戏目录的文本文件规范化为 UTF-8（dry-run 缺省）
eraengine_add_test(text_convert)
eraengine_add_test(expr_probe)
