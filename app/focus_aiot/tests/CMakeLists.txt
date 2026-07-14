cmake_minimum_required(VERSION 3.16)
project(focus_aiot_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(focus_aiot_tests
  test_ui.c
  ../src/lcd.c
  ../src/lcd_icons.c
  ../src/ui_draw.c
  ../src/mimo.c)
target_include_directories(focus_aiot_tests PRIVATE ../include ../src)
target_compile_definitions(focus_aiot_tests PRIVATE
  UI_UNIT_TEST UI_MESSAGE_DURATION_MS=150 UI_ANIMATION_STEP_MS=20)
if(NOT WIN32)
  target_link_libraries(focus_aiot_tests PRIVATE pthread)
endif()
enable_testing()
add_test(NAME focus_aiot_tests COMMAND focus_aiot_tests)
