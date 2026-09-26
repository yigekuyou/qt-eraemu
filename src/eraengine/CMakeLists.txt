cmake_minimum_required(VERSION 3.16)

project(eraengine VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR})



qt_add_library(eraengine STATIC)
qt_add_qml_module(eraengine
    URI io.yigekuoyou.eraengine
    VERSION 1.0
    SOURCES eraengine.cpp eraengine.h
Config/config_loader.cpp
Config/config_loader.h
Content/csv_loader.cpp
Content/csv_loader.h
Content/erb_loader.cpp
Content/erb_loader.h

GameData/file_system_io.cpp
GameData/file_system_io.h
GameData/game_base_data.cpp
GameData/game_base_data.h

GameData/efficient_csv_loader.cpp
GameData/efficient_csv_loader.h
GameData/identifier_dictionary.cpp
GameData/identifier_dictionary.h

GameData/execution/memory_block.cpp
GameData/execution/memory_block.h
GameData/execution/script_memory_space.cpp
GameData/execution/script_memory_space.h

GameData/expression/expression_ast.cpp
GameData/expression/expression_ast.h
GameData/expression/expression_evaluator.cpp
GameData/expression/expression_evaluator.h
GameData/expression/expression_lexer.cpp
GameData/expression/expression_lexer.h
GameData/expression/expression_parser.cpp
GameData/expression/expression_parser.h

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
GameProc/function_label_line.h
GameProc/logical_line_parser.cpp
GameProc/logical_line_parser.h
GameProc/process_state.cpp
GameProc/process_state.h
GameProc/script_line.cpp
GameProc/script_line.h
GameProc/script_processor.cpp
GameProc/script_processor.h
GameProc/signal_hub.cpp
GameProc/signal_hub.h
GameProc/system_processor.cpp
GameProc/system_processor.h
GameProc/system_status_manager.cpp
GameProc/system_status_manager.h
GameProc/user_defined_variable_data.cpp
GameProc/user_defined_variable_data.h

GameView/input_handler.cpp
GameView/input_handler.h
GameView/rendering_system.cpp
GameView/rendering_system.h
)

# Add QtConcurrent and OpenMP after the library is defined
target_link_libraries(eraengine
    PRIVATE Qt6::Quick Qt6::Core Qt6::DBus Qt6::Quick Qt6::Widgets Qt6::Concurrent
    PRIVATE OpenMP::OpenMP_CXX)
target_include_directories(eraengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Content
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/execution
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/expression
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Function
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Variable
    ${CMAKE_CURRENT_SOURCE_DIR}/GameProc
    ${CMAKE_CURRENT_SOURCE_DIR}/GameView
)
target_include_directories(eraengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Test executables
add_executable(test_cli
test/test_cli.cpp
)
target_link_libraries(test_cli PRIVATE eraengine Qt6::Core Qt6::Qml Qt6::Widgets)
target_include_directories(test_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Content
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/expression
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Function
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Variable
    ${CMAKE_CURRENT_SOURCE_DIR}/GameProc
    ${CMAKE_CURRENT_SOURCE_DIR}/GameView
)

# Position-region unit test (任务 3)
add_executable(test_era_parse_table
test/test_era_parse_table.cpp
)
target_link_libraries(test_era_parse_table PRIVATE eraengine Qt6::Core)
target_include_directories(test_era_parse_table PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Content
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/execution
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/expression
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Function
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Variable
    ${CMAKE_CURRENT_SOURCE_DIR}/GameProc
    ${CMAKE_CURRENT_SOURCE_DIR}/GameView
)

# Regression tests for Phase D (CALL/RETURN接线)
add_executable(test_regression
test/test_regression.cpp
)
target_link_libraries(test_regression PRIVATE eraengine Qt6::Core Qt6::Qml Qt6::Widgets)
target_include_directories(test_regression PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Content
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/expression
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Function
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Variable
    ${CMAKE_CURRENT_SOURCE_DIR}/GameProc
    ${CMAKE_CURRENT_SOURCE_DIR}/GameView
)

# Enable testing
enable_testing()

# Register all tests with CTest
add_test(NAME test_era_parse_table COMMAND test_era_parse_table)
add_test(NAME test_regression COMMAND test_regression)
