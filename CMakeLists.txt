cmake_minimum_required(VERSION 3.21)

project(moondrop-widget
    VERSION 0.1.0
    DESCRIPTION "KDE Plasma widget for MOONDROP headphone control (GAIA over Bluetooth RFCOMM)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus)
# KI18n provides i18nd() and, unlike Qt's QTranslator, can read gettext .mo files
find_package(KF6I18n REQUIRED)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Where to install the QML module (org/moondrop/backend).
# Qt knows its own import path, so ask it; fall back to <prefix>/<libdir>/qt6/qml.
# ---------------------------------------------------------------------------
set(MOONDROP_QML_DIR "" CACHE PATH "Directory to install the QML plugin into (default: Qt's import path)")

if(NOT MOONDROP_QML_DIR)
    find_program(QTPATHS_EXECUTABLE NAMES qtpaths6 qtpaths)
    if(QTPATHS_EXECUTABLE)
        execute_process(COMMAND ${QTPATHS_EXECUTABLE} --query QT_INSTALL_QML
                        OUTPUT_VARIABLE QTPATHS_QML_DIR
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
    endif()
    if(QTPATHS_QML_DIR AND EXISTS "${QTPATHS_QML_DIR}")
        set(MOONDROP_QML_DIR "${QTPATHS_QML_DIR}")
    else()
        set(MOONDROP_QML_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/qt6/qml")
    endif()
endif()

add_subdirectory(src)
add_subdirectory(cli)

message(STATUS "QML module install directory: ${MOONDROP_QML_DIR}")

install(DIRECTORY package/ DESTINATION ${CMAKE_INSTALL_DATADIR}/moondrop-widget/package)
install(PROGRAMS scripts/install-applet.sh DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME moondrop-widget-install-applet)
install(PROGRAMS scripts/uninstall.sh DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME moondrop-widget-uninstall)
