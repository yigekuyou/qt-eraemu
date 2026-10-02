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

project(emuera VERSION 0.1 LANGUAGES CXX)

option(EMUERA_WARNINGS_AS_ERRORS "Treat warnings as errors for project targets" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Quick Core DBus Widgets Concurrent Test QuickTest)
find_package(OpenMP REQUIRED)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appemuera
    src/main.cpp
)

qt_add_qml_module(appemuera
    URI io.yigekuoyou.appemuera
    QML_FILES
        src/qml/Main.qml
        src/qml/EraRender.qml
        src/qml/Console.qml
        src/qml/ConsoleBlock.qml
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
    PRIVATE Qt6::Quick Qt6::DBus Qt6::Widgets eraengine
)
    if(MSVC)
        target_compile_options(appemuera PRIVATE /W4 /WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(appemuera PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
target_compile_definitions(appemuera
    PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)
include(GNUInstallDirs)
install(TARGETS appemuera
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# Enable testing at top level
enable_testing()
include(CTest)

add_subdirectory(src/eraengine)
