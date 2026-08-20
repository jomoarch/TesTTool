cmake_minimum_required(VERSION 3.16)
project(TesTTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---- 核心库 ----
add_library(tettool STATIC
  src/compiler.cpp
  src/cli_config.cpp
  src/test_packager.cpp
  src/run_program.cpp
  src/file_comparator.cpp
  src/test_runner.cpp
  src/test_result_file.cpp
  src/test_result_viewer.cpp
  src/file_picker/helpfile.cpp
  src/file_picker/listing.cpp
  src/file_picker/picker.cpp
  src/file_picker/renderer.cpp
  src/file_picker/text.cpp
  src/file_picker/wcwidth.cpp
  src/file_picker/file_picker.cpp
)
if(WIN32)
  target_sources(tettool PRIVATE src/file_picker/terminal_windows.cpp)
else()
  target_sources(tettool PRIVATE src/file_picker/terminal_posix.cpp)
endif()

target_include_directories(tettool PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/file_picker
)
if(WIN32)
  target_link_libraries(tettool PRIVATE kernel32 user32)
endif()

# ---- CLI 程序（各操作拆分在 src/ttt_cli/ 下）----
add_executable(ttt-cli
  src/ttt_cli/main.cpp
  src/ttt_cli/common.cpp
  src/ttt_cli/cmd_add.cpp
  src/ttt_cli/cmd_submit.cpp
  src/ttt_cli/cmd_view.cpp
  src/ttt_cli/cmd_remove.cpp
)
target_link_libraries(ttt-cli PRIVATE tettool)
# 复制帮助文件到可执行文件旁，使 file_picker 的 H 键帮助可显示
add_custom_command(TARGET ttt-cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/file_picker_help.txt
          ${CMAKE_BINARY_DIR}/file_picker_help.txt)

# ---- 演示程序 ----
add_executable(run_tests_demo test/src/run_tests_demo.cpp)
target_link_libraries(run_tests_demo PRIVATE tettool)
add_executable(view_result_demo test/src/view_result_demo.cpp)
target_link_libraries(view_result_demo PRIVATE tettool)

# ---- 单元测试 ----
add_executable(test_test_runner test/src/test_test_runner.cpp)
target_link_libraries(test_test_runner PRIVATE tettool)
add_executable(test_ttt_cli test/src/test_ttt_cli.cpp)
target_link_libraries(test_ttt_cli PRIVATE tettool)

enable_testing()
add_test(NAME test_runner_unit COMMAND test_test_runner)
add_test(NAME ttt_cli_config_unit COMMAND test_ttt_cli)
