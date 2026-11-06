cmake_minimum_required(VERSION 3.16)
project(autokey-kwin-bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
add_compile_definitions(QT_NO_KEYWORDS)

find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Qml Widgets)
find_package(KWin REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

add_library(autokeybridge MODULE autokeybridge.cpp main.cpp)
set_target_properties(autokeybridge PROPERTIES PREFIX "")
target_link_libraries(autokeybridge PRIVATE KWin::kwin Qt6::Core Qt6::Gui Qt6::DBus Qt6::Qml PkgConfig::XKBCOMMON)

if(NOT DEFINED KWIN_PLUGIN_INSTALL_DIR)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    execute_process(COMMAND ${_qmake} -query QT_INSTALL_PLUGINS
                    OUTPUT_VARIABLE _qt_plugins OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(KWIN_PLUGIN_INSTALL_DIR "${_qt_plugins}/kwin/plugins")
endif()
install(TARGETS autokeybridge DESTINATION "${KWIN_PLUGIN_INSTALL_DIR}")
