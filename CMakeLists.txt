cmake_minimum_required(VERSION 3.16)

project(emuera VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Core DBus)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appemuera
    src/main.cpp
)

qt_add_qml_module(appemuera
    URI io.yigekuoyou.appemuera
    QML_FILES
        src/Main.qml
)
set_target_properties(appemuera PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appemuera
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appemuera
    PRIVATE Qt6::Quick Qt6::DBus
)

include(GNUInstallDirs)
install(TARGETS appemuera
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
add_subdirectory(src/eraengine)