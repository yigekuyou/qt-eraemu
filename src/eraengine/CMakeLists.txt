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
)

target_compile_definitions(eraengine
    PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
target_link_libraries(eraengine
    PRIVATE Qt6::Quick Qt6::Core Qt6::DBus Qt6::Quick)

target_include_directories(eraengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

