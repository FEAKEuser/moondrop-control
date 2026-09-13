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

# channel scan check: finds the GAIA channel even when the first candidate
# accepts the connection and then stays silent.  Runs against the fake headset,
# or against real hardware when given a Bluetooth address.
add_executable(moondrop-scan-check ../tests/scan-check.cpp ../tools/fakeheadset.h)
target_include_directories(moondrop-scan-check PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(moondrop-scan-check PRIVATE moondropcore Qt6::Core)

# robustness check: rapid connect/disconnect cycles must not crash or hang
add_executable(moondrop-stress ../tests/stress.cpp ../tools/fakeheadset.h)
target_include_directories(moondrop-stress PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(moondrop-stress PRIVATE moondropcore Qt6::Core)



# EBUSY path: the scan must give up and report the conflict, not retry for ever
add_executable(moondrop-ebusy-check ../tests/ebusy.cpp ../tools/fakeheadset.h)
target_include_directories(moondrop-ebusy-check PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(moondrop-ebusy-check PRIVATE moondropcore Qt6::Core)



# device-profile matching (incl. the foreign-name trap)
add_executable(moondrop-profile-check ../tests/prof.cpp)
target_include_directories(moondrop-profile-check PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(moondrop-profile-check PRIVATE moondropcore Qt6::Core)


add_executable(moondrop-switch-check ../tests/switch.cpp ../tools/fakeheadset.h)
target_include_directories(moondrop-switch-check PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(moondrop-switch-check PRIVATE moondropcore Qt6::Core)

# BlueZ signal parsing: "Connected" must be believed, or the widget never picks
# the headphone up again after it was switched off and on
add_executable(moondrop-watchstate-check ../tests/watchstate.cpp)
target_include_directories(moondrop-watchstate-check PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(moondrop-watchstate-check PRIVATE moondropcore Qt6::Core Qt6::DBus)

# failover: follow the headphone that is actually connected (needs real BlueZ state)
add_executable(moondrop-failover-check ../tests/failover.cpp)
target_include_directories(moondrop-failover-check PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(moondrop-failover-check PRIVATE moondropcore Qt6::Core)

# automatic recovery after the headphone goes away (no manual "Retry now")
add_executable(moondrop-reconnect-check ../tests/reconnect.cpp ../tools/fakeheadset.h)
target_include_directories(moondrop-reconnect-check PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools)
target_link_libraries(moondrop-reconnect-check PRIVATE moondropcore Qt6::Core)

# diagnostic: print the BlueZ headphone events the applet reacts to
add_executable(moondrop-hpwatch ../tests/hpwatch.cpp)
target_include_directories(moondrop-hpwatch PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(moondrop-hpwatch PRIVATE moondropcore Qt6::Core)

# diagnostic: observe automatic recovery while physically switching headphones
add_executable(moondrop-watchswitch ../tests/watchswitch.cpp)
target_include_directories(moondrop-watchswitch PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(moondrop-watchswitch PRIVATE moondropcore Qt6::Core)

# the applet started while another headphone was already on (needs real BlueZ)
add_executable(moondrop-startup-wait-check ../tests/startup-wait.cpp)
target_include_directories(moondrop-startup-wait-check PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(moondrop-startup-wait-check PRIVATE moondropcore Qt6::Core)
