# fakeheadset.h declares a QObject, so AUTOMOC has to process it
add_executable(moondrop-cli main.cpp ../tools/fakeheadset.h)
target_include_directories(moondrop-cli PRIVATE ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(moondrop-cli PRIVATE moondropcore Qt6::Core)
install(TARGETS moondrop-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# development helper: render a QML file to a PNG offscreen
# fakeheadset.h declares a QObject, so listing it here lets AUTOMOC run on it
add_executable(preview ../tools/preview.cpp ../tools/fakeheadset.h)
target_link_libraries(preview PRIVATE moondropcore Qt6::Gui Qt6::Qml Qt6::Quick KF6::I18n)
target_include_directories(preview PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools)

# protocol self test (no Bluetooth required)
add_executable(moondrop-selftest ../tests/protocol.cpp)
target_include_directories(moondrop-selftest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(moondrop-selftest PRIVATE moondropcore Qt6::Core)

# connection / queue behaviour against the fake headset (no Bluetooth)
add_executable(moondrop-conncheck ../tests/connection.cpp ../tools/fakeheadset.h)
target_include_directories(moondrop-conncheck PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(moondrop-conncheck PRIVATE moondropcore Qt6::Core)
