# Isolated B2 fixture project. Shared root CMake registry belongs to C2.
cmake_minimum_required(VERSION 3.25)
project(launcher_diagnostics_fixtures LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(launcher_diagnostics_test ../launcher_diagnostics_test.cpp
  ../../tools/launcher/diagnostics.cpp ../../tools/launcher/compatibility.cpp
  ../../tools/launcher/gpu.cpp)
target_include_directories(launcher_diagnostics_test PRIVATE "$ENV{VULKAN_SDK}/Include")
target_compile_definitions(launcher_diagnostics_test PRIVATE VK_NO_PROTOTYPES UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(launcher_diagnostics_test PRIVATE /W4 /utf-8)
target_link_libraries(launcher_diagnostics_test PRIVATE user32)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME launcher_diagnostics_fixtures COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../launcher_diagnostics_test.py" "$<TARGET_FILE:launcher_diagnostics_test>"
  "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
