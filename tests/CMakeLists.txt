cmake_minimum_required(VERSION 3.19)
project(ActionMacroRegression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
enable_testing()
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
add_executable(actionmacro_regression
    actionmacro_test.cpp
    ${ROOT}/src/device/controller/actionmacro.cpp
    ${ROOT}/src/device/controller/actionmacro.h
    ${ROOT}/src/device/controller/bufferutil.cpp
    ${ROOT}/src/device/controller/inputconvert/controlmsg.cpp
)
target_include_directories(actionmacro_regression PRIVATE
    ${ROOT}/src/common ${ROOT}/src/device/android
    ${ROOT}/src/device/controller ${ROOT}/src/device/controller/inputconvert
)
target_link_libraries(actionmacro_regression PRIVATE Qt${QT_VERSION_MAJOR}::Core)
foreach(case_name IN ITEMS
    semantic_round_trip invalid_timestamps reversed_timestamps format_and_version
    invalid_event_arrays coordinate_bounds mixed_screens metadata kind_and_type
    malformed_pointer_fields empty_clipboard invalid_text transactional_load
    malformed_and_large_file finite_repeat legacy_format duration_and_interval
    invalid_duration emergency_release infinite_stop incomplete_loop_release
    recording_release_reload geometry_guard recording_geometry_change
    play_arguments transport_reentrancy overdue_stop busy_state
)
    add_test(NAME macro_${case_name} COMMAND actionmacro_regression ${case_name})
    set_tests_properties(macro_${case_name} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(macroexecution_tests macroexecution_test.cpp
    ${ROOT}/src/device/controller/actionmacro.cpp
    ${ROOT}/src/device/controller/actionmacro.h
    ${ROOT}/src/device/controller/bufferutil.cpp
    ${ROOT}/src/device/controller/inputconvert/controlmsg.cpp)
target_include_directories(macroexecution_tests PRIVATE
    ${ROOT}/src/common ${ROOT}/src/device/android
    ${ROOT}/src/device/controller ${ROOT}/src/device/controller/inputconvert)
target_link_libraries(macroexecution_tests PRIVATE Qt${QT_VERSION_MAJOR}::Core)
foreach(case_name IN ITEMS speed_bounds speed_eight pause_wait pause_interval time_limit
    limit_during_interval limit_excludes_pause stop_paused touch_boundary key_boundary
    hid_pause_release recording_pause record_stop_paused geometry_paused pause_reentrancy busy_paused same_timestamp)
    add_test(NAME execution_${case_name} COMMAND macroexecution_tests ${case_name})
    set_tests_properties(execution_${case_name} PROPERTIES TIMEOUT 15)
endforeach()

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)
add_executable(mousemapping_tests mousemapping_test.cpp
    ${ROOT}/src/device/controller/controller.cpp ${ROOT}/src/device/controller/controller.h
    ${ROOT}/src/device/controller/actionmacro.cpp ${ROOT}/src/device/controller/actionmacro.h
    ${ROOT}/src/device/controller/bufferutil.cpp
    ${ROOT}/src/device/controller/inputconvert/controlmsg.cpp
    ${ROOT}/src/device/controller/uhidkeyboard.cpp
    ${ROOT}/src/device/controller/inputconvert/inputconvertbase.cpp ${ROOT}/src/device/controller/inputconvert/inputconvertbase.h
    ${ROOT}/src/device/controller/inputconvert/inputconvertnormal.cpp ${ROOT}/src/device/controller/inputconvert/inputconvertnormal.h
    ${ROOT}/src/device/controller/inputconvert/inputconvertgame.cpp ${ROOT}/src/device/controller/inputconvert/inputconvertgame.h
    ${ROOT}/src/device/controller/inputconvert/keymap/keymap.cpp ${ROOT}/src/device/controller/inputconvert/keymap/keymap.h
    ${ROOT}/src/device/controller/receiver/receiver.cpp ${ROOT}/src/device/controller/receiver/receiver.h
    ${ROOT}/src/device/controller/receiver/devicemsg.cpp)
target_include_directories(mousemapping_tests PRIVATE
    ${ROOT}/src/common ${ROOT}/src/device/android ${ROOT}/src/device/controller
    ${ROOT}/src/device/controller/inputconvert ${ROOT}/src/device/controller/inputconvert/keymap
    ${ROOT}/src/device/controller/receiver ${ROOT}/src/device/server)
target_link_libraries(mousemapping_tests PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
foreach(case_name IN ITEMS left right middle back forward aliases drag multi twice android joystick namespaces move_no_leak toggle simultaneous macro pause toggle_cancels_delayed uhid_priority)
    add_test(NAME mouse_${case_name} COMMAND mousemapping_tests ${case_name})
    set_tests_properties(mouse_${case_name} PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
