cmake_minimum_required(VERSION 3.16)
project(focus_aiot_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(focus_aiot_tests
  test_ui.c
  ../ui/lcd.c
  ../ui/lcd_icons.c
  ../ui/mimo.c
  ../ui/ui_cjk_font.c
  ../ui/ui_draw.c)
target_include_directories(focus_aiot_tests PRIVATE ../api ../ui)
target_compile_definitions(focus_aiot_tests PRIVATE
  UI_UNIT_TEST UI_MESSAGE_DURATION_MS=150 UI_ANIMATION_STEP_MS=20)
if(NOT WIN32)
  target_link_libraries(focus_aiot_tests PRIVATE pthread)
endif()
enable_testing()
add_test(NAME focus_aiot_tests COMMAND focus_aiot_tests)

add_executable(focus_aiot_behavior_tests
  test_behavior.c
  ../behavior/behavior.c
  ../behavior/behavior_config.c)
target_include_directories(focus_aiot_behavior_tests PRIVATE
  ../api ../config)
if(NOT WIN32)
  target_compile_options(focus_aiot_behavior_tests PRIVATE
    -Wall -Wextra -Werror)
endif()
add_test(NAME focus_aiot_behavior_tests COMMAND focus_aiot_behavior_tests)

add_executable(focus_aiot_perception_tests
  test_perception.c
  ../perception/perception.c)
target_include_directories(focus_aiot_perception_tests PRIVATE
  ../api)
target_compile_definitions(focus_aiot_perception_tests PRIVATE
  PERCEPTION_MOCK)
if(NOT WIN32)
  target_compile_options(focus_aiot_perception_tests PRIVATE
    -Wall -Wextra -Werror)
endif()
add_test(NAME focus_aiot_perception_tests COMMAND focus_aiot_perception_tests)
