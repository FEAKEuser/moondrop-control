set(MOONDROP_SOURCES
    gaia.cpp
    transport.cpp
    rfcommclient.cpp
    moondropdevice.cpp
    devicediscovery.cpp
    deviceprofile.cpp
)

add_library(moondropbackend MODULE
    ${MOONDROP_SOURCES}
    plugin.cpp
)

target_include_directories(moondropbackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moondropbackend PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::DBus KF6::I18n)

# keep a ready-to-use QML module in the build tree so the applet can be tested
# without installing anything:
#     QML_IMPORT_PATH=$PWD/build plasmawindowed org.moondrop.control
set_target_properties(moondropbackend PROPERTIES
    OUTPUT_NAME moondropplugin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/org/moondrop/backend
)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/qmldir
               ${CMAKE_BINARY_DIR}/org/moondrop/backend/qmldir COPYONLY)

install(TARGETS moondropbackend LIBRARY DESTINATION ${MOONDROP_QML_DIR}/org/moondrop/backend)
install(FILES qmldir DESTINATION ${MOONDROP_QML_DIR}/org/moondrop/backend)

# the CLI links the same code as a static library (without the QML plugin)
add_library(moondropcore STATIC ${MOONDROP_SOURCES})
target_include_directories(moondropcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moondropcore PUBLIC Qt6::Core Qt6::DBus KF6::I18n)
