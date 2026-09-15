cmake_minimum_required(VERSION 3.16)

project(eraengine VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR})

find_package(Qt6 6.2 COMPONENTS Quick REQUIRED)

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

GameData/expression/expression_ast.cpp
GameData/expression/expression_ast.h
GameData/expression/expression_evaluator.cpp
GameData/expression/expression_evaluator.h
GameData/expression/expression_lexer.cpp
GameData/expression/expression_lexer.h
GameData/expression/expression_parser.cpp
GameData/expression/expression_parser.h
GameData/expression/expression_token.cpp
GameData/expression/expression_token.h
GameData/Function/function_system.cpp
GameData/Function/function_system.h
GameData/Function/label_dictionary.cpp
GameData/Function/label_dictionary.h
GameData/Variable/character_variables.h
GameData/Variable/system_variables.h
GameData/Variable/variable_identifier.cpp
GameData/Variable/variable_identifier.h
GameData/Variable/variable_storage.cpp
GameData/Variable/variable_storage.h
GameData/Variable/variable_storage.h.bak
GameData/Variable/variable_token.cpp
GameData/Variable/variable_token.h
GameData/Variable/variable_types.h
GameData/Variable/variable_config.cpp
GameData/Variable/variable_config.h

GameProc/event_manager.cpp
GameProc/event_manager.h
GameProc/execution_engine.cpp
GameProc/execution_engine.h
GameProc/game_flow_control.cpp
GameProc/game_flow_control.h
GameProc/instruction_system.cpp
GameProc/instruction_system.h
GameProc/logical_line_parser.cpp
GameProc/logical_line_parser.h
GameProc/process_state.cpp
GameProc/process_state.h
GameProc/script_line.cpp
GameProc/script_line.h
GameProc/script_processor.cpp
GameProc/script_processor.h
GameProc/system_processor.cpp
GameProc/system_processor.h



GameView/input_handler.cpp
GameView/input_handler.h
GameView/input_system.cpp
GameView/input_system.h
GameView/rendering_system.cpp
GameView/rendering_system.h
GameProc/function_label_line.h
SOURCES GameProc/user_defined_variable_data.h GameProc/user_defined_variable_data.cpp
)

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
target_include_directories(eraengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Content
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/expression
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Function
    ${CMAKE_CURRENT_SOURCE_DIR}/GameData/Variable
    ${CMAKE_CURRENT_SOURCE_DIR}/GameProc
    ${CMAKE_CURRENT_SOURCE_DIR}/GameView
)
target_compile_definitions(eraengine
    PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
target_link_libraries(eraengine
    PRIVATE Qt6::Quick Qt6::Core Qt6::DBus Qt6::Quick Qt6::Widgets)
target_include_directories(eraengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
