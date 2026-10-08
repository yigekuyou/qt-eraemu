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
#
# LSP/MCP share one protocol library and the same eraengine frontend.
set(CMAKE_AUTOMOC ON)

add_library(eproto SHARED
    eproto/eproto_export.h
    eproto/json_rpc.cpp eproto/json_rpc.h
    eproto/erb_analyzer.cpp eproto/erb_analyzer.h
    eproto/lsp_server.cpp eproto/lsp_server.h
    eproto/mcp_server.cpp eproto/mcp_server.h
)
target_compile_definitions(eproto PRIVATE EPROTO_LIBRARY)
target_include_directories(eproto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/eproto)
# Internal engine headers currently use short includes across these directories.
target_include_directories(eproto PRIVATE
    ${CMAKE_SOURCE_DIR}/src/eraengine/Content
    ${CMAKE_SOURCE_DIR}/src/eraengine/Content/encoding
    ${CMAKE_SOURCE_DIR}/src/eraengine/GameData
    ${CMAKE_SOURCE_DIR}/src/eraengine/GameData/ast
    ${CMAKE_SOURCE_DIR}/src/eraengine/GameData/Variable
    ${CMAKE_SOURCE_DIR}/src/eraengine/GameProc
)
target_link_libraries(eproto PUBLIC Qt6::Core PRIVATE eraengine Qt6::Gui)
set_target_properties(eproto PROPERTIES
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

set(era_tool_targets eproto)
foreach(tool IN ITEMS eralsp eramcp)
    add_executable(${tool} ${tool}/main.cpp)
    target_link_libraries(${tool} PRIVATE eproto)
    list(APPEND era_tool_targets ${tool})
endforeach()

if(BUILD_TESTING)
    foreach(test IN ITEMS test_eproto test_erb_workspace test_lsp test_mcp test_transport)
        add_executable(${test} test/${test}.cpp)
        target_link_libraries(${test} PRIVATE eproto Qt6::Test)
        add_test(NAME ${test} COMMAND ${test})
        if(test STREQUAL "test_transport")
            add_dependencies(${test} eralsp eramcp)
        endif()
        list(APPEND era_tool_targets ${test})
    endforeach()
endif()

foreach(t IN LISTS era_tool_targets)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
        if(EMUERA_WARNINGS_AS_ERRORS)
            target_compile_options(${t} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
        if(EMUERA_WARNINGS_AS_ERRORS)
            target_compile_options(${t} PRIVATE -Werror)
        endif()
    endif()
endforeach()

include(GNUInstallDirs)
if(UNIX)
    if(APPLE)
        set(era_origin "@loader_path")
    else()
        set(era_origin "$ORIGIN")
    endif()
    file(RELATIVE_PATH era_lib_relative
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(eralsp eramcp PROPERTIES
        INSTALL_RPATH "${era_origin}/${era_lib_relative}")
    set_target_properties(eproto PROPERTIES INSTALL_RPATH "${era_origin}")
endif()
install(TARGETS eralsp eramcp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS eproto
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
