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
